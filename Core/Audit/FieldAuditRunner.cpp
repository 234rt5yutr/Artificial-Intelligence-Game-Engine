#include "Core/Audit/FieldAuditRunner.h"

#include "Core/Audit/FieldInventoryService.h"
#include "Core/Audit/FieldValidationService.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
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
    digestMaterial.push_back('\n');
    for (const FieldValidationFinding& finding : phaseStamp.Findings) {
        digestMaterial.append(finding.RuleId);
        digestMaterial.push_back('|');
        digestMaterial.append(finding.StableFieldKey);
        digestMaterial.push_back('|');
        digestMaterial.append(finding.DomainPair);
        digestMaterial.push_back('|');
        digestMaterial.append(finding.LeftEvidence.FieldId);
        digestMaterial.push_back('|');
        digestMaterial.append(finding.RightEvidence.FieldId);
        digestMaterial.push_back('|');
        digestMaterial.append(finding.MigrationRecommendationPlaceholder);
        digestMaterial.push_back('\n');
    }
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

[[nodiscard]] std::string ComputeSnapshotDigest(const FieldInventorySnapshot& snapshot) {
    std::string digestMaterial;
    digestMaterial.reserve((snapshot.Entries.size() * 64u) + 32u);
    digestMaterial.append(snapshot.Scope);
    digestMaterial.push_back('|');
    for (const FieldInventoryEntry& entry : snapshot.Entries) {
        digestMaterial.append(entry.FieldId);
        digestMaterial.push_back('|');
        digestMaterial.append(entry.OwnerSubsystem);
        digestMaterial.push_back('|');
        digestMaterial.push_back(entry.Required ? '1' : '0');
        digestMaterial.push_back('\n');
    }
    return HashToHex(HashString(digestMaterial));
}

[[nodiscard]] FieldInventorySnapshot BuildDomainFilteredSnapshot(const FieldInventorySnapshot& sourceSnapshot,
                                                                 const std::filesystem::path& outputDirectory,
                                                                 const std::vector<std::string_view>& includedDomains) {
    std::set<std::string_view> includedDomainSet(includedDomains.begin(), includedDomains.end());
    std::vector<FieldInventoryEntry> filteredEntries;
    filteredEntries.reserve(sourceSnapshot.Entries.size());
    for (const FieldInventoryEntry& entry : sourceSnapshot.Entries) {
        if (includedDomainSet.empty() || includedDomainSet.contains(entry.Domain)) {
            filteredEntries.push_back(entry);
        }
    }

    if (filteredEntries.empty()) {
        filteredEntries = sourceSnapshot.Entries;
    }

    std::sort(filteredEntries.begin(), filteredEntries.end(), [](const FieldInventoryEntry& left, const FieldInventoryEntry& right) {
        if (left.FieldId != right.FieldId) {
            return left.FieldId < right.FieldId;
        }
        if (left.OwnerSubsystem != right.OwnerSubsystem) {
            return left.OwnerSubsystem < right.OwnerSubsystem;
        }
        return left.SourceTrace.SourceFile < right.SourceTrace.SourceFile;
    });

    std::set<std::string> domainSet;
    for (const FieldInventoryEntry& entry : filteredEntries) {
        domainSet.insert(entry.Domain);
    }

    FieldInventorySnapshot filteredSnapshot{};
    filteredSnapshot.Scope = sourceSnapshot.Scope;
    filteredSnapshot.OutputDirectory = outputDirectory;
    filteredSnapshot.Domains.assign(domainSet.begin(), domainSet.end());
    filteredSnapshot.Entries = std::move(filteredEntries);
    filteredSnapshot.DeterministicDigest = ComputeSnapshotDigest(filteredSnapshot);
    return filteredSnapshot;
}

[[nodiscard]] Result<FieldValidationReport> RunValidationStage(const std::string_view validationScope,
                                                               const std::filesystem::path& outputDirectory,
                                                               const FieldInventorySnapshot& runtimeSnapshot,
                                                               const FieldInventorySnapshot& serializedSnapshot,
                                                               const FieldInventorySnapshot& protocolSnapshot) {
    FieldValidationRequest validationRequest{};
    validationRequest.Scope = std::string(validationScope);
    validationRequest.OutputDirectory = outputDirectory;
    validationRequest.RuntimeSnapshot = runtimeSnapshot;
    validationRequest.SerializedSnapshot = serializedSnapshot;
    validationRequest.ProtocolSnapshot = protocolSnapshot;

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
}

[[nodiscard]] Result<FieldAuditPhaseStamp> RunAuditPhase(const std::string_view phaseId,
                                                         const std::string_view phaseLabel,
                                                         const uint32_t phaseOrdinal,
                                                         const std::filesystem::path& phaseRoot,
                                                         const FieldInventorySnapshot& runtimeSnapshot,
                                                         const FieldInventorySnapshot& serializedSnapshot,
                                                         const FieldInventorySnapshot& protocolSnapshot) {
    const std::array<std::pair<std::string_view, std::string_view>, 4> validationStages = {
        std::pair<std::string_view, std::string_view>("type-nullability", "validation-type-nullability"),
        std::pair<std::string_view, std::string_view>("range-enum-pattern-domains", "validation-range-enum-pattern"),
        std::pair<std::string_view, std::string_view>("cross-field-invariant-rules", "validation-cross-field-invariants"),
        std::pair<std::string_view, std::string_view>("evolution-compatibility", "validation-evolution")};

    std::vector<FieldValidationReport> phaseValidationReports;
    phaseValidationReports.reserve(validationStages.size());
    std::vector<FieldValidationFinding> phaseFindings;
    uint32_t phaseFindingCount = 0;
    for (const auto& [validationScope, directoryName] : validationStages) {
        const Result<FieldValidationReport> validationReport = RunValidationStage(
            validationScope, phaseRoot / std::string(directoryName), runtimeSnapshot, serializedSnapshot, protocolSnapshot);
        if (!validationReport.Ok) {
            return Result<FieldAuditPhaseStamp>::Failure(validationReport.Error);
        }

        phaseFindingCount += validationReport.Value.Summary.TotalFindingCount;
        phaseFindings.insert(phaseFindings.end(),
                             validationReport.Value.Findings.begin(),
                             validationReport.Value.Findings.end());
        phaseValidationReports.push_back(validationReport.Value);
    }

    std::sort(phaseFindings.begin(),
              phaseFindings.end(),
              [](const FieldValidationFinding& left, const FieldValidationFinding& right) {
                  if (left.StableFieldKey != right.StableFieldKey) {
                      return left.StableFieldKey < right.StableFieldKey;
                  }
                  if (left.RuleId != right.RuleId) {
                      return left.RuleId < right.RuleId;
                  }
                  if (left.DomainPair != right.DomainPair) {
                      return left.DomainPair < right.DomainPair;
                  }
                  if (left.LeftEvidence.FieldId != right.LeftEvidence.FieldId) {
                      return left.LeftEvidence.FieldId < right.LeftEvidence.FieldId;
                  }
                  return left.RightEvidence.FieldId < right.RightEvidence.FieldId;
              });

    FieldAuditPhaseStamp phaseStamp{};
    phaseStamp.PhaseId = std::string(phaseId);
    phaseStamp.PhaseLabel = std::string(phaseLabel);
    phaseStamp.PhaseOrdinal = phaseOrdinal;
    phaseStamp.InventoryDigest = HashToHex(HashString(
        runtimeSnapshot.DeterministicDigest + "|" + serializedSnapshot.DeterministicDigest + "|" + protocolSnapshot.DeterministicDigest));
    phaseStamp.ValidationDigest = ComputeValidationDigest(phaseValidationReports);
    phaseStamp.TotalFindingCount = phaseFindingCount;
    phaseStamp.Findings = std::move(phaseFindings);
    phaseStamp.DeterministicPhaseDigest = ComputePhaseDigest(phaseStamp);
    return Result<FieldAuditPhaseStamp>::Success(std::move(phaseStamp));
}

[[nodiscard]] FieldAuditRunReport BuildRunReport(const FieldAuditRunRequest& request,
                                                 std::vector<FieldAuditPhaseStamp>&& phaseStamps) {
    FieldAuditRunReport report{};
    report.Scope = request.Scope;
    report.OutputDirectory = request.OutputDirectory;
    report.PhaseStamps = std::move(phaseStamps);
    report.TotalPhases = static_cast<uint32_t>(report.PhaseStamps.size());
    for (const FieldAuditPhaseStamp& phaseStamp : report.PhaseStamps) {
        report.TotalFindingCount += phaseStamp.TotalFindingCount;
    }
    report.DeterministicDigest = ComputeRunDigest(report);
    return report;
}

[[nodiscard]] Result<FieldInventorySnapshot> GenerateBaselineSnapshot(const std::string_view scope,
                                                                      const std::filesystem::path& outputDirectory) {
    FieldInventoryRequest request{};
    request.Scope = std::string(scope);
    request.OutputDirectory = outputDirectory;
    if (scope == "runtime") {
        return GenerateRuntimeFieldInventory(request);
    }
    if (scope == "serialized") {
        return GenerateSerializedFieldInventory(request);
    }
    if (scope == "protocol") {
        return GenerateProtocolFieldInventory(request);
    }
    return Result<FieldInventorySnapshot>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
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

    const Result<FieldInventorySnapshot> serializedInventory =
        GenerateBaselineSnapshot("serialized", request.OutputDirectory / "baseline-serialized");
    if (!serializedInventory.Ok) {
        return Result<FieldAuditRunReport>::Failure(serializedInventory.Error);
    }

    const Result<FieldInventorySnapshot> protocolInventory =
        GenerateBaselineSnapshot("protocol", request.OutputDirectory / "baseline-protocol");
    if (!protocolInventory.Ok) {
        return Result<FieldAuditRunReport>::Failure(protocolInventory.Error);
    }

    const std::array<std::pair<std::string_view, std::string_view>, 3> runtimePhases = {
        std::pair<std::string_view, std::string_view>("startup", "Startup"),
        std::pair<std::string_view, std::string_view>("loaded-scene", "LoadedScene"),
        std::pair<std::string_view, std::string_view>("teardown", "Teardown")};

    std::vector<FieldAuditPhaseStamp> phaseStamps;
    phaseStamps.reserve(runtimePhases.size());
    for (std::size_t phaseIndex = 0; phaseIndex < runtimePhases.size(); ++phaseIndex) {
        const auto [phaseId, phaseLabel] = runtimePhases[phaseIndex];
        const std::filesystem::path phaseRoot = request.OutputDirectory / ("runtime-phase-" + std::to_string(phaseIndex + 1u));

        const Result<FieldInventorySnapshot> runtimeInventory =
            GenerateBaselineSnapshot("runtime", phaseRoot / "runtime-inventory");
        if (!runtimeInventory.Ok) {
            return Result<FieldAuditRunReport>::Failure(runtimeInventory.Error);
        }

        const Result<FieldAuditPhaseStamp> phaseStamp = RunAuditPhase(phaseId,
                                                                      phaseLabel,
                                                                      static_cast<uint32_t>(phaseIndex + 1u),
                                                                      phaseRoot,
                                                                      runtimeInventory.Value,
                                                                      serializedInventory.Value,
                                                                      protocolInventory.Value);
        if (!phaseStamp.Ok) {
            return Result<FieldAuditRunReport>::Failure(phaseStamp.Error);
        }
        phaseStamps.push_back(phaseStamp.Value);
    }

    return Result<FieldAuditRunReport>::Success(BuildRunReport(request, std::move(phaseStamps)));
}

Result<FieldAuditRunReport> RunCookedAndPackagedArtifactFieldAudit(const FieldAuditRunRequest& request) {
    if (request.Scope.empty() || request.OutputDirectory.empty()) {
        return Result<FieldAuditRunReport>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (request.Scope != "cooked-packaged-artifacts") {
        return Result<FieldAuditRunReport>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    if (!EnsureOutputDirectory(request.OutputDirectory)) {
        return Result<FieldAuditRunReport>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    const Result<FieldInventorySnapshot> runtimeInventory =
        GenerateBaselineSnapshot("runtime", request.OutputDirectory / "baseline-runtime");
    if (!runtimeInventory.Ok) {
        return Result<FieldAuditRunReport>::Failure(runtimeInventory.Error);
    }

    const Result<FieldInventorySnapshot> serializedInventory =
        GenerateBaselineSnapshot("serialized", request.OutputDirectory / "baseline-serialized");
    if (!serializedInventory.Ok) {
        return Result<FieldAuditRunReport>::Failure(serializedInventory.Error);
    }

    const Result<FieldInventorySnapshot> protocolInventory =
        GenerateBaselineSnapshot("protocol", request.OutputDirectory / "baseline-protocol");
    if (!protocolInventory.Ok) {
        return Result<FieldAuditRunReport>::Failure(protocolInventory.Error);
    }

    std::vector<FieldAuditPhaseStamp> phaseStamps;
    phaseStamps.reserve(4u);

    const auto runArtifactPhase = [&](const std::string_view phaseId,
                                      const std::string_view phaseLabel,
                                      const uint32_t phaseOrdinal,
                                      const std::vector<std::string_view>& serializedDomains) -> Result<FieldAuditPhaseStamp> {
        const std::filesystem::path phaseRoot = request.OutputDirectory / ("artifact-phase-" + std::to_string(phaseOrdinal));
        const FieldInventorySnapshot filteredSerialized = BuildDomainFilteredSnapshot(
            serializedInventory.Value, phaseRoot / "serialized-artifact-snapshot", serializedDomains);
        return RunAuditPhase(phaseId,
                             phaseLabel,
                             phaseOrdinal,
                             phaseRoot,
                             runtimeInventory.Value,
                             filteredSerialized,
                             protocolInventory.Value);
    };

    const Result<FieldAuditPhaseStamp> cookedPhase = runArtifactPhase(
        "cooked-assets",
        "CookedAssets",
        1u,
        {"scene", "prefab", "addressable", "bundle", "widget", "localization"});
    if (!cookedPhase.Ok) {
        return Result<FieldAuditRunReport>::Failure(cookedPhase.Error);
    }
    phaseStamps.push_back(cookedPhase.Value);

    const Result<FieldAuditPhaseStamp> manifestPhase =
        runArtifactPhase("build-manifests", "BuildManifests", 2u, {"build-manifest"});
    if (!manifestPhase.Ok) {
        return Result<FieldAuditRunReport>::Failure(manifestPhase.Error);
    }
    phaseStamps.push_back(manifestPhase.Value);

    const Result<FieldAuditPhaseStamp> storePhase =
        runArtifactPhase("store-bundles", "StoreBundles", 3u, {"bundle", "addressable", "build-manifest"});
    if (!storePhase.Ok) {
        return Result<FieldAuditRunReport>::Failure(storePhase.Error);
    }
    phaseStamps.push_back(storePhase.Value);

    const Result<FieldAuditPhaseStamp> dedicatedServerPhase =
        runArtifactPhase("dedicated-server-artifacts", "DedicatedServerArtifacts", 4u, {"build-manifest", "save"});
    if (!dedicatedServerPhase.Ok) {
        return Result<FieldAuditRunReport>::Failure(dedicatedServerPhase.Error);
    }
    phaseStamps.push_back(dedicatedServerPhase.Value);

    return Result<FieldAuditRunReport>::Success(BuildRunReport(request, std::move(phaseStamps)));
}

Result<FieldAuditRunReport> RunNetworkAndReplayFieldAudit(const FieldAuditRunRequest& request) {
    if (request.Scope.empty() || request.OutputDirectory.empty()) {
        return Result<FieldAuditRunReport>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (request.Scope != "network-replay") {
        return Result<FieldAuditRunReport>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    if (!EnsureOutputDirectory(request.OutputDirectory)) {
        return Result<FieldAuditRunReport>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    const Result<FieldInventorySnapshot> runtimeInventory =
        GenerateBaselineSnapshot("runtime", request.OutputDirectory / "baseline-runtime");
    if (!runtimeInventory.Ok) {
        return Result<FieldAuditRunReport>::Failure(runtimeInventory.Error);
    }

    const Result<FieldInventorySnapshot> serializedInventory =
        GenerateBaselineSnapshot("serialized", request.OutputDirectory / "baseline-serialized");
    if (!serializedInventory.Ok) {
        return Result<FieldAuditRunReport>::Failure(serializedInventory.Error);
    }

    const Result<FieldInventorySnapshot> protocolInventory =
        GenerateBaselineSnapshot("protocol", request.OutputDirectory / "baseline-protocol");
    if (!protocolInventory.Ok) {
        return Result<FieldAuditRunReport>::Failure(protocolInventory.Error);
    }

    std::vector<FieldAuditPhaseStamp> phaseStamps;
    phaseStamps.reserve(4u);

    const auto runNetworkPhase = [&](const std::string_view phaseId,
                                     const std::string_view phaseLabel,
                                     const uint32_t phaseOrdinal,
                                     const std::vector<std::string_view>& protocolDomains) -> Result<FieldAuditPhaseStamp> {
        const std::filesystem::path phaseRoot = request.OutputDirectory / ("network-phase-" + std::to_string(phaseOrdinal));
        const FieldInventorySnapshot filteredProtocol =
            BuildDomainFilteredSnapshot(protocolInventory.Value, phaseRoot / "protocol-channel-snapshot", protocolDomains);
        return RunAuditPhase(phaseId,
                             phaseLabel,
                             phaseOrdinal,
                             phaseRoot,
                             runtimeInventory.Value,
                             serializedInventory.Value,
                             filteredProtocol);
    };

    const Result<FieldAuditPhaseStamp> replicationPhase =
        runNetworkPhase("replication-parity", "ReplicationParity", 1u, {"packets"});
    if (!replicationPhase.Ok) {
        return Result<FieldAuditRunReport>::Failure(replicationPhase.Error);
    }
    phaseStamps.push_back(replicationPhase.Value);

    const Result<FieldAuditPhaseStamp> rpcPhase = runNetworkPhase("rpc-fidelity", "RpcFidelity", 2u, {"rpc"});
    if (!rpcPhase.Ok) {
        return Result<FieldAuditRunReport>::Failure(rpcPhase.Error);
    }
    phaseStamps.push_back(rpcPhase.Value);

    const Result<FieldAuditPhaseStamp> replayPhase = runNetworkPhase("replay-integrity", "ReplayIntegrity", 3u, {"replay"});
    if (!replayPhase.Ok) {
        return Result<FieldAuditRunReport>::Failure(replayPhase.Error);
    }
    phaseStamps.push_back(replayPhase.Value);

    const Result<FieldAuditPhaseStamp> rollbackPhase =
        runNetworkPhase("rollback-consistency", "RollbackConsistency", 4u, {"replay", "packets"});
    if (!rollbackPhase.Ok) {
        return Result<FieldAuditRunReport>::Failure(rollbackPhase.Error);
    }
    phaseStamps.push_back(rollbackPhase.Value);

    return Result<FieldAuditRunReport>::Success(BuildRunReport(request, std::move(phaseStamps)));
}

Result<FieldAuditRunReport> RunToolingAndAuthoringFieldAudit(const FieldAuditRunRequest& request) {
    if (request.Scope.empty() || request.OutputDirectory.empty()) {
        return Result<FieldAuditRunReport>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (request.Scope != "tooling-authoring") {
        return Result<FieldAuditRunReport>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    if (!EnsureOutputDirectory(request.OutputDirectory)) {
        return Result<FieldAuditRunReport>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    const Result<FieldInventorySnapshot> runtimeInventory =
        GenerateBaselineSnapshot("runtime", request.OutputDirectory / "baseline-runtime");
    if (!runtimeInventory.Ok) {
        return Result<FieldAuditRunReport>::Failure(runtimeInventory.Error);
    }

    const Result<FieldInventorySnapshot> serializedInventory =
        GenerateBaselineSnapshot("serialized", request.OutputDirectory / "baseline-serialized");
    if (!serializedInventory.Ok) {
        return Result<FieldAuditRunReport>::Failure(serializedInventory.Error);
    }

    const Result<FieldInventorySnapshot> protocolInventory =
        GenerateBaselineSnapshot("protocol", request.OutputDirectory / "baseline-protocol");
    if (!protocolInventory.Ok) {
        return Result<FieldAuditRunReport>::Failure(protocolInventory.Error);
    }

    std::vector<FieldAuditPhaseStamp> phaseStamps;
    phaseStamps.reserve(3u);

    const auto runToolingPhase = [&](const std::string_view phaseId,
                                     const std::string_view phaseLabel,
                                     const uint32_t phaseOrdinal,
                                     const std::vector<std::string_view>& serializedDomains,
                                     const std::vector<std::string_view>& protocolDomains) -> Result<FieldAuditPhaseStamp> {
        const std::filesystem::path phaseRoot = request.OutputDirectory / ("tooling-phase-" + std::to_string(phaseOrdinal));
        const FieldInventorySnapshot filteredSerialized = BuildDomainFilteredSnapshot(
            serializedInventory.Value, phaseRoot / "serialized-tooling-snapshot", serializedDomains);
        const FieldInventorySnapshot filteredProtocol =
            BuildDomainFilteredSnapshot(protocolInventory.Value, phaseRoot / "protocol-tooling-snapshot", protocolDomains);
        return RunAuditPhase(phaseId,
                             phaseLabel,
                             phaseOrdinal,
                             phaseRoot,
                             runtimeInventory.Value,
                             filteredSerialized,
                             filteredProtocol);
    };

    const Result<FieldAuditPhaseStamp> editorAuthoringPhase = runToolingPhase(
        "editor-authoring-schemas",
        "EditorAuthoringSchemas",
        1u,
        {"scene", "prefab", "widget", "localization"},
        {"mcp-request", "mcp-response"});
    if (!editorAuthoringPhase.Ok) {
        return Result<FieldAuditRunReport>::Failure(editorAuthoringPhase.Error);
    }
    phaseStamps.push_back(editorAuthoringPhase.Value);

    const Result<FieldAuditPhaseStamp> mcpToolPhase = runToolingPhase("mcp-tool-payloads",
                                                                       "McpToolPayloads",
                                                                       2u,
                                                                       {"widget", "build-manifest"},
                                                                       {"mcp-request", "mcp-response"});
    if (!mcpToolPhase.Ok) {
        return Result<FieldAuditRunReport>::Failure(mcpToolPhase.Error);
    }
    phaseStamps.push_back(mcpToolPhase.Value);

    const Result<FieldAuditPhaseStamp> automationReportPhase = runToolingPhase("automation-report-schemas",
                                                                                "AutomationReportSchemas",
                                                                                3u,
                                                                                {"build-manifest", "save", "addressable"},
                                                                                {"packets", "rpc", "replay"});
    if (!automationReportPhase.Ok) {
        return Result<FieldAuditRunReport>::Failure(automationReportPhase.Error);
    }
    phaseStamps.push_back(automationReportPhase.Value);

    return Result<FieldAuditRunReport>::Success(BuildRunReport(request, std::move(phaseStamps)));
}

} // namespace Core::Audit
