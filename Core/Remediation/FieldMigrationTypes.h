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
    Prefab = 1,
    UIWidget = 2,
    LocalizationTable = 3,
    AddressableCatalog = 4,
    AssetBundle = 5,
    BuildManifest = 6,
    PlayerSave = 7,
    ReplayData = 8,
    AutomationBaseline = 9
};

enum class FieldMigrationTransformKind : uint8_t {
    RequiredFieldBackfill = 0,
    StaleGraphNormalization = 1,
    BindingKeyPathNormalization = 2,
    LocalizationReferenceNormalization = 3,
    LocaleSchemaNormalization = 4,
    WidgetMetadataNormalization = 5,
    CatalogParityNormalization = 6,
    BundleParityNormalization = 7,
    BuildProfileTargetNormalization = 8,
    ManifestLinkageNormalization = 9,
    SaveCompatibilityNormalization = 10,
    ReplayDeterminismNormalization = 11,
    AutomationBaselineNormalization = 12
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
    std::string BindingKey;
    std::string ExpectedBindingKey;
    std::string BindingPath;
    std::string ExpectedBindingPath;
    std::string LocalizationKey;
    std::string ExpectedLocalizationKey;
    std::string LocalizationReference;
    std::string ExpectedLocalizationReference;
    std::string LocaleCode;
    std::string LocaleSchemaVersion;
    std::string ExpectedLocaleSchemaVersion;
    std::string WidgetPresentationMode;
    std::string WidgetMetadataSpace;
    std::string CatalogAssetKey;
    std::string ExpectedCatalogAssetKey;
    std::string CatalogAddress;
    std::string ExpectedCatalogAddress;
    std::string BundleId;
    std::string ExpectedBundleId;
    std::string BundleHash;
    std::string ExpectedBundleHash;
    std::string BundleReference;
    std::string ExpectedBundleReference;
    std::string BuildProfile;
    std::string ExpectedBuildProfile;
    std::string BuildTarget;
    std::string ExpectedBuildTarget;
    std::string ManifestBundleId;
    std::string ExpectedManifestBundleId;
    std::string ManifestCatalogKey;
    std::string ExpectedManifestCatalogKey;
    std::string SaveSchemaVersion;
    std::string ExpectedSaveSchemaVersion;
    std::string SaveCompatibilityPath;
    std::string ExpectedSaveCompatibilityPath;
    std::string ReplaySchemaVersion;
    std::string ExpectedReplaySchemaVersion;
    std::string ReplayDeterminismVersion;
    std::string ExpectedReplayDeterminismVersion;
    std::string AutomationBaselineId;
    std::string ExpectedAutomationBaselineId;
    std::string AutomationSeed;
    std::string ExpectedAutomationSeed;
    std::string AutomationDeterminismBaseline;
    std::string ExpectedAutomationDeterminismBaseline;
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
    uint32_t UIWidgetMigrationCount = 0;
    uint32_t LocalizationTableMigrationCount = 0;
    uint32_t AddressableCatalogMigrationCount = 0;
    uint32_t AssetBundleMigrationCount = 0;
    uint32_t BuildManifestMigrationCount = 0;
    uint32_t PlayerSaveMigrationCount = 0;
    uint32_t ReplayDataMigrationCount = 0;
    uint32_t AutomationBaselineMigrationCount = 0;
    uint32_t RequiredBackfillCount = 0;
    uint32_t GraphNormalizationCount = 0;
    uint32_t BindingNormalizationCount = 0;
    uint32_t LocalizationNormalizationCount = 0;
    uint32_t LocaleSchemaNormalizationCount = 0;
    uint32_t WidgetMetadataNormalizationCount = 0;
    uint32_t CatalogParityNormalizationCount = 0;
    uint32_t BundleParityNormalizationCount = 0;
    uint32_t BuildProfileTargetNormalizationCount = 0;
    uint32_t ManifestLinkageNormalizationCount = 0;
    uint32_t SaveCompatibilityNormalizationCount = 0;
    uint32_t ReplayDeterminismNormalizationCount = 0;
    uint32_t AutomationBaselineNormalizationCount = 0;
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
