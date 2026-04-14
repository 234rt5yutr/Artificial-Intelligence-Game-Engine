#pragma once

#include "Core/Asset/Addressables/AddressablesCatalogTypes.h"
#include "Core/Audit/FieldAuditTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Core::Remediation {

template <typename T>
using Result = Core::Asset::Addressables::Result<T>;

enum class FieldSchemaPatchKind : uint8_t {
    TypeNameCorrection = 0,
    NullabilityFlagCorrection = 1,
    RequiredFlagCorrection = 2
};

enum class FieldDefaultFallbackNormalizationKind : uint8_t {
    DefaultValuePolicy = 0,
    FallbackPathPolicy = 1
};

enum class FieldSerializationMappingFixKind : uint8_t {
    SerializedNameCorrection = 0,
    AliasSetCorrection = 1,
    SerializedPathCorrection = 2
};

struct FieldSchemaPatchSourceFinding {
    std::string FindingId;
    std::string Owner;
    Core::Audit::FieldValidationFinding Finding;
};

struct FieldSchemaPatchRequest {
    std::string Scope = "schema-definitions";
    std::filesystem::path OutputDirectory;
    std::string RemediationBatchId;
    std::vector<FieldSchemaPatchSourceFinding> Findings;
};

struct FieldSchemaPatchProvenanceMetadata {
    std::string FindingId;
    std::string RuleId;
    std::string Owner;
    std::string RemediationBatchId;
};

struct FieldSchemaPatchRecord {
    std::string PatchId;
    FieldSchemaPatchKind PatchKind = FieldSchemaPatchKind::TypeNameCorrection;
    std::string StableFieldKey;
    std::string DomainPair;
    std::string TargetFieldId;
    std::string PropertyPath;
    std::string ExistingValue;
    std::string ReplacementValue;
    FieldSchemaPatchProvenanceMetadata Provenance;
    std::string DeterministicDigest;
};

struct FieldSchemaPatchSummary {
    uint32_t TypePatchCount = 0;
    uint32_t NullabilityPatchCount = 0;
    uint32_t RequiredPatchCount = 0;
    uint32_t TotalPatchCount = 0;
};

struct FieldSchemaPatchResult {
    std::string Scope;
    std::filesystem::path OutputDirectory;
    std::string RemediationBatchId;
    std::vector<FieldSchemaPatchRecord> PatchRecords;
    FieldSchemaPatchSummary Summary;
    std::string DeterministicDigest;
};

struct FieldDefaultFallbackPolicyEvidence {
    std::string SnapshotScope;
    std::string FieldId;
    bool HasDefaultValue = false;
    std::string DefaultValue;
    bool HasFallbackPath = false;
    std::string FallbackPath;
};

struct FieldDefaultFallbackPolicyFinding {
    std::string FindingId;
    std::string Owner;
    std::string RuleId;
    std::string StableFieldKey;
    std::string DomainPair;
    FieldDefaultFallbackPolicyEvidence LeftEvidence;
    FieldDefaultFallbackPolicyEvidence RightEvidence;
};

struct FieldDefaultFallbackNormalizationRequest {
    std::string Scope = "schema-definitions";
    std::filesystem::path OutputDirectory;
    std::string RemediationBatchId;
    std::vector<FieldDefaultFallbackPolicyFinding> Findings;
};

struct FieldDefaultFallbackNormalizationRecord {
    std::string NormalizationId;
    FieldDefaultFallbackNormalizationKind NormalizationKind = FieldDefaultFallbackNormalizationKind::DefaultValuePolicy;
    std::string StableFieldKey;
    std::string DomainPair;
    std::string TargetFieldId;
    std::string PropertyPath;
    std::string ExistingValue;
    std::string NormalizedValue;
    std::string Rationale;
    FieldSchemaPatchProvenanceMetadata Provenance;
    std::string DeterministicDigest;
};

struct FieldDefaultFallbackNormalizationSummary {
    uint32_t DefaultPolicyNormalizationCount = 0;
    uint32_t FallbackPolicyNormalizationCount = 0;
    uint32_t TotalNormalizationCount = 0;
};

struct FieldDefaultFallbackNormalizationResult {
    std::string Scope;
    std::filesystem::path OutputDirectory;
    std::string RemediationBatchId;
    std::vector<FieldDefaultFallbackNormalizationRecord> NormalizationRecords;
    FieldDefaultFallbackNormalizationSummary Summary;
    std::string DeterministicDigest;
};

struct FieldSerializationMappingEvidence {
    std::string SnapshotScope;
    std::string FieldId;
    std::string SerializedName;
    std::vector<std::string> AliasNames;
    std::string SerializedPath;
};

struct FieldSerializationMappingFinding {
    std::string FindingId;
    std::string Owner;
    std::string RuleId;
    std::string StableFieldKey;
    std::string DomainPair;
    FieldSerializationMappingEvidence RuntimeEvidence;
    FieldSerializationMappingEvidence EditorEvidence;
    FieldSerializationMappingEvidence CookedEvidence;
};

struct FieldSerializationMappingFixRequest {
    std::string Scope = "schema-definitions";
    std::filesystem::path OutputDirectory;
    std::string RemediationBatchId;
    std::vector<FieldSerializationMappingFinding> Findings;
};

struct FieldSerializationMappingFixRecord {
    std::string FixId;
    FieldSerializationMappingFixKind FixKind = FieldSerializationMappingFixKind::SerializedNameCorrection;
    std::string StableFieldKey;
    std::string DomainPair;
    std::string TargetFieldId;
    std::string PropertyPath;
    std::string ExistingValue;
    std::string ReplacementValue;
    std::string Rationale;
    FieldSchemaPatchProvenanceMetadata Provenance;
    std::string DeterministicDigest;
};

struct FieldSerializationMappingFixSummary {
    uint32_t SerializedNameFixCount = 0;
    uint32_t AliasSetFixCount = 0;
    uint32_t SerializedPathFixCount = 0;
    uint32_t TotalFixCount = 0;
};

struct FieldSerializationMappingFixResult {
    std::string Scope;
    std::filesystem::path OutputDirectory;
    std::string RemediationBatchId;
    std::vector<FieldSerializationMappingFixRecord> FixRecords;
    FieldSerializationMappingFixSummary Summary;
    std::string DeterministicDigest;
};

} // namespace Core::Remediation
