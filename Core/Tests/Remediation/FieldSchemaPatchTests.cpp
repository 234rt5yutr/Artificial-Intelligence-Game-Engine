#include "Core/Remediation/FieldSchemaPatchService.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

Core::Audit::FieldValidationFinding BuildFinding(const std::string& ruleId,
                                                 const Core::Audit::FieldValidationMismatchKind mismatchKind,
                                                 const std::string& stableFieldKey,
                                                 const std::string& domainPair,
                                                 const std::string& leftScope,
                                                 const std::string& leftFieldId,
                                                 const std::string& leftTypeName,
                                                 const bool leftRequired,
                                                 const std::string& rightScope,
                                                 const std::string& rightFieldId,
                                                 const std::string& rightTypeName,
                                                 const bool rightRequired) {
    Core::Audit::FieldValidationFinding finding{};
    finding.RuleId = ruleId;
    finding.MismatchKind = mismatchKind;
    finding.StableFieldKey = stableFieldKey;
    finding.DomainPair = domainPair;
    finding.LeftEvidence.SnapshotScope = leftScope;
    finding.LeftEvidence.FieldId = leftFieldId;
    finding.LeftEvidence.TypeName = leftTypeName;
    finding.LeftEvidence.Required = leftRequired;
    finding.RightEvidence.SnapshotScope = rightScope;
    finding.RightEvidence.FieldId = rightFieldId;
    finding.RightEvidence.TypeName = rightTypeName;
    finding.RightEvidence.Required = rightRequired;
    return finding;
}

Core::Remediation::FieldSchemaPatchSourceFinding BuildSourceFinding(const std::string& findingId,
                                                                    const std::string& owner,
                                                                    const Core::Audit::FieldValidationFinding& finding) {
    Core::Remediation::FieldSchemaPatchSourceFinding sourceFinding{};
    sourceFinding.FindingId = findingId;
    sourceFinding.Owner = owner;
    sourceFinding.Finding = finding;
    return sourceFinding;
}

Core::Remediation::FieldDefaultFallbackPolicyEvidence BuildDefaultFallbackEvidence(const std::string& scope,
                                                                                    const std::string& fieldId,
                                                                                    const bool hasDefaultValue,
                                                                                    const std::string& defaultValue,
                                                                                    const bool hasFallbackPath,
                                                                                    const std::string& fallbackPath) {
    Core::Remediation::FieldDefaultFallbackPolicyEvidence evidence{};
    evidence.SnapshotScope = scope;
    evidence.FieldId = fieldId;
    evidence.HasDefaultValue = hasDefaultValue;
    evidence.DefaultValue = defaultValue;
    evidence.HasFallbackPath = hasFallbackPath;
    evidence.FallbackPath = fallbackPath;
    return evidence;
}

Core::Remediation::FieldDefaultFallbackPolicyFinding BuildDefaultFallbackFinding(
    const std::string& findingId,
    const std::string& owner,
    const std::string& ruleId,
    const std::string& stableFieldKey,
    const std::string& domainPair,
    const Core::Remediation::FieldDefaultFallbackPolicyEvidence& left,
    const Core::Remediation::FieldDefaultFallbackPolicyEvidence& right) {
    Core::Remediation::FieldDefaultFallbackPolicyFinding finding{};
    finding.FindingId = findingId;
    finding.Owner = owner;
    finding.RuleId = ruleId;
    finding.StableFieldKey = stableFieldKey;
    finding.DomainPair = domainPair;
    finding.LeftEvidence = left;
    finding.RightEvidence = right;
    return finding;
}

} // namespace

int main() {
    using namespace Core::Remediation;

    std::error_code errorCode;
    const std::filesystem::path root = std::filesystem::path("build") / "field-schema-patch-tests";
    std::filesystem::remove_all(root, errorCode);

    {
        FieldSchemaPatchRequest invalidRequest{};
        const Result<FieldSchemaPatchResult> result = PatchFieldSchemaDefinitions(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_ARGUMENT_INVALID");
    }

    {
        FieldSchemaPatchRequest unsupportedScopeRequest{};
        unsupportedScopeRequest.Scope = "schema-definitions-v2";
        unsupportedScopeRequest.OutputDirectory = root / "unsupported";
        unsupportedScopeRequest.RemediationBatchId = "batch-001";
        unsupportedScopeRequest.Findings = {BuildSourceFinding(
            "finding-001",
            "runtime-systems",
            BuildFinding("FIELD_AUDIT_RULE_TYPE_CONTRACT_MISMATCH",
                         Core::Audit::FieldValidationMismatchKind::TypeMismatch,
                         "PlayerState::Health",
                         "runtime:ecs<->serialized:save",
                         "runtime",
                         "runtime::PlayerState::Health",
                         "float",
                         true,
                         "serialized",
                         "serialized::PlayerState::Health",
                         "int32",
                         true))};
        const Result<FieldSchemaPatchResult> result = PatchFieldSchemaDefinitions(unsupportedScopeRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    {
        FieldSchemaPatchRequest invalidFindingRequest{};
        invalidFindingRequest.Scope = "schema-definitions";
        invalidFindingRequest.OutputDirectory = root / "invalid-finding";
        invalidFindingRequest.RemediationBatchId = "batch-002";
        invalidFindingRequest.Findings = {BuildSourceFinding(
            "",
            "runtime-systems",
            BuildFinding("FIELD_AUDIT_RULE_TYPE_CONTRACT_MISMATCH",
                         Core::Audit::FieldValidationMismatchKind::TypeMismatch,
                         "PlayerState::Health",
                         "runtime:ecs<->serialized:save",
                         "runtime",
                         "runtime::PlayerState::Health",
                         "float",
                         true,
                         "serialized",
                         "serialized::PlayerState::Health",
                         "int32",
                         true))};

        const Result<FieldSchemaPatchResult> result = PatchFieldSchemaDefinitions(invalidFindingRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_ARGUMENT_INVALID");
    }

    {
        const Core::Audit::FieldValidationFinding typeMismatch = BuildFinding(
            "FIELD_AUDIT_RULE_TYPE_CONTRACT_MISMATCH",
            Core::Audit::FieldValidationMismatchKind::TypeMismatch,
            "PlayerState::Health",
            "runtime:ecs<->serialized:save",
            "runtime",
            "runtime::PlayerState::Health",
            "float",
            true,
            "serialized",
            "serialized::PlayerState::Health",
            "int32",
            true);

        const Core::Audit::FieldValidationFinding nullabilityMismatch = BuildFinding(
            "FIELD_AUDIT_RULE_NULLABILITY_CONTRACT_MISMATCH",
            Core::Audit::FieldValidationMismatchKind::NullabilityMismatch,
            "PlayerState::DisplayName",
            "serialized:save<->protocol:replication",
            "serialized",
            "serialized::PlayerState::DisplayName",
            "string",
            false,
            "protocol",
            "protocol::PlayerState::DisplayName",
            "string",
            true);

        FieldSchemaPatchRequest request{};
        request.Scope = "schema-definitions";
        request.OutputDirectory = root / "success";
        request.RemediationBatchId = "batch-003";
        request.Findings = {
            BuildSourceFinding("finding-b", "network-systems", nullabilityMismatch),
            BuildSourceFinding("finding-a", "runtime-systems", typeMismatch)};

        const Result<FieldSchemaPatchResult> first = PatchFieldSchemaDefinitions(request);
        assert(first.Ok);
        assert(first.Value.Scope == request.Scope);
        assert(first.Value.RemediationBatchId == request.RemediationBatchId);
        assert(first.Value.Summary.TypePatchCount == 1u);
        assert(first.Value.Summary.NullabilityPatchCount == 1u);
        assert(first.Value.Summary.RequiredPatchCount == 1u);
        assert(first.Value.Summary.TotalPatchCount == first.Value.PatchRecords.size());
        assert(first.Value.Summary.TotalPatchCount == 3u);
        assert(!first.Value.DeterministicDigest.empty());

        bool sawTypePatch = false;
        bool sawNullabilityPatch = false;
        bool sawRequiredPatch = false;
        std::string previousPatchId;
        for (const FieldSchemaPatchRecord& record : first.Value.PatchRecords) {
            assert(!record.PatchId.empty());
            assert(!record.DeterministicDigest.empty());
            assert(!record.StableFieldKey.empty());
            assert(!record.TargetFieldId.empty());
            assert(!record.PropertyPath.empty());
            assert(!record.ExistingValue.empty());
            assert(!record.ReplacementValue.empty());
            assert(!record.Provenance.FindingId.empty());
            assert(!record.Provenance.RuleId.empty());
            assert(!record.Provenance.Owner.empty());
            assert(record.Provenance.RemediationBatchId == request.RemediationBatchId);

            if (!previousPatchId.empty()) {
                assert(previousPatchId <= record.PatchId || record.Provenance.FindingId >= "finding-a");
            }
            previousPatchId = record.PatchId;

            if (record.PatchKind == FieldSchemaPatchKind::TypeNameCorrection) {
                sawTypePatch = true;
                assert(record.PropertyPath == "typeName");
                assert(record.TargetFieldId == "serialized::PlayerState::Health");
                assert(record.ExistingValue == "int32");
                assert(record.ReplacementValue == "float");
            } else if (record.PatchKind == FieldSchemaPatchKind::NullabilityFlagCorrection) {
                sawNullabilityPatch = true;
                assert(record.PropertyPath == "nullability.required");
                assert(record.TargetFieldId == "protocol::PlayerState::DisplayName");
                assert(record.ExistingValue == "true");
                assert(record.ReplacementValue == "false");
            } else if (record.PatchKind == FieldSchemaPatchKind::RequiredFlagCorrection) {
                sawRequiredPatch = true;
                assert(record.PropertyPath == "required");
                assert(record.TargetFieldId == "protocol::PlayerState::DisplayName");
                assert(record.ExistingValue == "true");
                assert(record.ReplacementValue == "false");
            }
        }
        assert(sawTypePatch);
        assert(sawNullabilityPatch);
        assert(sawRequiredPatch);

        const Result<FieldSchemaPatchResult> second = PatchFieldSchemaDefinitions(request);
        assert(second.Ok);
        assert(second.Value.Summary.TotalPatchCount == first.Value.Summary.TotalPatchCount);
        assert(second.Value.Summary.TypePatchCount == first.Value.Summary.TypePatchCount);
        assert(second.Value.Summary.NullabilityPatchCount == first.Value.Summary.NullabilityPatchCount);
        assert(second.Value.Summary.RequiredPatchCount == first.Value.Summary.RequiredPatchCount);
        assert(second.Value.DeterministicDigest == first.Value.DeterministicDigest);
        assert(second.Value.PatchRecords.size() == first.Value.PatchRecords.size());
        for (std::size_t index = 0; index < first.Value.PatchRecords.size(); ++index) {
            assert(second.Value.PatchRecords[index].PatchId == first.Value.PatchRecords[index].PatchId);
            assert(second.Value.PatchRecords[index].DeterministicDigest ==
                   first.Value.PatchRecords[index].DeterministicDigest);
            assert(second.Value.PatchRecords[index].Provenance.FindingId ==
                   first.Value.PatchRecords[index].Provenance.FindingId);
            assert(second.Value.PatchRecords[index].Provenance.RuleId == first.Value.PatchRecords[index].Provenance.RuleId);
            assert(second.Value.PatchRecords[index].Provenance.Owner == first.Value.PatchRecords[index].Provenance.Owner);
            assert(second.Value.PatchRecords[index].Provenance.RemediationBatchId ==
                   first.Value.PatchRecords[index].Provenance.RemediationBatchId);
        }
    }

    {
        FieldDefaultFallbackNormalizationRequest invalidRequest{};
        const Result<FieldDefaultFallbackNormalizationResult> result =
            NormalizeFieldDefaultAndFallbackPolicies(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_ARGUMENT_INVALID");
    }

    {
        FieldDefaultFallbackNormalizationRequest request{};
        request.Scope = "schema-definitions";
        request.OutputDirectory = root / "default-fallback-success";
        request.RemediationBatchId = "batch-004";
        request.Findings = {
            BuildDefaultFallbackFinding(
                "finding-b",
                "save-systems",
                "FIELD_AUDIT_RULE_DEFAULT_FALLBACK_POLICY_MISMATCH",
                "PlayerState::DisplayName",
                "runtime:ecs<->serialized:save",
                BuildDefaultFallbackEvidence("runtime",
                                             "runtime::PlayerState::DisplayName",
                                             true,
                                             "Unknown",
                                             true,
                                             "/ui/defaults/player-display-name"),
                BuildDefaultFallbackEvidence("serialized",
                                             "serialized::PlayerState::DisplayName",
                                             false,
                                             "",
                                             true,
                                             "/ui/defaults/player-name")),
            BuildDefaultFallbackFinding(
                "finding-a",
                "network-systems",
                "FIELD_AUDIT_RULE_DEFAULT_FALLBACK_POLICY_MISMATCH",
                "WeaponState::DamageType",
                "serialized:save<->protocol:replication",
                BuildDefaultFallbackEvidence("serialized",
                                             "serialized::WeaponState::DamageType",
                                             true,
                                             "Physical",
                                             true,
                                             "/combat/defaults/damage-type"),
                BuildDefaultFallbackEvidence("protocol",
                                             "protocol::WeaponState::DamageType",
                                             true,
                                             "Arcane",
                                             true,
                                             "/combat/defaults/weapon-damage"))};

        const Result<FieldDefaultFallbackNormalizationResult> first =
            NormalizeFieldDefaultAndFallbackPolicies(request);
        assert(first.Ok);
        assert(first.Value.Scope == request.Scope);
        assert(first.Value.RemediationBatchId == request.RemediationBatchId);
        assert(first.Value.Summary.DefaultPolicyNormalizationCount == 2u);
        assert(first.Value.Summary.FallbackPolicyNormalizationCount == 2u);
        assert(first.Value.Summary.TotalNormalizationCount == 4u);
        assert(first.Value.NormalizationRecords.size() == 4u);
        assert(!first.Value.DeterministicDigest.empty());

        std::string previousNormalizationId;
        bool sawDefaultToUnsetRepair = false;
        bool sawFallbackRepair = false;
        for (const FieldDefaultFallbackNormalizationRecord& record : first.Value.NormalizationRecords) {
            assert(!record.NormalizationId.empty());
            assert(!record.StableFieldKey.empty());
            assert(!record.TargetFieldId.empty());
            assert(!record.PropertyPath.empty());
            assert(!record.NormalizedValue.empty());
            assert(!record.Rationale.empty());
            assert(!record.DeterministicDigest.empty());
            assert(record.Provenance.RemediationBatchId == request.RemediationBatchId);
            if (!previousNormalizationId.empty()) {
                assert(previousNormalizationId <= record.NormalizationId || record.Provenance.FindingId >= "finding-a");
            }
            previousNormalizationId = record.NormalizationId;

            if (record.PropertyPath == "defaultValue" &&
                record.TargetFieldId == "serialized::PlayerState::DisplayName") {
                sawDefaultToUnsetRepair = true;
                assert(record.ExistingValue == "<unset>");
                assert(record.NormalizedValue == "Unknown");
                assert(record.NormalizationKind == FieldDefaultFallbackNormalizationKind::DefaultValuePolicy);
            }

            if (record.PropertyPath == "fallbackPath" &&
                record.TargetFieldId == "protocol::WeaponState::DamageType") {
                sawFallbackRepair = true;
                assert(record.ExistingValue == "/combat/defaults/weapon-damage");
                assert(record.NormalizedValue == "/combat/defaults/damage-type");
                assert(record.NormalizationKind == FieldDefaultFallbackNormalizationKind::FallbackPathPolicy);
            }
        }
        assert(sawDefaultToUnsetRepair);
        assert(sawFallbackRepair);

        const Result<FieldDefaultFallbackNormalizationResult> second =
            NormalizeFieldDefaultAndFallbackPolicies(request);
        assert(second.Ok);
        assert(second.Value.DeterministicDigest == first.Value.DeterministicDigest);
        assert(second.Value.Summary.TotalNormalizationCount == first.Value.Summary.TotalNormalizationCount);
        assert(second.Value.NormalizationRecords.size() == first.Value.NormalizationRecords.size());
        for (std::size_t index = 0; index < first.Value.NormalizationRecords.size(); ++index) {
            assert(second.Value.NormalizationRecords[index].NormalizationId ==
                   first.Value.NormalizationRecords[index].NormalizationId);
            assert(second.Value.NormalizationRecords[index].DeterministicDigest ==
                   first.Value.NormalizationRecords[index].DeterministicDigest);
            assert(second.Value.NormalizationRecords[index].Provenance.FindingId ==
                   first.Value.NormalizationRecords[index].Provenance.FindingId);
            assert(second.Value.NormalizationRecords[index].Provenance.RuleId ==
                   first.Value.NormalizationRecords[index].Provenance.RuleId);
            assert(second.Value.NormalizationRecords[index].Provenance.Owner ==
                   first.Value.NormalizationRecords[index].Provenance.Owner);
            assert(second.Value.NormalizationRecords[index].Provenance.RemediationBatchId ==
                   first.Value.NormalizationRecords[index].Provenance.RemediationBatchId);
        }
    }

    return 0;
}
