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

} // namespace Core::Remediation
