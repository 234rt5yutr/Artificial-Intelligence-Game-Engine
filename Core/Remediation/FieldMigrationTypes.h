#pragma once

#include "Core/Asset/Addressables/AddressablesCatalogTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Core::Remediation {

template <typename T>
using Result = Core::Asset::Addressables::Result<T>;

enum class FieldMigrationAssetKind : uint8_t {
    Scene = 0,
    Prefab = 1
};

enum class FieldMigrationTransformKind : uint8_t {
    RequiredFieldBackfill = 0,
    StaleGraphNormalization = 1
};

struct FieldMigrationProvenanceMetadata {
    std::string FindingId;
    std::string RuleId;
    std::string Owner;
    std::string RemediationBatchId;
};

struct FieldMigrationRollbackCheckpointMetadata {
    std::string RollbackCheckpointId;
    std::string RollbackPropertyPath;
    std::string RollbackValue;
    bool RollbackRequired = true;
    std::string RollbackManifestPath;
};

struct FieldMigrationEntry {
    std::string AssetId;
    FieldMigrationAssetKind AssetKind = FieldMigrationAssetKind::Scene;
    std::string StableFieldKey;
    std::string DomainPair;
    std::string FieldId;
    std::string FieldValue;
    bool Required = false;
    std::string RequiredValue;
    std::vector<std::string> GraphNodeIds;
    std::vector<std::string> GraphEdgeTargets;
    FieldMigrationProvenanceMetadata Provenance;
};

struct FieldMigrationRequest {
    std::string Scope = "scene-prefab-data";
    std::filesystem::path OutputDirectory;
    std::string RemediationBatchId;
    std::filesystem::path RollbackManifestPath;
    std::vector<FieldMigrationEntry> Entries;
};

struct FieldMigrationRecord {
    std::string MigrationId;
    FieldMigrationAssetKind AssetKind = FieldMigrationAssetKind::Scene;
    FieldMigrationTransformKind TransformKind = FieldMigrationTransformKind::RequiredFieldBackfill;
    std::string AssetId;
    std::string StableFieldKey;
    std::string DomainPair;
    std::string TargetFieldId;
    std::string PropertyPath;
    std::string ExistingValue;
    std::string ReplacementValue;
    std::string GraphNormalizationAction;
    FieldMigrationProvenanceMetadata Provenance;
    FieldMigrationRollbackCheckpointMetadata Rollback;
    std::string DeterministicDigest;
};

struct FieldMigrationSummary {
    uint32_t SceneMigrationCount = 0;
    uint32_t PrefabMigrationCount = 0;
    uint32_t RequiredBackfillCount = 0;
    uint32_t GraphNormalizationCount = 0;
    uint32_t RollbackSafeMigrationCount = 0;
    uint32_t TotalMigrationCount = 0;
};

struct FieldMigrationResult {
    std::string Scope;
    std::filesystem::path OutputDirectory;
    std::string RemediationBatchId;
    std::filesystem::path RollbackManifestPath;
    std::vector<FieldMigrationRecord> MigrationRecords;
    FieldMigrationSummary Summary;
    std::string RollbackManifestDigest;
    std::string DeterministicDigest;
};

} // namespace Core::Remediation
