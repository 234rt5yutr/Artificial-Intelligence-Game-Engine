#include "Core/Remediation/FieldMigrationService.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

Core::Remediation::FieldMigrationEntry BuildEntry(const std::string& assetId,
                                                  const Core::Remediation::FieldMigrationAssetKind assetKind,
                                                  const std::string& stableFieldKey,
                                                  const std::string& domainPair,
                                                  const std::string& fieldId,
                                                  const std::string& fieldValue,
                                                  const bool required,
                                                  const std::string& requiredValue,
                                                  const std::vector<std::string>& nodeIds,
                                                  const std::vector<std::string>& edgeTargets,
                                                  const std::string& findingId,
                                                  const std::string& owner) {
    Core::Remediation::FieldMigrationEntry entry{};
    entry.AssetId = assetId;
    entry.AssetKind = assetKind;
    entry.StableFieldKey = stableFieldKey;
    entry.DomainPair = domainPair;
    entry.FieldId = fieldId;
    entry.FieldValue = fieldValue;
    entry.Required = required;
    entry.RequiredValue = requiredValue;
    entry.GraphNodeIds = nodeIds;
    entry.GraphEdgeTargets = edgeTargets;
    entry.Provenance.FindingId = findingId;
    entry.Provenance.RuleId = "FIELD_AUDIT_RULE_REQUIRED_FIELD_MISSING";
    entry.Provenance.Owner = owner;
    return entry;
}

Core::Remediation::FieldMigrationEntry BuildUIEntry(
    const std::string& assetId,
    const Core::Remediation::FieldMigrationAssetKind assetKind,
    const std::string& fieldId,
    const std::string& findingId,
    const std::string& owner) {
    Core::Remediation::FieldMigrationEntry entry{};
    entry.AssetId = assetId;
    entry.AssetKind = assetKind;
    entry.StableFieldKey = "UI::Binding";
    entry.DomainPair = "ui<->runtime";
    entry.FieldId = fieldId;
    entry.FieldValue = "configured";
    entry.Provenance.FindingId = findingId;
    entry.Provenance.RuleId = "FIELD_AUDIT_RULE_UI_LOCALIZATION_DRIFT";
    entry.Provenance.Owner = owner;
    return entry;
}

Core::Remediation::FieldMigrationEntry BuildAddressableEntry(
    const std::string& assetId,
    const Core::Remediation::FieldMigrationAssetKind assetKind,
    const std::string& fieldId,
    const std::string& findingId,
    const std::string& owner) {
    Core::Remediation::FieldMigrationEntry entry{};
    entry.AssetId = assetId;
    entry.AssetKind = assetKind;
    entry.StableFieldKey = "Addressable::Parity";
    entry.DomainPair = "catalog<->bundle<->build";
    entry.FieldId = fieldId;
    entry.FieldValue = "configured";
    entry.Provenance.FindingId = findingId;
    entry.Provenance.RuleId = "FIELD_AUDIT_RULE_ADDRESSABLE_BUNDLE_BUILD_DRIFT";
    entry.Provenance.Owner = owner;
    return entry;
}

} // namespace

#define REQUIRE_OR_FAIL(condition) \
    do {                           \
        if (!(condition)) {        \
            return __LINE__;       \
        }                          \
    } while (false)

int main() {
    using namespace Core::Remediation;

    std::error_code errorCode;
    const std::filesystem::path root = std::filesystem::path("build") / "field-migration-tests";
    std::filesystem::remove_all(root, errorCode);

    {
        FieldMigrationRequest invalidRequest{};
        const Result<FieldMigrationResult> result = MigrateSceneAndPrefabFieldData(invalidRequest);
        REQUIRE_OR_FAIL(!result.Ok);
        REQUIRE_OR_FAIL(result.Error == "FIELD_AUDIT_ARGUMENT_INVALID");
    }

    {
        FieldMigrationRequest invalidRequest{};
        const Result<FieldMigrationResult> result = MigrateUIAndLocalizationFieldData(invalidRequest);
        REQUIRE_OR_FAIL(!result.Ok);
        REQUIRE_OR_FAIL(result.Error == "FIELD_AUDIT_ARGUMENT_INVALID");
    }

    {
        FieldMigrationRequest request{};
        request.Scope = "scene-prefab-data";
        request.OutputDirectory = root / "success";
        request.RemediationBatchId = "batch-030201";
        request.RollbackManifestPath = request.OutputDirectory / "rollback-scene-prefab.json";
        request.Entries = {
            BuildEntry("scene://forest",
                       FieldMigrationAssetKind::Scene,
                       "Scene::SpawnPoint::DisplayName",
                       "scene<->runtime",
                       "scene::SpawnPoint::DisplayName",
                       "",
                       true,
                       "Spawn",
                       {"Root", "SpawnPoint", "NpcAnchor"},
                       {"Root", "GhostNode", "SpawnPoint"},
                       "finding-b",
                       "scene-authoring"),
            BuildEntry("prefab://enemy/grunt",
                       FieldMigrationAssetKind::Prefab,
                       "Prefab::Enemy::Health",
                       "prefab<->runtime",
                       "prefab::Enemy::Health",
                       "100",
                       true,
                       "100",
                       {"Root", "Body", "Weapon"},
                       {"Body", "StaleSocket", "Weapon"},
                       "finding-a",
                       "prefab-authoring")};

        const Result<FieldMigrationResult> first = MigrateSceneAndPrefabFieldData(request);
        REQUIRE_OR_FAIL(first.Ok);
        REQUIRE_OR_FAIL(first.Value.Scope == request.Scope);
        REQUIRE_OR_FAIL(first.Value.RemediationBatchId == request.RemediationBatchId);
        REQUIRE_OR_FAIL(first.Value.RollbackManifestPath == request.RollbackManifestPath);
        REQUIRE_OR_FAIL(first.Value.Summary.SceneMigrationCount == 2u);
        REQUIRE_OR_FAIL(first.Value.Summary.PrefabMigrationCount == 1u);
        REQUIRE_OR_FAIL(first.Value.Summary.RequiredBackfillCount == 1u);
        REQUIRE_OR_FAIL(first.Value.Summary.GraphNormalizationCount == 2u);
        REQUIRE_OR_FAIL(first.Value.Summary.RollbackSafeMigrationCount == first.Value.Summary.TotalMigrationCount);
        REQUIRE_OR_FAIL(first.Value.Summary.TotalMigrationCount == 3u);
        REQUIRE_OR_FAIL(first.Value.MigrationRecords.size() == 3u);
        REQUIRE_OR_FAIL(!first.Value.RollbackManifestDigest.empty());
        REQUIRE_OR_FAIL(!first.Value.DeterministicDigest.empty());

        bool sawSceneBackfill = false;
        bool sawSceneGraphNormalization = false;
        bool sawPrefabGraphNormalization = false;
        for (const FieldMigrationRecord& record : first.Value.MigrationRecords) {
            REQUIRE_OR_FAIL(!record.MigrationId.empty());
            REQUIRE_OR_FAIL(!record.TargetFieldId.empty());
            REQUIRE_OR_FAIL(!record.PropertyPath.empty());
            REQUIRE_OR_FAIL(!record.DeterministicDigest.empty());
            REQUIRE_OR_FAIL(!record.Provenance.FindingId.empty());
            REQUIRE_OR_FAIL(!record.Provenance.RuleId.empty());
            REQUIRE_OR_FAIL(!record.Provenance.Owner.empty());
            REQUIRE_OR_FAIL(record.Provenance.RemediationBatchId == request.RemediationBatchId);
            REQUIRE_OR_FAIL(record.Rollback.RollbackRequired);
            REQUIRE_OR_FAIL(!record.Rollback.RollbackCheckpointId.empty());
            REQUIRE_OR_FAIL(!record.Rollback.RollbackPropertyPath.empty());
            REQUIRE_OR_FAIL(!record.Rollback.RollbackManifestPath.empty());

            if (record.AssetId == "scene://forest" && record.TransformKind == FieldMigrationTransformKind::RequiredFieldBackfill) {
                sawSceneBackfill = true;
                REQUIRE_OR_FAIL(record.AssetKind == FieldMigrationAssetKind::Scene);
                REQUIRE_OR_FAIL(record.PropertyPath == "field.value");
                REQUIRE_OR_FAIL(record.ExistingValue == "<unset>");
                REQUIRE_OR_FAIL(record.ReplacementValue == "Spawn");
                REQUIRE_OR_FAIL(record.GraphNormalizationAction == "none");
            }

            if (record.AssetId == "scene://forest" &&
                record.TransformKind == FieldMigrationTransformKind::StaleGraphNormalization) {
                sawSceneGraphNormalization = true;
                REQUIRE_OR_FAIL(record.AssetKind == FieldMigrationAssetKind::Scene);
                REQUIRE_OR_FAIL(record.PropertyPath == "graph.edges");
                REQUIRE_OR_FAIL(record.ExistingValue == "GhostNode,Root,SpawnPoint");
                REQUIRE_OR_FAIL(record.ReplacementValue == "Root,SpawnPoint");
                REQUIRE_OR_FAIL(record.GraphNormalizationAction == "remove-stale-edge-targets");
            }

            if (record.AssetId == "prefab://enemy/grunt" &&
                record.TransformKind == FieldMigrationTransformKind::StaleGraphNormalization) {
                sawPrefabGraphNormalization = true;
                REQUIRE_OR_FAIL(record.AssetKind == FieldMigrationAssetKind::Prefab);
                REQUIRE_OR_FAIL(record.PropertyPath == "graph.edges");
                REQUIRE_OR_FAIL(record.ExistingValue == "Body,StaleSocket,Weapon");
                REQUIRE_OR_FAIL(record.ReplacementValue == "Body,Weapon");
                REQUIRE_OR_FAIL(record.GraphNormalizationAction == "remove-stale-edge-targets");
            }
        }
        REQUIRE_OR_FAIL(sawSceneBackfill);
        REQUIRE_OR_FAIL(sawSceneGraphNormalization);
        REQUIRE_OR_FAIL(sawPrefabGraphNormalization);

        const Result<FieldMigrationResult> second = MigrateSceneAndPrefabFieldData(request);
        REQUIRE_OR_FAIL(second.Ok);
        REQUIRE_OR_FAIL(second.Value.RollbackManifestDigest == first.Value.RollbackManifestDigest);
        REQUIRE_OR_FAIL(second.Value.DeterministicDigest == first.Value.DeterministicDigest);
        REQUIRE_OR_FAIL(second.Value.MigrationRecords.size() == first.Value.MigrationRecords.size());
        for (std::size_t index = 0; index < first.Value.MigrationRecords.size(); ++index) {
            REQUIRE_OR_FAIL(second.Value.MigrationRecords[index].MigrationId == first.Value.MigrationRecords[index].MigrationId);
            REQUIRE_OR_FAIL(second.Value.MigrationRecords[index].DeterministicDigest ==
                   first.Value.MigrationRecords[index].DeterministicDigest);
            REQUIRE_OR_FAIL(second.Value.MigrationRecords[index].Rollback.RollbackCheckpointId ==
                   first.Value.MigrationRecords[index].Rollback.RollbackCheckpointId);
        }
    }

    {
        FieldMigrationRequest invalidRequest{};
        const Result<FieldMigrationResult> result = MigrateAddressableBundleAndBuildManifestFieldData(invalidRequest);
        REQUIRE_OR_FAIL(!result.Ok);
        REQUIRE_OR_FAIL(result.Error == "FIELD_AUDIT_ARGUMENT_INVALID");
    }

    {
        FieldMigrationRequest request{};
        request.Scope = "addressable-bundle-build-manifest-data";
        request.OutputDirectory = root / "addressable-bundle-build-success";
        request.RemediationBatchId = "batch-030203";
        request.RollbackManifestPath = request.OutputDirectory / "rollback-addressable-bundle-build.json";

        FieldMigrationEntry catalogEntry = BuildAddressableEntry("catalog://main",
                                                                 FieldMigrationAssetKind::AddressableCatalog,
                                                                 "catalog::main::player",
                                                                 "finding-abm-a",
                                                                 "asset-pipeline");
        catalogEntry.CatalogAssetKey = "assets/player.prefab";
        catalogEntry.ExpectedCatalogAssetKey = "assets/player";
        catalogEntry.CatalogAddress = "characters/player-v1";
        catalogEntry.ExpectedCatalogAddress = "characters/player";
        catalogEntry.ManifestCatalogKey = "catalog-main-legacy";
        catalogEntry.ExpectedManifestCatalogKey = "catalog-main";

        FieldMigrationEntry bundleEntry = BuildAddressableEntry("bundle://characters/player",
                                                                FieldMigrationAssetKind::AssetBundle,
                                                                "bundle::characters::player",
                                                                "finding-abm-b",
                                                                "build-pipeline");
        bundleEntry.BundleId = "bundle-player-legacy";
        bundleEntry.ExpectedBundleId = "bundle-player";
        bundleEntry.BundleHash = "hash-001";
        bundleEntry.ExpectedBundleHash = "hash-101";
        bundleEntry.BundleReference = "bundle://legacy/player";
        bundleEntry.ExpectedBundleReference = "bundle://characters/player";
        bundleEntry.ManifestBundleId = "bundle-player-legacy";
        bundleEntry.ExpectedManifestBundleId = "bundle-player";

        FieldMigrationEntry buildManifestEntry = BuildAddressableEntry("build-manifest://windows",
                                                                       FieldMigrationAssetKind::BuildManifest,
                                                                       "build-manifest::windows::default",
                                                                       "finding-abm-c",
                                                                       "release-engineering");
        buildManifestEntry.BuildProfile = "debug";
        buildManifestEntry.ExpectedBuildProfile = "release";
        buildManifestEntry.BuildTarget = "win64-dev";
        buildManifestEntry.ExpectedBuildTarget = "win64";

        request.Entries = {catalogEntry, catalogEntry, bundleEntry, buildManifestEntry};

        const Result<FieldMigrationResult> first = MigrateAddressableBundleAndBuildManifestFieldData(request);
        REQUIRE_OR_FAIL(first.Ok);
        REQUIRE_OR_FAIL(first.Value.Scope == request.Scope);
        REQUIRE_OR_FAIL(first.Value.Summary.AddressableCatalogMigrationCount == 3u);
        REQUIRE_OR_FAIL(first.Value.Summary.AssetBundleMigrationCount == 4u);
        REQUIRE_OR_FAIL(first.Value.Summary.BuildManifestMigrationCount == 2u);
        REQUIRE_OR_FAIL(first.Value.Summary.CatalogParityNormalizationCount == 3u);
        REQUIRE_OR_FAIL(first.Value.Summary.BundleParityNormalizationCount == 3u);
        REQUIRE_OR_FAIL(first.Value.Summary.BuildProfileTargetNormalizationCount == 2u);
        REQUIRE_OR_FAIL(first.Value.Summary.ManifestLinkageNormalizationCount == 1u);
        REQUIRE_OR_FAIL(first.Value.Summary.TotalMigrationCount == 9u);
        REQUIRE_OR_FAIL(first.Value.Summary.RollbackSafeMigrationCount == first.Value.Summary.TotalMigrationCount);
        REQUIRE_OR_FAIL(first.Value.MigrationRecords.size() == 9u);
        REQUIRE_OR_FAIL(!first.Value.RollbackManifestDigest.empty());
        REQUIRE_OR_FAIL(!first.Value.DeterministicDigest.empty());

        bool sawCatalogAssetKey = false;
        bool sawCatalogAddress = false;
        bool sawManifestCatalogLink = false;
        bool sawBundleId = false;
        bool sawBundleHash = false;
        bool sawBundleReference = false;
        bool sawManifestBundleLink = false;
        bool sawBuildProfile = false;
        bool sawBuildTarget = false;
        for (const FieldMigrationRecord& record : first.Value.MigrationRecords) {
            REQUIRE_OR_FAIL(!record.Rollback.RollbackCheckpointId.empty());
            REQUIRE_OR_FAIL(!record.DeterministicDigest.empty());
            REQUIRE_OR_FAIL(record.Provenance.RemediationBatchId == request.RemediationBatchId);

            if (record.PropertyPath == "addressables.catalog.asset-key") {
                sawCatalogAssetKey = true;
                REQUIRE_OR_FAIL(record.TransformKind == FieldMigrationTransformKind::CatalogParityNormalization);
                REQUIRE_OR_FAIL(record.ExistingValue == "assets/player.prefab");
                REQUIRE_OR_FAIL(record.ReplacementValue == "assets/player");
            } else if (record.PropertyPath == "addressables.catalog.address") {
                sawCatalogAddress = true;
                REQUIRE_OR_FAIL(record.TransformKind == FieldMigrationTransformKind::CatalogParityNormalization);
                REQUIRE_OR_FAIL(record.ExistingValue == "characters/player-v1");
                REQUIRE_OR_FAIL(record.ReplacementValue == "characters/player");
            } else if (record.PropertyPath == "build.manifest.catalog-link.catalog-key") {
                sawManifestCatalogLink = true;
                REQUIRE_OR_FAIL(record.TransformKind == FieldMigrationTransformKind::CatalogParityNormalization);
                REQUIRE_OR_FAIL(record.ExistingValue == "catalog-main-legacy");
                REQUIRE_OR_FAIL(record.ReplacementValue == "catalog-main");
            } else if (record.PropertyPath == "addressables.bundle.id") {
                sawBundleId = true;
                REQUIRE_OR_FAIL(record.TransformKind == FieldMigrationTransformKind::BundleParityNormalization);
                REQUIRE_OR_FAIL(record.ExistingValue == "bundle-player-legacy");
                REQUIRE_OR_FAIL(record.ReplacementValue == "bundle-player");
            } else if (record.PropertyPath == "addressables.bundle.hash") {
                sawBundleHash = true;
                REQUIRE_OR_FAIL(record.TransformKind == FieldMigrationTransformKind::BundleParityNormalization);
                REQUIRE_OR_FAIL(record.ExistingValue == "hash-001");
                REQUIRE_OR_FAIL(record.ReplacementValue == "hash-101");
            } else if (record.PropertyPath == "addressables.bundle.reference") {
                sawBundleReference = true;
                REQUIRE_OR_FAIL(record.TransformKind == FieldMigrationTransformKind::BundleParityNormalization);
                REQUIRE_OR_FAIL(record.ExistingValue == "bundle://legacy/player");
                REQUIRE_OR_FAIL(record.ReplacementValue == "bundle://characters/player");
            } else if (record.PropertyPath == "build.manifest.bundle-link.bundle-id") {
                sawManifestBundleLink = true;
                REQUIRE_OR_FAIL(record.TransformKind == FieldMigrationTransformKind::ManifestLinkageNormalization);
                REQUIRE_OR_FAIL(record.ExistingValue == "bundle-player-legacy");
                REQUIRE_OR_FAIL(record.ReplacementValue == "bundle-player");
            } else if (record.PropertyPath == "build.profile") {
                sawBuildProfile = true;
                REQUIRE_OR_FAIL(record.TransformKind == FieldMigrationTransformKind::BuildProfileTargetNormalization);
                REQUIRE_OR_FAIL(record.ExistingValue == "debug");
                REQUIRE_OR_FAIL(record.ReplacementValue == "release");
            } else if (record.PropertyPath == "build.target") {
                sawBuildTarget = true;
                REQUIRE_OR_FAIL(record.TransformKind == FieldMigrationTransformKind::BuildProfileTargetNormalization);
                REQUIRE_OR_FAIL(record.ExistingValue == "win64-dev");
                REQUIRE_OR_FAIL(record.ReplacementValue == "win64");
            }
        }

        REQUIRE_OR_FAIL(sawCatalogAssetKey);
        REQUIRE_OR_FAIL(sawCatalogAddress);
        REQUIRE_OR_FAIL(sawManifestCatalogLink);
        REQUIRE_OR_FAIL(sawBundleId);
        REQUIRE_OR_FAIL(sawBundleHash);
        REQUIRE_OR_FAIL(sawBundleReference);
        REQUIRE_OR_FAIL(sawManifestBundleLink);
        REQUIRE_OR_FAIL(sawBuildProfile);
        REQUIRE_OR_FAIL(sawBuildTarget);

        const Result<FieldMigrationResult> second = MigrateAddressableBundleAndBuildManifestFieldData(request);
        REQUIRE_OR_FAIL(second.Ok);
        REQUIRE_OR_FAIL(second.Value.RollbackManifestDigest == first.Value.RollbackManifestDigest);
        REQUIRE_OR_FAIL(second.Value.DeterministicDigest == first.Value.DeterministicDigest);
        REQUIRE_OR_FAIL(second.Value.MigrationRecords.size() == first.Value.MigrationRecords.size());
        for (std::size_t index = 0; index < first.Value.MigrationRecords.size(); ++index) {
            REQUIRE_OR_FAIL(second.Value.MigrationRecords[index].MigrationId == first.Value.MigrationRecords[index].MigrationId);
            REQUIRE_OR_FAIL(second.Value.MigrationRecords[index].DeterministicDigest ==
                            first.Value.MigrationRecords[index].DeterministicDigest);
            REQUIRE_OR_FAIL(second.Value.MigrationRecords[index].Rollback.RollbackCheckpointId ==
                            first.Value.MigrationRecords[index].Rollback.RollbackCheckpointId);
        }
    }

    {
        FieldMigrationRequest request{};
        request.Scope = "ui-localization-data";
        request.OutputDirectory = root / "ui-localization-success";
        request.RemediationBatchId = "batch-030202";
        request.RollbackManifestPath = request.OutputDirectory / "rollback-ui-localization.json";

        FieldMigrationEntry widgetEntry =
            BuildUIEntry("widget://hud/main", FieldMigrationAssetKind::UIWidget, "ui::hud::title", "finding-ui-a", "ui-authoring");
        widgetEntry.BindingKey = "hud.title.old";
        widgetEntry.ExpectedBindingKey = "hud.title";
        widgetEntry.BindingPath = "vm.old.title";
        widgetEntry.ExpectedBindingPath = "vm.hud.title";
        widgetEntry.LocalizationKey = "loc.hud.title.legacy";
        widgetEntry.ExpectedLocalizationKey = "loc.hud.title";
        widgetEntry.LocalizationReference = "table://legacy/hud";
        widgetEntry.ExpectedLocalizationReference = "table://ui/hud";
        widgetEntry.WidgetPresentationMode = "World";
        widgetEntry.WidgetMetadataSpace = "Modal";

        FieldMigrationEntry localeEntry = BuildUIEntry("loc://ui/hud",
                                                       FieldMigrationAssetKind::LocalizationTable,
                                                       "loc::hud::table",
                                                       "finding-ui-b",
                                                       "localization-authoring");
        localeEntry.LocaleCode = "en-US";
        localeEntry.LocaleSchemaVersion = "v1";
        localeEntry.ExpectedLocaleSchemaVersion = "v2";

        request.Entries = {widgetEntry, widgetEntry, localeEntry};

        const Result<FieldMigrationResult> first = MigrateUIAndLocalizationFieldData(request);
        REQUIRE_OR_FAIL(first.Ok);
        REQUIRE_OR_FAIL(first.Value.Scope == request.Scope);
        REQUIRE_OR_FAIL(first.Value.Summary.UIWidgetMigrationCount == 5u);
        REQUIRE_OR_FAIL(first.Value.Summary.LocalizationTableMigrationCount == 1u);
        REQUIRE_OR_FAIL(first.Value.Summary.BindingNormalizationCount == 2u);
        REQUIRE_OR_FAIL(first.Value.Summary.LocalizationNormalizationCount == 2u);
        REQUIRE_OR_FAIL(first.Value.Summary.LocaleSchemaNormalizationCount == 1u);
        REQUIRE_OR_FAIL(first.Value.Summary.WidgetMetadataNormalizationCount == 1u);
        REQUIRE_OR_FAIL(first.Value.Summary.TotalMigrationCount == 6u);
        REQUIRE_OR_FAIL(first.Value.Summary.RollbackSafeMigrationCount == first.Value.Summary.TotalMigrationCount);
        REQUIRE_OR_FAIL(first.Value.MigrationRecords.size() == 6u);
        REQUIRE_OR_FAIL(!first.Value.RollbackManifestDigest.empty());
        REQUIRE_OR_FAIL(!first.Value.DeterministicDigest.empty());

        bool sawBindingKey = false;
        bool sawBindingPath = false;
        bool sawLocalizationKey = false;
        bool sawLocalizationReference = false;
        bool sawLocaleSchema = false;
        bool sawWidgetMetadata = false;
        for (const FieldMigrationRecord& record : first.Value.MigrationRecords) {
            REQUIRE_OR_FAIL(!record.Rollback.RollbackCheckpointId.empty());
            REQUIRE_OR_FAIL(!record.DeterministicDigest.empty());
            REQUIRE_OR_FAIL(record.Provenance.RemediationBatchId == request.RemediationBatchId);

            if (record.PropertyPath == "ui.binding.key") {
                sawBindingKey = true;
                REQUIRE_OR_FAIL(record.TransformKind == FieldMigrationTransformKind::BindingKeyPathNormalization);
                REQUIRE_OR_FAIL(record.ExistingValue == "hud.title.old");
                REQUIRE_OR_FAIL(record.ReplacementValue == "hud.title");
            } else if (record.PropertyPath == "ui.binding.path") {
                sawBindingPath = true;
                REQUIRE_OR_FAIL(record.TransformKind == FieldMigrationTransformKind::BindingKeyPathNormalization);
                REQUIRE_OR_FAIL(record.ExistingValue == "vm.old.title");
                REQUIRE_OR_FAIL(record.ReplacementValue == "vm.hud.title");
            } else if (record.PropertyPath == "ui.localization.key") {
                sawLocalizationKey = true;
                REQUIRE_OR_FAIL(record.TransformKind == FieldMigrationTransformKind::LocalizationReferenceNormalization);
                REQUIRE_OR_FAIL(record.ExistingValue == "loc.hud.title.legacy");
                REQUIRE_OR_FAIL(record.ReplacementValue == "loc.hud.title");
            } else if (record.PropertyPath == "ui.localization.reference") {
                sawLocalizationReference = true;
                REQUIRE_OR_FAIL(record.TransformKind == FieldMigrationTransformKind::LocalizationReferenceNormalization);
                REQUIRE_OR_FAIL(record.ExistingValue == "table://legacy/hud");
                REQUIRE_OR_FAIL(record.ReplacementValue == "table://ui/hud");
            } else if (record.PropertyPath == "localization.locale-schema.version[en-US]") {
                sawLocaleSchema = true;
                REQUIRE_OR_FAIL(record.TransformKind == FieldMigrationTransformKind::LocaleSchemaNormalization);
                REQUIRE_OR_FAIL(record.ExistingValue == "v1");
                REQUIRE_OR_FAIL(record.ReplacementValue == "v2");
            } else if (record.PropertyPath == "ui.widget.metadata.space") {
                sawWidgetMetadata = true;
                REQUIRE_OR_FAIL(record.TransformKind == FieldMigrationTransformKind::WidgetMetadataNormalization);
                REQUIRE_OR_FAIL(record.ExistingValue == "Modal");
                REQUIRE_OR_FAIL(record.ReplacementValue == "World");
            }
        }

        REQUIRE_OR_FAIL(sawBindingKey);
        REQUIRE_OR_FAIL(sawBindingPath);
        REQUIRE_OR_FAIL(sawLocalizationKey);
        REQUIRE_OR_FAIL(sawLocalizationReference);
        REQUIRE_OR_FAIL(sawLocaleSchema);
        REQUIRE_OR_FAIL(sawWidgetMetadata);

        const Result<FieldMigrationResult> second = MigrateUIAndLocalizationFieldData(request);
        REQUIRE_OR_FAIL(second.Ok);
        REQUIRE_OR_FAIL(second.Value.RollbackManifestDigest == first.Value.RollbackManifestDigest);
        REQUIRE_OR_FAIL(second.Value.DeterministicDigest == first.Value.DeterministicDigest);
        REQUIRE_OR_FAIL(second.Value.MigrationRecords.size() == first.Value.MigrationRecords.size());
        for (std::size_t index = 0; index < first.Value.MigrationRecords.size(); ++index) {
            REQUIRE_OR_FAIL(second.Value.MigrationRecords[index].MigrationId == first.Value.MigrationRecords[index].MigrationId);
            REQUIRE_OR_FAIL(second.Value.MigrationRecords[index].DeterministicDigest ==
                            first.Value.MigrationRecords[index].DeterministicDigest);
            REQUIRE_OR_FAIL(second.Value.MigrationRecords[index].Rollback.RollbackCheckpointId ==
                            first.Value.MigrationRecords[index].Rollback.RollbackCheckpointId);
        }
    }

    return 0;
}

#undef REQUIRE_OR_FAIL
