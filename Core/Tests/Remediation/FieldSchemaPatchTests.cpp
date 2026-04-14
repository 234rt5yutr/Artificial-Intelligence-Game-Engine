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

Core::Remediation::FieldSerializationMappingEvidence BuildSerializationMappingEvidence(
    const std::string& scope,
    const std::string& fieldId,
    const std::string& serializedName,
    const std::vector<std::string>& aliasNames,
    const std::string& serializedPath) {
    Core::Remediation::FieldSerializationMappingEvidence evidence{};
    evidence.SnapshotScope = scope;
    evidence.FieldId = fieldId;
    evidence.SerializedName = serializedName;
    evidence.AliasNames = aliasNames;
    evidence.SerializedPath = serializedPath;
    return evidence;
}

Core::Remediation::FieldSerializationMappingFinding BuildSerializationMappingFinding(
    const std::string& findingId,
    const std::string& owner,
    const std::string& ruleId,
    const std::string& stableFieldKey,
    const std::string& domainPair,
    const Core::Remediation::FieldSerializationMappingEvidence& runtime,
    const Core::Remediation::FieldSerializationMappingEvidence& editor,
    const Core::Remediation::FieldSerializationMappingEvidence& cooked) {
    Core::Remediation::FieldSerializationMappingFinding finding{};
    finding.FindingId = findingId;
    finding.Owner = owner;
    finding.RuleId = ruleId;
    finding.StableFieldKey = stableFieldKey;
    finding.DomainPair = domainPair;
    finding.RuntimeEvidence = runtime;
    finding.EditorEvidence = editor;
    finding.CookedEvidence = cooked;
    return finding;
}

Core::Remediation::FieldSchemaMigrationEvidence BuildSchemaMigrationEvidence(const std::string& scope,
                                                                             const std::string& fieldId,
                                                                             const std::string& typeName,
                                                                             const bool required,
                                                                             const uint32_t schemaVersion,
                                                                             const std::vector<std::string>& aliases) {
    Core::Remediation::FieldSchemaMigrationEvidence evidence{};
    evidence.SnapshotScope = scope;
    evidence.FieldId = fieldId;
    evidence.TypeName = typeName;
    evidence.Required = required;
    evidence.SchemaVersion = schemaVersion;
    evidence.CompatibilityAliases = aliases;
    return evidence;
}

Core::Remediation::FieldSchemaMigrationFinding BuildSchemaMigrationFinding(
    const std::string& findingId,
    const std::string& owner,
    const std::string& ruleId,
    const std::string& stableFieldKey,
    const std::string& domainPair,
    const Core::Remediation::FieldSchemaMigrationEvidence& source,
    const Core::Remediation::FieldSchemaMigrationEvidence& target) {
    Core::Remediation::FieldSchemaMigrationFinding finding{};
    finding.FindingId = findingId;
    finding.Owner = owner;
    finding.RuleId = ruleId;
    finding.StableFieldKey = stableFieldKey;
    finding.DomainPair = domainPair;
    finding.SourceEvidence = source;
    finding.TargetEvidence = target;
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
        FieldSerializationMappingFixRequest invalidRequest{};
        const Result<FieldSerializationMappingFixResult> result = FixFieldSerializationMappings(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_ARGUMENT_INVALID");
    }

    {
        FieldSerializationMappingFixRequest request{};
        request.Scope = "schema-definitions";
        request.OutputDirectory = root / "serialization-mapping-success";
        request.RemediationBatchId = "batch-005";
        request.Findings = {
            BuildSerializationMappingFinding(
                "finding-b",
                "serialization-systems",
                "FIELD_AUDIT_RULE_SERIALIZATION_MAPPING_MISMATCH",
                "PlayerState::Health",
                "runtime<->editor<->cooked",
                BuildSerializationMappingEvidence("runtime",
                                                  "runtime::PlayerState::Health",
                                                  "playerHealth",
                                                  {"health", "hp"},
                                                  "/player/state/health"),
                BuildSerializationMappingEvidence("editor",
                                                  "editor::PlayerState::Health",
                                                  "PlayerHealth",
                                                  {"Health", "hp"},
                                                  "/editor/player/state/health"),
                BuildSerializationMappingEvidence("cooked",
                                                  "cooked::PlayerState::Health",
                                                  "health_h",
                                                  {"hp"},
                                                  "/cooked/ps/h")),
            BuildSerializationMappingFinding(
                "finding-a",
                "serialization-systems",
                "FIELD_AUDIT_RULE_SERIALIZATION_MAPPING_MISMATCH",
                "WeaponState::DamageType",
                "runtime<->editor<->cooked",
                BuildSerializationMappingEvidence("runtime",
                                                  "runtime::WeaponState::DamageType",
                                                  "damageType",
                                                  {"damage_type", "type"},
                                                  "/weapon/state/damage-type"),
                BuildSerializationMappingEvidence("editor",
                                                  "editor::WeaponState::DamageType",
                                                  "damageType",
                                                  {"type", "damage_type"},
                                                  "/editor/weapon/state/damage-type"),
                BuildSerializationMappingEvidence("cooked",
                                                  "cooked::WeaponState::DamageType",
                                                  "dmg_type",
                                                  {"type"},
                                                  "/cooked/weapon/state/dmg"))};

        const Result<FieldSerializationMappingFixResult> first = FixFieldSerializationMappings(request);
        assert(first.Ok);
        assert(first.Value.Scope == request.Scope);
        assert(first.Value.RemediationBatchId == request.RemediationBatchId);
        assert(first.Value.Summary.SerializedNameFixCount == 3u);
        assert(first.Value.Summary.AliasSetFixCount == 3u);
        assert(first.Value.Summary.SerializedPathFixCount == 4u);
        assert(first.Value.Summary.TotalFixCount == 10u);
        assert(first.Value.FixRecords.size() == first.Value.Summary.TotalFixCount);
        assert(!first.Value.DeterministicDigest.empty());

        bool sawEditorNameFix = false;
        bool sawCookedAliasFix = false;
        bool sawCookedPathFix = false;
        std::string previousFixId;
        for (const FieldSerializationMappingFixRecord& record : first.Value.FixRecords) {
            assert(!record.FixId.empty());
            assert(!record.DeterministicDigest.empty());
            assert(!record.StableFieldKey.empty());
            assert(!record.TargetFieldId.empty());
            assert(!record.PropertyPath.empty());
            assert(!record.ExistingValue.empty());
            assert(!record.ReplacementValue.empty());
            assert(!record.Rationale.empty());
            assert(record.Provenance.RemediationBatchId == request.RemediationBatchId);
            if (!previousFixId.empty()) {
                assert(previousFixId <= record.FixId || record.Provenance.FindingId >= "finding-a");
            }
            previousFixId = record.FixId;

            if (record.PropertyPath == "serialization.name" &&
                record.TargetFieldId == "editor::PlayerState::Health") {
                sawEditorNameFix = true;
                assert(record.ExistingValue == "PlayerHealth");
                assert(record.ReplacementValue == "playerHealth");
                assert(record.FixKind == FieldSerializationMappingFixKind::SerializedNameCorrection);
            }

            if (record.PropertyPath == "serialization.aliases" &&
                record.TargetFieldId == "cooked::WeaponState::DamageType") {
                sawCookedAliasFix = true;
                assert(record.ExistingValue == "type");
                assert(record.ReplacementValue == "damage_type,type");
                assert(record.FixKind == FieldSerializationMappingFixKind::AliasSetCorrection);
            }

            if (record.PropertyPath == "serialization.path" &&
                record.TargetFieldId == "cooked::PlayerState::Health") {
                sawCookedPathFix = true;
                assert(record.ExistingValue == "/cooked/ps/h");
                assert(record.ReplacementValue == "/player/state/health");
                assert(record.FixKind == FieldSerializationMappingFixKind::SerializedPathCorrection);
            }
        }
        assert(sawEditorNameFix);
        assert(sawCookedAliasFix);
        assert(sawCookedPathFix);

        const Result<FieldSerializationMappingFixResult> second = FixFieldSerializationMappings(request);
        assert(second.Ok);
        assert(second.Value.DeterministicDigest == first.Value.DeterministicDigest);
        assert(second.Value.Summary.TotalFixCount == first.Value.Summary.TotalFixCount);
        assert(second.Value.FixRecords.size() == first.Value.FixRecords.size());
        for (std::size_t index = 0; index < first.Value.FixRecords.size(); ++index) {
            assert(second.Value.FixRecords[index].FixId == first.Value.FixRecords[index].FixId);
            assert(second.Value.FixRecords[index].DeterministicDigest == first.Value.FixRecords[index].DeterministicDigest);
            assert(second.Value.FixRecords[index].Provenance.FindingId ==
                   first.Value.FixRecords[index].Provenance.FindingId);
            assert(second.Value.FixRecords[index].Provenance.RuleId == first.Value.FixRecords[index].Provenance.RuleId);
            assert(second.Value.FixRecords[index].Provenance.Owner == first.Value.FixRecords[index].Provenance.Owner);
            assert(second.Value.FixRecords[index].Provenance.RemediationBatchId ==
                   first.Value.FixRecords[index].Provenance.RemediationBatchId);
        }
    }

    {
        FieldSchemaMigrationRequest invalidRequest{};
        const Result<FieldSchemaMigrationResult> result = VersionAndApplyFieldSchemaMigrations(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_ARGUMENT_INVALID");
    }

    {
        FieldSchemaMigrationRequest unsupportedScopeRequest{};
        unsupportedScopeRequest.Scope = "schema-definitions-v2";
        unsupportedScopeRequest.OutputDirectory = root / "migration-unsupported";
        unsupportedScopeRequest.RemediationBatchId = "batch-006";
        unsupportedScopeRequest.SourceSchemaVersion = 4u;
        unsupportedScopeRequest.TargetSchemaVersion = 5u;
        unsupportedScopeRequest.CompatibilityWindow.MinimumReadableVersion = 4u;
        unsupportedScopeRequest.CompatibilityWindow.MaximumReadableVersion = 5u;
        unsupportedScopeRequest.Findings = {BuildSchemaMigrationFinding(
            "migration-finding-001",
            "runtime-systems",
            "FIELD_AUDIT_RULE_MIGRATION_WINDOW_MISMATCH",
            "PlayerState::Health",
            "runtime<->serialized",
            BuildSchemaMigrationEvidence("runtime",
                                         "runtime::PlayerState::Health",
                                         "float",
                                         true,
                                         4u,
                                         {"serialized::PlayerState::Health"}),
            BuildSchemaMigrationEvidence("serialized",
                                         "serialized::PlayerState::Health",
                                         "double",
                                         true,
                                         5u,
                                         {"runtime::PlayerState::Health"}))};

        const Result<FieldSchemaMigrationResult> result =
            VersionAndApplyFieldSchemaMigrations(unsupportedScopeRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    {
        FieldSchemaMigrationRequest request{};
        request.Scope = "schema-definitions";
        request.OutputDirectory = root / "migration-window-failure";
        request.RemediationBatchId = "batch-007";
        request.SourceSchemaVersion = 4u;
        request.TargetSchemaVersion = 6u;
        request.CompatibilityWindow.MinimumReadableVersion = 5u;
        request.CompatibilityWindow.MaximumReadableVersion = 6u;
        request.Findings = {BuildSchemaMigrationFinding(
            "migration-finding-002",
            "save-systems",
            "FIELD_AUDIT_RULE_MIGRATION_WINDOW_MISMATCH",
            "PlayerState::DisplayName",
            "runtime<->serialized",
            BuildSchemaMigrationEvidence("runtime", "runtime::PlayerState::DisplayName", "string", false, 4u, {}),
            BuildSchemaMigrationEvidence("serialized",
                                         "serialized::PlayerState::DisplayName",
                                         "string",
                                         true,
                                         6u,
                                         {"runtime::PlayerState::DisplayName"}))};

        const Result<FieldSchemaMigrationResult> result = VersionAndApplyFieldSchemaMigrations(request);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_COMPATIBILITY_WINDOW_UNSUPPORTED");
    }

    {
        FieldSchemaMigrationRequest request{};
        request.Scope = "schema-definitions";
        request.OutputDirectory = root / "migration-success";
        request.RemediationBatchId = "batch-008";
        request.SourceSchemaVersion = 4u;
        request.TargetSchemaVersion = 5u;
        request.CompatibilityWindow.MinimumReadableVersion = 4u;
        request.CompatibilityWindow.MaximumReadableVersion = 5u;
        request.Findings = {
            BuildSchemaMigrationFinding(
                "migration-finding-b",
                "runtime-systems",
                "FIELD_AUDIT_RULE_MIGRATION_WINDOW_MISMATCH",
                "PlayerState::Health",
                "runtime<->serialized",
                BuildSchemaMigrationEvidence("runtime", "runtime::PlayerState::Health", "float", true, 4u, {}),
                BuildSchemaMigrationEvidence("serialized",
                                             "serialized::PlayerState::HealthV2",
                                             "double",
                                             false,
                                             5u,
                                             {"runtime::PlayerState::Health"})),
            BuildSchemaMigrationFinding(
                "migration-finding-a",
                "ui-systems",
                "FIELD_AUDIT_RULE_MIGRATION_WINDOW_MISMATCH",
                "PlayerState::DisplayName",
                "runtime<->serialized",
                BuildSchemaMigrationEvidence("runtime", "runtime::PlayerState::DisplayName", "string", false, 4u, {}),
                BuildSchemaMigrationEvidence("serialized",
                                             "serialized::PlayerState::DisplayName",
                                             "string",
                                             true,
                                             5u,
                                             {"runtime::PlayerState::DisplayName"}))};

        const Result<FieldSchemaMigrationResult> first = VersionAndApplyFieldSchemaMigrations(request);
        assert(first.Ok);
        assert(first.Value.Scope == request.Scope);
        assert(first.Value.RemediationBatchId == request.RemediationBatchId);
        assert(first.Value.SourceSchemaVersion == request.SourceSchemaVersion);
        assert(first.Value.TargetSchemaVersion == request.TargetSchemaVersion);
        assert(first.Value.CompatibilityWindow.MinimumReadableVersion == request.CompatibilityWindow.MinimumReadableVersion);
        assert(first.Value.CompatibilityWindow.MaximumReadableVersion == request.CompatibilityWindow.MaximumReadableVersion);
        assert(first.Value.Summary.TotalMigrationCount == first.Value.MigrationRecords.size());
        assert(first.Value.Summary.TotalMigrationCount == 4u);
        assert(first.Value.Summary.RollbackSafeMigrationCount == first.Value.Summary.TotalMigrationCount);
        assert(!first.Value.RollbackManifestDigest.empty());
        assert(!first.Value.DeterministicDigest.empty());

        bool sawTypeMigration = false;
        bool sawRequiredMigration = false;
        bool sawAliasMigration = false;
        for (const FieldSchemaMigrationRecord& record : first.Value.MigrationRecords) {
            assert(!record.MigrationId.empty());
            assert(!record.DeterministicDigest.empty());
            assert(!record.PropertyPath.empty());
            assert(!record.ExistingValue.empty());
            assert(!record.ReplacementValue.empty());
            assert(!record.ForwardTransform.empty());
            assert(!record.RollbackTransform.empty());
            assert(record.Rollback.RollbackRequired);
            assert(!record.Rollback.RollbackCheckpointId.empty());
            assert(!record.Rollback.RollbackPropertyPath.empty());
            assert(!record.Rollback.RollbackValue.empty());
            assert(record.CompatibilityWindow.MinimumReadableVersion ==
                   request.CompatibilityWindow.MinimumReadableVersion);
            assert(record.CompatibilityWindow.MaximumReadableVersion ==
                   request.CompatibilityWindow.MaximumReadableVersion);
            assert(record.Provenance.RemediationBatchId == request.RemediationBatchId);

            if (record.TransformKind == FieldSchemaMigrationTransformKind::TypeNameMigration) {
                sawTypeMigration = true;
            } else if (record.TransformKind == FieldSchemaMigrationTransformKind::RequiredFlagMigration) {
                sawRequiredMigration = true;
            } else if (record.TransformKind == FieldSchemaMigrationTransformKind::CompatibilityAliasBackfill) {
                sawAliasMigration = true;
            }
        }
        assert(sawTypeMigration);
        assert(sawRequiredMigration);
        assert(sawAliasMigration);

        const Result<FieldSchemaMigrationResult> second = VersionAndApplyFieldSchemaMigrations(request);
        assert(second.Ok);
        assert(second.Value.DeterministicDigest == first.Value.DeterministicDigest);
        assert(second.Value.RollbackManifestDigest == first.Value.RollbackManifestDigest);
        assert(second.Value.MigrationRecords.size() == first.Value.MigrationRecords.size());
        for (std::size_t index = 0; index < first.Value.MigrationRecords.size(); ++index) {
            assert(second.Value.MigrationRecords[index].MigrationId == first.Value.MigrationRecords[index].MigrationId);
            assert(second.Value.MigrationRecords[index].DeterministicDigest ==
                   first.Value.MigrationRecords[index].DeterministicDigest);
            assert(second.Value.MigrationRecords[index].Rollback.RollbackCheckpointId ==
                   first.Value.MigrationRecords[index].Rollback.RollbackCheckpointId);
        }
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
