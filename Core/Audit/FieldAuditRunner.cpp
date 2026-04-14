#include "Core/Audit/FieldAuditRunner.h"

#include "Core/Audit/FieldInventoryService.h"
#include "Core/Audit/FieldValidationService.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace Core::Audit {
namespace {

[[nodiscard]] uint64_t HashString(const std::string_view value) {
    constexpr uint64_t kFnvOffset = 14695981039346656037ull;
    constexpr uint64_t kFnvPrime = 1099511628211ull;

    uint64_t hash = kFnvOffset;
    for (const unsigned char symbol : value) {
        hash ^= static_cast<uint64_t>(symbol);
        hash *= kFnvPrime;
    }
    return hash;
}

[[nodiscard]] std::string HashToHex(const uint64_t value) {
    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << value;
    return stream.str();
}

[[nodiscard]] bool EnsureOutputDirectory(const std::filesystem::path& outputDirectory) {
    std::error_code errorCode;
    const bool outputExists = std::filesystem::exists(outputDirectory, errorCode);
    if (errorCode) {
        return false;
    }

    if (outputExists) {
        const bool isDirectory = std::filesystem::is_directory(outputDirectory, errorCode);
        return !errorCode && isDirectory;
    }

    std::filesystem::create_directories(outputDirectory, errorCode);
    return !errorCode;
}

[[nodiscard]] std::string ComputeValidationDigest(const std::vector<FieldValidationReport>& reports) {
    std::string digestMaterial;
    digestMaterial.reserve((reports.size() * 48u) + 32u);
    for (const FieldValidationReport& report : reports) {
        digestMaterial.append(report.Scope);
        digestMaterial.push_back('|');
        digestMaterial.append(report.DeterministicDigest);
        digestMaterial.push_back('|');
        digestMaterial.append(std::to_string(report.Summary.TotalFindingCount));
        digestMaterial.push_back('\n');
    }
    return HashToHex(HashString(digestMaterial));
}

[[nodiscard]] std::string ComputePhaseDigest(const FieldAuditPhaseStamp& phaseStamp) {
    std::string digestMaterial;
    digestMaterial.reserve(160u);
    digestMaterial.append(phaseStamp.PhaseId);
    digestMaterial.push_back('|');
    digestMaterial.append(phaseStamp.PhaseLabel);
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(phaseStamp.PhaseOrdinal));
    digestMaterial.push_back('|');
    digestMaterial.append(phaseStamp.InventoryDigest);
    digestMaterial.push_back('|');
    digestMaterial.append(phaseStamp.ValidationDigest);
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(phaseStamp.TotalFindingCount));
    return HashToHex(HashString(digestMaterial));
}

[[nodiscard]] std::string ComputeRunDigest(const FieldAuditRunReport& report) {
    std::string digestMaterial;
    digestMaterial.reserve((report.PhaseStamps.size() * 80u) + 64u);
    digestMaterial.append(report.Scope);
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(report.TotalPhases));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(report.TotalFindingCount));
    digestMaterial.push_back('\n');

    for (const FieldAuditPhaseStamp& phaseStamp : report.PhaseStamps) {
        digestMaterial.append(phaseStamp.PhaseId);
        digestMaterial.push_back('|');
        digestMaterial.append(phaseStamp.DeterministicPhaseDigest);
        digestMaterial.push_back('|');
        digestMaterial.append(phaseStamp.InventoryDigest);
        digestMaterial.push_back('|');
        digestMaterial.append(phaseStamp.ValidationDigest);
        digestMaterial.push_back('|');
        digestMaterial.append(std::to_string(phaseStamp.TotalFindingCount));
        digestMaterial.push_back('\n');
    }

    return HashToHex(HashString(digestMaterial));
}

} // namespace

Result<FieldAuditRunReport> RunRuntimeStateFieldAudit(const FieldAuditRunRequest& request) {
    if (request.Scope.empty() || request.OutputDirectory.empty()) {
        return Result<FieldAuditRunReport>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (request.Scope != "runtime-state") {
        return Result<FieldAuditRunReport>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    if (!EnsureOutputDirectory(request.OutputDirectory)) {
        return Result<FieldAuditRunReport>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    FieldInventoryRequest serializedRequest{};
    serializedRequest.Scope = "serialized";
    serializedRequest.OutputDirectory = request.OutputDirectory / "baseline-serialized";
    const Result<FieldInventorySnapshot> serializedInventory = GenerateSerializedFieldInventory(serializedRequest);
    if (!serializedInventory.Ok) {
        return Result<FieldAuditRunReport>::Failure(serializedInventory.Error);
    }

    FieldInventoryRequest protocolRequest{};
    protocolRequest.Scope = "protocol";
    protocolRequest.OutputDirectory = request.OutputDirectory / "baseline-protocol";
    const Result<FieldInventorySnapshot> protocolInventory = GenerateProtocolFieldInventory(protocolRequest);
    if (!protocolInventory.Ok) {
        return Result<FieldAuditRunReport>::Failure(protocolInventory.Error);
    }

    const std::array<std::pair<std::string_view, std::string_view>, 3> runtimePhases = {
        std::pair<std::string_view, std::string_view>("startup", "Startup"),
        std::pair<std::string_view, std::string_view>("loaded-scene", "LoadedScene"),
        std::pair<std::string_view, std::string_view>("teardown", "Teardown")};

    std::vector<FieldAuditPhaseStamp> phaseStamps;
    phaseStamps.reserve(runtimePhases.size());
    uint32_t totalFindingCount = 0;
    for (std::size_t phaseIndex = 0; phaseIndex < runtimePhases.size(); ++phaseIndex) {
        const auto [phaseId, phaseLabel] = runtimePhases[phaseIndex];
        const std::filesystem::path phaseRoot = request.OutputDirectory / ("runtime-phase-" + std::to_string(phaseIndex + 1u));

        FieldInventoryRequest runtimeRequest{};
        runtimeRequest.Scope = "runtime";
        runtimeRequest.OutputDirectory = phaseRoot / "runtime";
        const Result<FieldInventorySnapshot> runtimeInventory = GenerateRuntimeFieldInventory(runtimeRequest);
        if (!runtimeInventory.Ok) {
            return Result<FieldAuditRunReport>::Failure(runtimeInventory.Error);
        }

        const auto runValidation = [&](const std::string_view validationScope, const std::filesystem::path& outputDirectory)
            -> Result<FieldValidationReport> {
            FieldValidationRequest validationRequest{};
            validationRequest.Scope = std::string(validationScope);
            validationRequest.OutputDirectory = outputDirectory;
            validationRequest.RuntimeSnapshot = runtimeInventory.Value;
            validationRequest.SerializedSnapshot = serializedInventory.Value;
            validationRequest.ProtocolSnapshot = protocolInventory.Value;

            if (validationScope == "type-nullability") {
                return ValidateFieldTypeAndNullabilityContracts(validationRequest);
            }
            if (validationScope == "range-enum-pattern-domains") {
                return ValidateFieldRangeEnumAndPatternDomains(validationRequest);
            }
            if (validationScope == "cross-field-invariant-rules") {
                return ValidateCrossFieldInvariantRules(validationRequest);
            }
            if (validationScope == "evolution-compatibility") {
                return ValidateFieldEvolutionCompatibility(validationRequest);
            }
            return Result<FieldValidationReport>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
        };

        const std::array<std::pair<std::string_view, std::string_view>, 4> validationStages = {
            std::pair<std::string_view, std::string_view>("type-nullability", "validation-type-nullability"),
            std::pair<std::string_view, std::string_view>("range-enum-pattern-domains", "validation-range-enum-pattern"),
            std::pair<std::string_view, std::string_view>("cross-field-invariant-rules", "validation-cross-field-invariants"),
            std::pair<std::string_view, std::string_view>("evolution-compatibility", "validation-evolution")};

        std::vector<FieldValidationReport> phaseValidationReports;
        phaseValidationReports.reserve(validationStages.size());
        uint32_t phaseFindingCount = 0;
        for (const auto& [validationScope, directoryName] : validationStages) {
            const Result<FieldValidationReport> validationReport =
                runValidation(validationScope, phaseRoot / std::string(directoryName));
            if (!validationReport.Ok) {
                return Result<FieldAuditRunReport>::Failure(validationReport.Error);
            }

            phaseFindingCount += validationReport.Value.Summary.TotalFindingCount;
            phaseValidationReports.push_back(validationReport.Value);
        }

        FieldAuditPhaseStamp phaseStamp{};
        phaseStamp.PhaseId = std::string(phaseId);
        phaseStamp.PhaseLabel = std::string(phaseLabel);
        phaseStamp.PhaseOrdinal = static_cast<uint32_t>(phaseIndex + 1u);
        phaseStamp.InventoryDigest = runtimeInventory.Value.DeterministicDigest;
        phaseStamp.ValidationDigest = ComputeValidationDigest(phaseValidationReports);
        phaseStamp.TotalFindingCount = phaseFindingCount;
        phaseStamp.DeterministicPhaseDigest = ComputePhaseDigest(phaseStamp);
        totalFindingCount += phaseFindingCount;

        phaseStamps.push_back(std::move(phaseStamp));
    }

    FieldAuditRunReport report{};
    report.Scope = request.Scope;
    report.OutputDirectory = request.OutputDirectory;
    report.PhaseStamps = std::move(phaseStamps);
    report.TotalPhases = static_cast<uint32_t>(report.PhaseStamps.size());
    report.TotalFindingCount = totalFindingCount;
    report.DeterministicDigest = ComputeRunDigest(report);

    return Result<FieldAuditRunReport>::Success(std::move(report));
}

} // namespace Core::Audit
