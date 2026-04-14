#include "Core/Audit/FieldValidationService.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

namespace {

Core::Audit::FieldInventoryEntry BuildEntry(const std::string& domain,
                                            const std::string& ownerSubsystem,
                                            const std::string& typeName,
                                            const std::string& fieldPath,
                                            const bool required,
                                            const std::string& collectorId) {
    Core::Audit::FieldInventoryEntry entry{};
    entry.Domain = domain;
    entry.OwnerSubsystem = ownerSubsystem;
    entry.TypeName = typeName;
    entry.FieldPath = fieldPath;
    entry.Required = required;
    entry.FieldId = domain + "::" + typeName + "::" + fieldPath;
    entry.SourceTrace.SourceFile = "Core/Tests/Audit/FieldValidationTests.cpp";
    entry.SourceTrace.SourceSymbol = "BuildEntry";
    entry.SourceTrace.SourceLine = 1u;
    entry.SourceTrace.CollectorId = collectorId;
    return entry;
}

Core::Audit::FieldInventorySnapshot BuildSnapshot(const std::string& scope,
                                                  const std::filesystem::path& outputDirectory,
                                                  std::vector<Core::Audit::FieldInventoryEntry> entries) {
    Core::Audit::FieldInventorySnapshot snapshot{};
    snapshot.Scope = scope;
    snapshot.OutputDirectory = outputDirectory;
    snapshot.Entries = std::move(entries);
    snapshot.DeterministicDigest = "snapshot-digest-placeholder";
    return snapshot;
}

Core::Audit::FieldValidationRequest BuildRequest(const std::filesystem::path& outputDirectory,
                                                 Core::Audit::FieldInventorySnapshot runtimeSnapshot,
                                                 Core::Audit::FieldInventorySnapshot serializedSnapshot,
                                                 Core::Audit::FieldInventorySnapshot protocolSnapshot) {
    Core::Audit::FieldValidationRequest request{};
    request.Scope = "type-nullability";
    request.OutputDirectory = outputDirectory;
    request.RuntimeSnapshot = std::move(runtimeSnapshot);
    request.SerializedSnapshot = std::move(serializedSnapshot);
    request.ProtocolSnapshot = std::move(protocolSnapshot);
    return request;
}

bool AreFindingsSorted(const std::vector<Core::Audit::FieldValidationFinding>& findings) {
    return std::is_sorted(findings.begin(),
                          findings.end(),
                          [](const Core::Audit::FieldValidationFinding& left,
                             const Core::Audit::FieldValidationFinding& right) {
                              return std::tie(left.StableFieldKey,
                                              left.RuleId,
                                              left.DomainPair,
                                              left.LeftEvidence.FieldId,
                                              left.RightEvidence.FieldId) <
                                     std::tie(right.StableFieldKey,
                                              right.RuleId,
                                              right.DomainPair,
                                              right.LeftEvidence.FieldId,
                                              right.RightEvidence.FieldId);
                          });
}

} // namespace

int main() {
    using namespace Core::Audit;

    std::error_code errorCode;
    const std::filesystem::path root = std::filesystem::path("build") / "field-validation-tests";
    std::filesystem::remove_all(root, errorCode);

    {
        FieldValidationRequest invalidRequest{};
        const Result<FieldValidationReport> result = ValidateFieldTypeAndNullabilityContracts(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_ARGUMENT_INVALID");
    }

    {
        const FieldInventorySnapshot runtimeSnapshot = BuildSnapshot(
            "runtime",
            root / "runtime-no-mismatch",
            {BuildEntry("runtime-gameplay", "runtime-subsystem", "PlayerState", "Health", true, "runtime-collector")});
        const FieldInventorySnapshot serializedSnapshot = BuildSnapshot(
            "serialized",
            root / "serialized-no-mismatch",
            {BuildEntry("serialized-save", "serialized-subsystem", "PlayerState", "Health", true, "serialized-collector")});
        const FieldInventorySnapshot protocolSnapshot = BuildSnapshot(
            "protocol",
            root / "protocol-no-mismatch",
            {BuildEntry("protocol-rpc", "protocol-subsystem", "PlayerState", "Health", true, "protocol-collector")});

        const FieldValidationRequest request =
            BuildRequest(root / "validation-no-mismatch", runtimeSnapshot, serializedSnapshot, protocolSnapshot);
        const Result<FieldValidationReport> first = ValidateFieldTypeAndNullabilityContracts(request);
        assert(first.Ok);
        assert(first.Value.Scope == request.Scope);
        assert(first.Value.Findings.empty());
        assert(first.Value.Summary.ComparedFieldCount == 1u);
        assert(first.Value.Summary.TypeMismatchCount == 0u);
        assert(first.Value.Summary.NullabilityMismatchCount == 0u);
        assert(first.Value.Summary.TotalFindingCount == 0u);
        assert(!first.Value.DeterministicDigest.empty());

        const Result<FieldValidationReport> second = ValidateFieldTypeAndNullabilityContracts(request);
        assert(second.Ok);
        assert(second.Value.Findings.empty());
        assert(second.Value.DeterministicDigest == first.Value.DeterministicDigest);
        assert(second.Value.Summary.ComparedFieldCount == first.Value.Summary.ComparedFieldCount);
    }

    {
        const FieldInventorySnapshot runtimeSnapshot = BuildSnapshot(
            "runtime",
            root / "runtime-mismatch",
            {BuildEntry("runtime-gameplay", "runtime-subsystem", "PlayerState", "Health", true, "runtime-collector"),
             BuildEntry("runtime-score", "runtime-subsystem", "PlayerScore", "Score", true, "runtime-collector")});
        const FieldInventorySnapshot serializedSnapshot = BuildSnapshot(
            "serialized",
            root / "serialized-mismatch",
            {BuildEntry("serialized-save", "serialized-subsystem", "PlayerState", "Health", false, "serialized-collector")});
        const FieldInventorySnapshot protocolSnapshot = BuildSnapshot(
            "protocol",
            root / "protocol-mismatch",
            {BuildEntry("protocol-score", "protocol-subsystem", "ScorePayload", "Score", true, "protocol-collector")});

        const FieldValidationRequest request =
            BuildRequest(root / "validation-mismatch", runtimeSnapshot, serializedSnapshot, protocolSnapshot);
        const Result<FieldValidationReport> first = ValidateFieldTypeAndNullabilityContracts(request);
        assert(first.Ok);
        assert(first.Value.Findings.size() == 2u);
        assert(first.Value.Summary.ComparedFieldCount == 2u);
        assert(first.Value.Summary.TypeMismatchCount == 1u);
        assert(first.Value.Summary.NullabilityMismatchCount == 1u);
        assert(first.Value.Summary.TotalFindingCount == 2u);
        assert(AreFindingsSorted(first.Value.Findings));

        bool foundTypeMismatch = false;
        bool foundNullabilityMismatch = false;
        for (const FieldValidationFinding& finding : first.Value.Findings) {
            if (finding.RuleId == "FIELD_AUDIT_RULE_TYPE_CONTRACT_MISMATCH") {
                foundTypeMismatch = true;
                assert(finding.LeftEvidence.TypeName != finding.RightEvidence.TypeName);
            }
            if (finding.RuleId == "FIELD_AUDIT_RULE_NULLABILITY_CONTRACT_MISMATCH") {
                foundNullabilityMismatch = true;
                assert(finding.LeftEvidence.Required != finding.RightEvidence.Required);
            }
            assert(!finding.DomainPair.empty());
            assert(!finding.StableFieldKey.empty());
        }
        assert(foundTypeMismatch);
        assert(foundNullabilityMismatch);

        const Result<FieldValidationReport> second = ValidateFieldTypeAndNullabilityContracts(request);
        assert(second.Ok);
        assert(second.Value.Findings.size() == first.Value.Findings.size());
        assert(second.Value.DeterministicDigest == first.Value.DeterministicDigest);
        for (std::size_t index = 0; index < first.Value.Findings.size(); ++index) {
            assert(second.Value.Findings[index].RuleId == first.Value.Findings[index].RuleId);
            assert(second.Value.Findings[index].StableFieldKey == first.Value.Findings[index].StableFieldKey);
            assert(second.Value.Findings[index].DomainPair == first.Value.Findings[index].DomainPair);
        }
    }

    return 0;
}
