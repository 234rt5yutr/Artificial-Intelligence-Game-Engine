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

    return 0;
}

#undef REQUIRE_OR_FAIL
