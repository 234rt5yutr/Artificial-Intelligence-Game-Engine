#include "Core/Remediation/FieldMigrationService.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace Core::Remediation {
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

[[nodiscard]] bool IsValidEntry(const FieldMigrationEntry& entry) {
    if (entry.AssetId.empty() || entry.StableFieldKey.empty() || entry.DomainPair.empty() || entry.FieldId.empty()) {
        return false;
    }
    if (entry.Provenance.FindingId.empty() || entry.Provenance.RuleId.empty() || entry.Provenance.Owner.empty()) {
        return false;
    }
    if (entry.Required && entry.RequiredValue.empty()) {
        return false;
    }
    for (const std::string& nodeId : entry.GraphNodeIds) {
        if (nodeId.empty()) {
            return false;
        }
    }
    for (const std::string& edgeTarget : entry.GraphEdgeTargets) {
        if (edgeTarget.empty()) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string AsExistingValue(const std::string& value) {
    if (value.empty()) {
        return "<unset>";
    }
    return value;
}

[[nodiscard]] std::string NormalizeToken(const std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const unsigned char symbol : value) {
        if (!std::isspace(symbol)) {
            normalized.push_back(static_cast<char>(std::tolower(symbol)));
        }
    }
    return normalized;
}

[[nodiscard]] std::vector<std::string> SortUniqueValues(const std::vector<std::string>& values) {
    std::vector<std::string> normalized = values;
    normalized.erase(std::remove_if(normalized.begin(),
                                    normalized.end(),
                                    [](const std::string& value) { return value.empty(); }),
                     normalized.end());
    std::sort(normalized.begin(), normalized.end());
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
    return normalized;
}

[[nodiscard]] std::string JoinValues(const std::vector<std::string>& values) {
    if (values.empty()) {
        return "<none>";
    }

    std::string joined;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0u) {
            joined.push_back(',');
        }
        joined.append(values[index]);
    }
    return joined;
}

[[nodiscard]] std::string BuildMigrationId(const FieldMigrationRecord& record) {
    std::string idMaterial;
    idMaterial.reserve(320u);
    idMaterial.append("field-migration|");
    idMaterial.append(record.Provenance.FindingId);
    idMaterial.push_back('|');
    idMaterial.append(record.AssetId);
    idMaterial.push_back('|');
    idMaterial.append(record.TargetFieldId);
    idMaterial.push_back('|');
    idMaterial.append(std::to_string(static_cast<uint32_t>(record.AssetKind)));
    idMaterial.push_back('|');
    idMaterial.append(std::to_string(static_cast<uint32_t>(record.TransformKind)));
    idMaterial.push_back('|');
    idMaterial.append(record.PropertyPath);
    idMaterial.push_back('|');
    idMaterial.append(record.ReplacementValue);
    return HashToHex(HashString(idMaterial));
}

[[nodiscard]] std::string ComputeMigrationRecordDigest(const FieldMigrationRecord& record) {
    std::string digestMaterial;
    digestMaterial.reserve(640u);
    digestMaterial.append(record.MigrationId);
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(static_cast<uint32_t>(record.AssetKind)));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(static_cast<uint32_t>(record.TransformKind)));
    digestMaterial.push_back('|');
    digestMaterial.append(record.AssetId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.StableFieldKey);
    digestMaterial.push_back('|');
    digestMaterial.append(record.DomainPair);
    digestMaterial.push_back('|');
    digestMaterial.append(record.TargetFieldId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.PropertyPath);
    digestMaterial.push_back('|');
    digestMaterial.append(record.ExistingValue);
    digestMaterial.push_back('|');
    digestMaterial.append(record.ReplacementValue);
    digestMaterial.push_back('|');
    digestMaterial.append(record.GraphNormalizationAction);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Provenance.FindingId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Provenance.RuleId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Provenance.Owner);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Provenance.RemediationBatchId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Rollback.RollbackCheckpointId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Rollback.RollbackPropertyPath);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Rollback.RollbackValue);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Rollback.RollbackRequired ? "true" : "false");
    digestMaterial.push_back('|');
    digestMaterial.append(record.Rollback.RollbackManifestPath);
    return HashToHex(HashString(digestMaterial));
}

[[nodiscard]] std::string ComputeRollbackManifestDigest(const FieldMigrationResult& result) {
    std::string digestMaterial;
    digestMaterial.reserve((result.MigrationRecords.size() * 96u) + 224u);
    digestMaterial.append(result.Scope);
    digestMaterial.push_back('|');
    digestMaterial.append(result.RemediationBatchId);
    digestMaterial.push_back('|');
    digestMaterial.append(result.RollbackManifestPath.generic_string());
    digestMaterial.push_back('\n');

    for (const FieldMigrationRecord& record : result.MigrationRecords) {
        digestMaterial.append(record.MigrationId);
        digestMaterial.push_back('|');
        digestMaterial.append(record.Rollback.RollbackCheckpointId);
        digestMaterial.push_back('|');
        digestMaterial.append(record.Rollback.RollbackPropertyPath);
        digestMaterial.push_back('|');
        digestMaterial.append(record.Rollback.RollbackValue);
        digestMaterial.push_back('\n');
    }
    return HashToHex(HashString(digestMaterial));
}

[[nodiscard]] std::string ComputeResultDigest(const FieldMigrationResult& result) {
    std::string digestMaterial;
    digestMaterial.reserve((result.MigrationRecords.size() * 80u) + 352u);
    digestMaterial.append(result.Scope);
    digestMaterial.push_back('|');
    digestMaterial.append(result.RemediationBatchId);
    digestMaterial.push_back('|');
    digestMaterial.append(result.RollbackManifestPath.generic_string());
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.SceneMigrationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.PrefabMigrationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.UIWidgetMigrationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.LocalizationTableMigrationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.AddressableCatalogMigrationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.AssetBundleMigrationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.BuildManifestMigrationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.PlayerSaveMigrationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.ReplayDataMigrationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.AutomationBaselineMigrationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.RequiredBackfillCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.GraphNormalizationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.BindingNormalizationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.LocalizationNormalizationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.LocaleSchemaNormalizationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.WidgetMetadataNormalizationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.CatalogParityNormalizationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.BundleParityNormalizationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.BuildProfileTargetNormalizationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.ManifestLinkageNormalizationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.SaveCompatibilityNormalizationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.ReplayDeterminismNormalizationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.AutomationBaselineNormalizationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.RollbackSafeMigrationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.TotalMigrationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(result.RollbackManifestDigest);
    digestMaterial.push_back('\n');

    for (const FieldMigrationRecord& record : result.MigrationRecords) {
        digestMaterial.append(record.MigrationId);
        digestMaterial.push_back('|');
        digestMaterial.append(record.DeterministicDigest);
        digestMaterial.push_back('|');
        digestMaterial.append(record.TargetFieldId);
        digestMaterial.push_back('\n');
    }
    return HashToHex(HashString(digestMaterial));
}

void SortMigrationRecords(std::vector<FieldMigrationRecord>& records) {
    std::sort(records.begin(),
              records.end(),
              [](const FieldMigrationRecord& left, const FieldMigrationRecord& right) {
                  if (left.Provenance.FindingId != right.Provenance.FindingId) {
                      return left.Provenance.FindingId < right.Provenance.FindingId;
                  }
                  if (left.StableFieldKey != right.StableFieldKey) {
                      return left.StableFieldKey < right.StableFieldKey;
                  }
                  if (left.AssetId != right.AssetId) {
                      return left.AssetId < right.AssetId;
                  }
                  if (left.AssetKind != right.AssetKind) {
                      return static_cast<uint32_t>(left.AssetKind) < static_cast<uint32_t>(right.AssetKind);
                  }
                  if (left.TransformKind != right.TransformKind) {
                      return static_cast<uint32_t>(left.TransformKind) < static_cast<uint32_t>(right.TransformKind);
                  }
                  if (left.PropertyPath != right.PropertyPath) {
                      return left.PropertyPath < right.PropertyPath;
                  }
                  return left.MigrationId < right.MigrationId;
              });
}

} // namespace

Result<FieldMigrationResult> MigrateSceneAndPrefabFieldData(const FieldMigrationRequest& request) {
    if (request.Scope.empty() || request.OutputDirectory.empty() || request.RemediationBatchId.empty() ||
        request.RollbackManifestPath.empty() || request.Entries.empty()) {
        return Result<FieldMigrationResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (request.Scope != "scene-prefab-data") {
        return Result<FieldMigrationResult>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    for (const FieldMigrationEntry& entry : request.Entries) {
        if (!IsValidEntry(entry)) {
            return Result<FieldMigrationResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
        }
    }

    if (!EnsureOutputDirectory(request.OutputDirectory)) {
        return Result<FieldMigrationResult>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    std::vector<FieldMigrationRecord> migrationRecords;
    std::set<std::string> seenMigrationKeys;
    for (const FieldMigrationEntry& entry : request.Entries) {
        auto emitMigration = [&](const FieldMigrationTransformKind transformKind,
                                 const std::string& propertyPath,
                                 const std::string& existingValue,
                                 const std::string& replacementValue,
                                 const std::string& graphNormalizationAction) {
            std::string dedupeKey;
            dedupeKey.reserve(320u);
            dedupeKey.append(entry.Provenance.FindingId);
            dedupeKey.push_back('|');
            dedupeKey.append(entry.AssetId);
            dedupeKey.push_back('|');
            dedupeKey.append(entry.FieldId);
            dedupeKey.push_back('|');
            dedupeKey.append(std::to_string(static_cast<uint32_t>(transformKind)));
            dedupeKey.push_back('|');
            dedupeKey.append(propertyPath);
            dedupeKey.push_back('|');
            dedupeKey.append(replacementValue);
            if (seenMigrationKeys.contains(dedupeKey)) {
                return;
            }
            seenMigrationKeys.emplace(std::move(dedupeKey));

            FieldMigrationRecord record{};
            record.AssetKind = entry.AssetKind;
            record.TransformKind = transformKind;
            record.AssetId = entry.AssetId;
            record.StableFieldKey = entry.StableFieldKey;
            record.DomainPair = entry.DomainPair;
            record.TargetFieldId = entry.FieldId;
            record.PropertyPath = propertyPath;
            record.ExistingValue = existingValue;
            record.ReplacementValue = replacementValue;
            record.GraphNormalizationAction = graphNormalizationAction;
            record.Provenance = entry.Provenance;
            record.Provenance.RemediationBatchId = request.RemediationBatchId;
            record.Rollback.RollbackRequired = true;
            record.Rollback.RollbackPropertyPath = propertyPath;
            record.Rollback.RollbackValue = existingValue;
            record.Rollback.RollbackManifestPath = request.RollbackManifestPath.generic_string();
            record.Rollback.RollbackCheckpointId =
                HashToHex(HashString(request.RemediationBatchId + "|" + entry.AssetId + "|" + entry.FieldId + "|" +
                                     propertyPath + "|" + existingValue + "|" + replacementValue));
            record.MigrationId = BuildMigrationId(record);
            record.DeterministicDigest = ComputeMigrationRecordDigest(record);
            migrationRecords.push_back(std::move(record));
        };

        if (entry.Required && entry.FieldValue.empty()) {
            emitMigration(FieldMigrationTransformKind::RequiredFieldBackfill,
                          "field.value",
                          "<unset>",
                          entry.RequiredValue,
                          "none");
        }

        const std::vector<std::string> normalizedNodes = SortUniqueValues(entry.GraphNodeIds);
        const std::vector<std::string> normalizedEdges = SortUniqueValues(entry.GraphEdgeTargets);
        std::vector<std::string> filteredEdges;
        filteredEdges.reserve(normalizedEdges.size());

        for (const std::string& edgeTarget : normalizedEdges) {
            if (std::find(normalizedNodes.begin(), normalizedNodes.end(), edgeTarget) != normalizedNodes.end()) {
                filteredEdges.push_back(edgeTarget);
            }
        }

        const std::string normalizedEdgesText = JoinValues(normalizedEdges);
        const std::string filteredEdgesText = JoinValues(filteredEdges);
        if (normalizedEdgesText != filteredEdgesText) {
            emitMigration(FieldMigrationTransformKind::StaleGraphNormalization,
                          "graph.edges",
                          normalizedEdgesText,
                          filteredEdgesText,
                          "remove-stale-edge-targets");
        }
    }

    SortMigrationRecords(migrationRecords);

    FieldMigrationResult result{};
    result.Scope = request.Scope;
    result.OutputDirectory = request.OutputDirectory;
    result.RemediationBatchId = request.RemediationBatchId;
    result.RollbackManifestPath = request.RollbackManifestPath;
    result.MigrationRecords = std::move(migrationRecords);
    for (const FieldMigrationRecord& record : result.MigrationRecords) {
        if (record.AssetKind == FieldMigrationAssetKind::Scene) {
            ++result.Summary.SceneMigrationCount;
        } else if (record.AssetKind == FieldMigrationAssetKind::Prefab) {
            ++result.Summary.PrefabMigrationCount;
        }
        if (record.TransformKind == FieldMigrationTransformKind::RequiredFieldBackfill) {
            ++result.Summary.RequiredBackfillCount;
        } else if (record.TransformKind == FieldMigrationTransformKind::StaleGraphNormalization) {
            ++result.Summary.GraphNormalizationCount;
        }
        if (record.Rollback.RollbackRequired) {
            ++result.Summary.RollbackSafeMigrationCount;
        }
    }
    result.Summary.TotalMigrationCount = static_cast<uint32_t>(result.MigrationRecords.size());
    result.RollbackManifestDigest = ComputeRollbackManifestDigest(result);
    result.DeterministicDigest = ComputeResultDigest(result);
    return Result<FieldMigrationResult>::Success(std::move(result));
}

Result<FieldMigrationResult> MigrateUIAndLocalizationFieldData(const FieldMigrationRequest& request) {
    if (request.Scope.empty() || request.OutputDirectory.empty() || request.RemediationBatchId.empty() ||
        request.RollbackManifestPath.empty() || request.Entries.empty()) {
        return Result<FieldMigrationResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (request.Scope != "ui-localization-data") {
        return Result<FieldMigrationResult>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    for (const FieldMigrationEntry& entry : request.Entries) {
        if (!IsValidEntry(entry)) {
            return Result<FieldMigrationResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
        }
    }

    if (!EnsureOutputDirectory(request.OutputDirectory)) {
        return Result<FieldMigrationResult>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    std::vector<FieldMigrationRecord> migrationRecords;
    std::set<std::string> seenMigrationKeys;
    for (const FieldMigrationEntry& entry : request.Entries) {
        auto emitMigration = [&](const FieldMigrationTransformKind transformKind,
                                 const std::string& propertyPath,
                                 const std::string& existingValue,
                                 const std::string& replacementValue,
                                 const std::string& graphNormalizationAction) {
            std::string dedupeKey;
            dedupeKey.reserve(384u);
            dedupeKey.append(entry.Provenance.FindingId);
            dedupeKey.push_back('|');
            dedupeKey.append(entry.AssetId);
            dedupeKey.push_back('|');
            dedupeKey.append(entry.FieldId);
            dedupeKey.push_back('|');
            dedupeKey.append(std::to_string(static_cast<uint32_t>(transformKind)));
            dedupeKey.push_back('|');
            dedupeKey.append(propertyPath);
            dedupeKey.push_back('|');
            dedupeKey.append(replacementValue);
            if (seenMigrationKeys.contains(dedupeKey)) {
                return;
            }
            seenMigrationKeys.emplace(std::move(dedupeKey));

            FieldMigrationRecord record{};
            record.AssetKind = entry.AssetKind;
            record.TransformKind = transformKind;
            record.AssetId = entry.AssetId;
            record.StableFieldKey = entry.StableFieldKey;
            record.DomainPair = entry.DomainPair;
            record.TargetFieldId = entry.FieldId;
            record.PropertyPath = propertyPath;
            record.ExistingValue = existingValue;
            record.ReplacementValue = replacementValue;
            record.GraphNormalizationAction = graphNormalizationAction;
            record.Provenance = entry.Provenance;
            record.Provenance.RemediationBatchId = request.RemediationBatchId;
            record.Rollback.RollbackRequired = true;
            record.Rollback.RollbackPropertyPath = propertyPath;
            record.Rollback.RollbackValue = existingValue;
            record.Rollback.RollbackManifestPath = request.RollbackManifestPath.generic_string();
            record.Rollback.RollbackCheckpointId =
                HashToHex(HashString(request.RemediationBatchId + "|" + entry.AssetId + "|" + entry.FieldId + "|" +
                                     propertyPath + "|" + existingValue + "|" + replacementValue));
            record.MigrationId = BuildMigrationId(record);
            record.DeterministicDigest = ComputeMigrationRecordDigest(record);
            migrationRecords.push_back(std::move(record));
        };

        if (!entry.ExpectedBindingKey.empty() && entry.BindingKey != entry.ExpectedBindingKey) {
            emitMigration(FieldMigrationTransformKind::BindingKeyPathNormalization,
                          "ui.binding.key",
                          AsExistingValue(entry.BindingKey),
                          entry.ExpectedBindingKey,
                          "repair-binding-key-drift");
        }

        if (!entry.ExpectedBindingPath.empty() && entry.BindingPath != entry.ExpectedBindingPath) {
            emitMigration(FieldMigrationTransformKind::BindingKeyPathNormalization,
                          "ui.binding.path",
                          AsExistingValue(entry.BindingPath),
                          entry.ExpectedBindingPath,
                          "repair-binding-path-drift");
        }

        if (!entry.ExpectedLocalizationKey.empty() && entry.LocalizationKey != entry.ExpectedLocalizationKey) {
            emitMigration(FieldMigrationTransformKind::LocalizationReferenceNormalization,
                          "ui.localization.key",
                          AsExistingValue(entry.LocalizationKey),
                          entry.ExpectedLocalizationKey,
                          "repair-localization-key-drift");
        }

        if (!entry.ExpectedLocalizationReference.empty() &&
            entry.LocalizationReference != entry.ExpectedLocalizationReference) {
            emitMigration(FieldMigrationTransformKind::LocalizationReferenceNormalization,
                          "ui.localization.reference",
                          AsExistingValue(entry.LocalizationReference),
                          entry.ExpectedLocalizationReference,
                          "repair-localization-reference-drift");
        }

        if (!entry.ExpectedLocaleSchemaVersion.empty() &&
            entry.LocaleSchemaVersion != entry.ExpectedLocaleSchemaVersion) {
            std::string propertyPath = "localization.locale-schema.version";
            if (!entry.LocaleCode.empty()) {
                propertyPath.append("[");
                propertyPath.append(entry.LocaleCode);
                propertyPath.append("]");
            }

            emitMigration(FieldMigrationTransformKind::LocaleSchemaNormalization,
                          propertyPath,
                          AsExistingValue(entry.LocaleSchemaVersion),
                          entry.ExpectedLocaleSchemaVersion,
                          "repair-locale-schema-drift");
        }

        if (!entry.WidgetPresentationMode.empty() && !entry.WidgetMetadataSpace.empty() &&
            NormalizeToken(entry.WidgetPresentationMode) != NormalizeToken(entry.WidgetMetadataSpace)) {
            emitMigration(FieldMigrationTransformKind::WidgetMetadataNormalization,
                          "ui.widget.metadata.space",
                          entry.WidgetMetadataSpace,
                          entry.WidgetPresentationMode,
                          "repair-modal-world-widget-metadata-drift");
        }
    }

    SortMigrationRecords(migrationRecords);

    FieldMigrationResult result{};
    result.Scope = request.Scope;
    result.OutputDirectory = request.OutputDirectory;
    result.RemediationBatchId = request.RemediationBatchId;
    result.RollbackManifestPath = request.RollbackManifestPath;
    result.MigrationRecords = std::move(migrationRecords);
    for (const FieldMigrationRecord& record : result.MigrationRecords) {
        if (record.AssetKind == FieldMigrationAssetKind::UIWidget) {
            ++result.Summary.UIWidgetMigrationCount;
        } else if (record.AssetKind == FieldMigrationAssetKind::LocalizationTable) {
            ++result.Summary.LocalizationTableMigrationCount;
        }

        if (record.TransformKind == FieldMigrationTransformKind::BindingKeyPathNormalization) {
            ++result.Summary.BindingNormalizationCount;
        } else if (record.TransformKind == FieldMigrationTransformKind::LocalizationReferenceNormalization) {
            ++result.Summary.LocalizationNormalizationCount;
        } else if (record.TransformKind == FieldMigrationTransformKind::LocaleSchemaNormalization) {
            ++result.Summary.LocaleSchemaNormalizationCount;
        } else if (record.TransformKind == FieldMigrationTransformKind::WidgetMetadataNormalization) {
            ++result.Summary.WidgetMetadataNormalizationCount;
        }

        if (record.Rollback.RollbackRequired) {
            ++result.Summary.RollbackSafeMigrationCount;
        }
    }
    result.Summary.TotalMigrationCount = static_cast<uint32_t>(result.MigrationRecords.size());
    result.RollbackManifestDigest = ComputeRollbackManifestDigest(result);
    result.DeterministicDigest = ComputeResultDigest(result);
    return Result<FieldMigrationResult>::Success(std::move(result));
}

Result<FieldMigrationResult> MigrateAddressableBundleAndBuildManifestFieldData(const FieldMigrationRequest& request) {
    if (request.Scope.empty() || request.OutputDirectory.empty() || request.RemediationBatchId.empty() ||
        request.RollbackManifestPath.empty() || request.Entries.empty()) {
        return Result<FieldMigrationResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (request.Scope != "addressable-bundle-build-manifest-data") {
        return Result<FieldMigrationResult>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    for (const FieldMigrationEntry& entry : request.Entries) {
        if (!IsValidEntry(entry)) {
            return Result<FieldMigrationResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
        }
    }

    if (!EnsureOutputDirectory(request.OutputDirectory)) {
        return Result<FieldMigrationResult>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    std::vector<FieldMigrationRecord> migrationRecords;
    std::set<std::string> seenMigrationKeys;
    for (const FieldMigrationEntry& entry : request.Entries) {
        auto emitMigration = [&](const FieldMigrationTransformKind transformKind,
                                 const std::string& propertyPath,
                                 const std::string& existingValue,
                                 const std::string& replacementValue,
                                 const std::string& graphNormalizationAction) {
            std::string dedupeKey;
            dedupeKey.reserve(384u);
            dedupeKey.append(entry.Provenance.FindingId);
            dedupeKey.push_back('|');
            dedupeKey.append(entry.AssetId);
            dedupeKey.push_back('|');
            dedupeKey.append(entry.FieldId);
            dedupeKey.push_back('|');
            dedupeKey.append(std::to_string(static_cast<uint32_t>(transformKind)));
            dedupeKey.push_back('|');
            dedupeKey.append(propertyPath);
            dedupeKey.push_back('|');
            dedupeKey.append(replacementValue);
            if (seenMigrationKeys.contains(dedupeKey)) {
                return;
            }
            seenMigrationKeys.emplace(std::move(dedupeKey));

            FieldMigrationRecord record{};
            record.AssetKind = entry.AssetKind;
            record.TransformKind = transformKind;
            record.AssetId = entry.AssetId;
            record.StableFieldKey = entry.StableFieldKey;
            record.DomainPair = entry.DomainPair;
            record.TargetFieldId = entry.FieldId;
            record.PropertyPath = propertyPath;
            record.ExistingValue = existingValue;
            record.ReplacementValue = replacementValue;
            record.GraphNormalizationAction = graphNormalizationAction;
            record.Provenance = entry.Provenance;
            record.Provenance.RemediationBatchId = request.RemediationBatchId;
            record.Rollback.RollbackRequired = true;
            record.Rollback.RollbackPropertyPath = propertyPath;
            record.Rollback.RollbackValue = existingValue;
            record.Rollback.RollbackManifestPath = request.RollbackManifestPath.generic_string();
            record.Rollback.RollbackCheckpointId =
                HashToHex(HashString(request.RemediationBatchId + "|" + entry.AssetId + "|" + entry.FieldId + "|" +
                                     propertyPath + "|" + existingValue + "|" + replacementValue));
            record.MigrationId = BuildMigrationId(record);
            record.DeterministicDigest = ComputeMigrationRecordDigest(record);
            migrationRecords.push_back(std::move(record));
        };

        if (!entry.ExpectedCatalogAssetKey.empty() && entry.CatalogAssetKey != entry.ExpectedCatalogAssetKey) {
            emitMigration(FieldMigrationTransformKind::CatalogParityNormalization,
                          "addressables.catalog.asset-key",
                          AsExistingValue(entry.CatalogAssetKey),
                          entry.ExpectedCatalogAssetKey,
                          "repair-catalog-asset-key-drift");
        }

        if (!entry.ExpectedCatalogAddress.empty() && entry.CatalogAddress != entry.ExpectedCatalogAddress) {
            emitMigration(FieldMigrationTransformKind::CatalogParityNormalization,
                          "addressables.catalog.address",
                          AsExistingValue(entry.CatalogAddress),
                          entry.ExpectedCatalogAddress,
                          "repair-catalog-address-drift");
        }

        if (!entry.ExpectedManifestCatalogKey.empty() && entry.ManifestCatalogKey != entry.ExpectedManifestCatalogKey) {
            emitMigration(FieldMigrationTransformKind::CatalogParityNormalization,
                          "build.manifest.catalog-link.catalog-key",
                          AsExistingValue(entry.ManifestCatalogKey),
                          entry.ExpectedManifestCatalogKey,
                          "repair-manifest-catalog-linkage-drift");
        }

        if (!entry.ExpectedBundleId.empty() && entry.BundleId != entry.ExpectedBundleId) {
            emitMigration(FieldMigrationTransformKind::BundleParityNormalization,
                          "addressables.bundle.id",
                          AsExistingValue(entry.BundleId),
                          entry.ExpectedBundleId,
                          "repair-bundle-id-drift");
        }

        if (!entry.ExpectedBundleHash.empty() && entry.BundleHash != entry.ExpectedBundleHash) {
            emitMigration(FieldMigrationTransformKind::BundleParityNormalization,
                          "addressables.bundle.hash",
                          AsExistingValue(entry.BundleHash),
                          entry.ExpectedBundleHash,
                          "repair-bundle-hash-drift");
        }

        if (!entry.ExpectedBundleReference.empty() && entry.BundleReference != entry.ExpectedBundleReference) {
            emitMigration(FieldMigrationTransformKind::BundleParityNormalization,
                          "addressables.bundle.reference",
                          AsExistingValue(entry.BundleReference),
                          entry.ExpectedBundleReference,
                          "repair-bundle-reference-drift");
        }

        if (!entry.ExpectedManifestBundleId.empty() && entry.ManifestBundleId != entry.ExpectedManifestBundleId) {
            emitMigration(FieldMigrationTransformKind::ManifestLinkageNormalization,
                          "build.manifest.bundle-link.bundle-id",
                          AsExistingValue(entry.ManifestBundleId),
                          entry.ExpectedManifestBundleId,
                          "repair-manifest-bundle-linkage-drift");
        }

        if (!entry.ExpectedBuildProfile.empty() && entry.BuildProfile != entry.ExpectedBuildProfile) {
            emitMigration(FieldMigrationTransformKind::BuildProfileTargetNormalization,
                          "build.profile",
                          AsExistingValue(entry.BuildProfile),
                          entry.ExpectedBuildProfile,
                          "repair-build-profile-drift");
        }

        if (!entry.ExpectedBuildTarget.empty() && entry.BuildTarget != entry.ExpectedBuildTarget) {
            emitMigration(FieldMigrationTransformKind::BuildProfileTargetNormalization,
                          "build.target",
                          AsExistingValue(entry.BuildTarget),
                          entry.ExpectedBuildTarget,
                          "repair-build-target-drift");
        }
    }

    SortMigrationRecords(migrationRecords);

    FieldMigrationResult result{};
    result.Scope = request.Scope;
    result.OutputDirectory = request.OutputDirectory;
    result.RemediationBatchId = request.RemediationBatchId;
    result.RollbackManifestPath = request.RollbackManifestPath;
    result.MigrationRecords = std::move(migrationRecords);

    for (const FieldMigrationRecord& record : result.MigrationRecords) {
        if (record.AssetKind == FieldMigrationAssetKind::AddressableCatalog) {
            ++result.Summary.AddressableCatalogMigrationCount;
        } else if (record.AssetKind == FieldMigrationAssetKind::AssetBundle) {
            ++result.Summary.AssetBundleMigrationCount;
        } else if (record.AssetKind == FieldMigrationAssetKind::BuildManifest) {
            ++result.Summary.BuildManifestMigrationCount;
        }

        if (record.TransformKind == FieldMigrationTransformKind::CatalogParityNormalization) {
            ++result.Summary.CatalogParityNormalizationCount;
        } else if (record.TransformKind == FieldMigrationTransformKind::BundleParityNormalization) {
            ++result.Summary.BundleParityNormalizationCount;
        } else if (record.TransformKind == FieldMigrationTransformKind::BuildProfileTargetNormalization) {
            ++result.Summary.BuildProfileTargetNormalizationCount;
        } else if (record.TransformKind == FieldMigrationTransformKind::ManifestLinkageNormalization) {
            ++result.Summary.ManifestLinkageNormalizationCount;
        }

        if (record.Rollback.RollbackRequired) {
            ++result.Summary.RollbackSafeMigrationCount;
        }
    }
    result.Summary.TotalMigrationCount = static_cast<uint32_t>(result.MigrationRecords.size());
    result.RollbackManifestDigest = ComputeRollbackManifestDigest(result);
    result.DeterministicDigest = ComputeResultDigest(result);
    return Result<FieldMigrationResult>::Success(std::move(result));
}

Result<FieldMigrationResult> RepairPlayerSaveReplayAndAutomationFieldData(const FieldMigrationRequest& request) {
    if (request.Scope.empty() || request.OutputDirectory.empty() || request.RemediationBatchId.empty() ||
        request.RollbackManifestPath.empty() || request.Entries.empty()) {
        return Result<FieldMigrationResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (request.Scope != "player-save-replay-automation-data") {
        return Result<FieldMigrationResult>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    for (const FieldMigrationEntry& entry : request.Entries) {
        if (!IsValidEntry(entry)) {
            return Result<FieldMigrationResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
        }
    }

    if (!EnsureOutputDirectory(request.OutputDirectory)) {
        return Result<FieldMigrationResult>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    std::vector<FieldMigrationRecord> migrationRecords;
    std::set<std::string> seenMigrationKeys;
    for (const FieldMigrationEntry& entry : request.Entries) {
        auto emitMigration = [&](const FieldMigrationTransformKind transformKind,
                                 const std::string& propertyPath,
                                 const std::string& existingValue,
                                 const std::string& replacementValue,
                                 const std::string& graphNormalizationAction) {
            std::string dedupeKey;
            dedupeKey.reserve(384u);
            dedupeKey.append(entry.Provenance.FindingId);
            dedupeKey.push_back('|');
            dedupeKey.append(entry.AssetId);
            dedupeKey.push_back('|');
            dedupeKey.append(entry.FieldId);
            dedupeKey.push_back('|');
            dedupeKey.append(std::to_string(static_cast<uint32_t>(transformKind)));
            dedupeKey.push_back('|');
            dedupeKey.append(propertyPath);
            dedupeKey.push_back('|');
            dedupeKey.append(replacementValue);
            if (seenMigrationKeys.contains(dedupeKey)) {
                return;
            }
            seenMigrationKeys.emplace(std::move(dedupeKey));

            FieldMigrationRecord record{};
            record.AssetKind = entry.AssetKind;
            record.TransformKind = transformKind;
            record.AssetId = entry.AssetId;
            record.StableFieldKey = entry.StableFieldKey;
            record.DomainPair = entry.DomainPair;
            record.TargetFieldId = entry.FieldId;
            record.PropertyPath = propertyPath;
            record.ExistingValue = existingValue;
            record.ReplacementValue = replacementValue;
            record.GraphNormalizationAction = graphNormalizationAction;
            record.Provenance = entry.Provenance;
            record.Provenance.RemediationBatchId = request.RemediationBatchId;
            record.Rollback.RollbackRequired = true;
            record.Rollback.RollbackPropertyPath = propertyPath;
            record.Rollback.RollbackValue = existingValue;
            record.Rollback.RollbackManifestPath = request.RollbackManifestPath.generic_string();
            record.Rollback.RollbackCheckpointId =
                HashToHex(HashString(request.RemediationBatchId + "|" + entry.AssetId + "|" + entry.FieldId + "|" +
                                     propertyPath + "|" + existingValue + "|" + replacementValue));
            record.MigrationId = BuildMigrationId(record);
            record.DeterministicDigest = ComputeMigrationRecordDigest(record);
            migrationRecords.push_back(std::move(record));
        };

        if (!entry.ExpectedSaveSchemaVersion.empty() && entry.SaveSchemaVersion != entry.ExpectedSaveSchemaVersion) {
            emitMigration(FieldMigrationTransformKind::SaveCompatibilityNormalization,
                          "save.schema.version",
                          AsExistingValue(entry.SaveSchemaVersion),
                          entry.ExpectedSaveSchemaVersion,
                          "repair-save-schema-version-drift");
        }

        if (!entry.ExpectedSaveCompatibilityPath.empty() &&
            entry.SaveCompatibilityPath != entry.ExpectedSaveCompatibilityPath) {
            emitMigration(FieldMigrationTransformKind::SaveCompatibilityNormalization,
                          "save.compatibility.path",
                          AsExistingValue(entry.SaveCompatibilityPath),
                          entry.ExpectedSaveCompatibilityPath,
                          "repair-save-compatibility-path-drift");
        }

        if (!entry.ExpectedReplaySchemaVersion.empty() && entry.ReplaySchemaVersion != entry.ExpectedReplaySchemaVersion) {
            emitMigration(FieldMigrationTransformKind::ReplayDeterminismNormalization,
                          "replay.schema.version",
                          AsExistingValue(entry.ReplaySchemaVersion),
                          entry.ExpectedReplaySchemaVersion,
                          "repair-replay-schema-version-drift");
        }

        if (!entry.ExpectedReplayDeterminismVersion.empty() &&
            entry.ReplayDeterminismVersion != entry.ExpectedReplayDeterminismVersion) {
            emitMigration(FieldMigrationTransformKind::ReplayDeterminismNormalization,
                          "replay.determinism.version",
                          AsExistingValue(entry.ReplayDeterminismVersion),
                          entry.ExpectedReplayDeterminismVersion,
                          "repair-replay-determinism-drift");
        }

        if (!entry.ExpectedAutomationBaselineId.empty() && entry.AutomationBaselineId != entry.ExpectedAutomationBaselineId) {
            emitMigration(FieldMigrationTransformKind::AutomationBaselineNormalization,
                          "automation.baseline.id",
                          AsExistingValue(entry.AutomationBaselineId),
                          entry.ExpectedAutomationBaselineId,
                          "repair-automation-baseline-id-drift");
        }

        if (!entry.ExpectedAutomationSeed.empty() && entry.AutomationSeed != entry.ExpectedAutomationSeed) {
            emitMigration(FieldMigrationTransformKind::AutomationBaselineNormalization,
                          "automation.seed",
                          AsExistingValue(entry.AutomationSeed),
                          entry.ExpectedAutomationSeed,
                          "repair-automation-seed-drift");
        }

        if (!entry.ExpectedAutomationDeterminismBaseline.empty() &&
            entry.AutomationDeterminismBaseline != entry.ExpectedAutomationDeterminismBaseline) {
            emitMigration(FieldMigrationTransformKind::AutomationBaselineNormalization,
                          "automation.determinism.baseline",
                          AsExistingValue(entry.AutomationDeterminismBaseline),
                          entry.ExpectedAutomationDeterminismBaseline,
                          "repair-automation-determinism-baseline-drift");
        }
    }

    SortMigrationRecords(migrationRecords);

    FieldMigrationResult result{};
    result.Scope = request.Scope;
    result.OutputDirectory = request.OutputDirectory;
    result.RemediationBatchId = request.RemediationBatchId;
    result.RollbackManifestPath = request.RollbackManifestPath;
    result.MigrationRecords = std::move(migrationRecords);

    for (const FieldMigrationRecord& record : result.MigrationRecords) {
        if (record.AssetKind == FieldMigrationAssetKind::PlayerSave) {
            ++result.Summary.PlayerSaveMigrationCount;
        } else if (record.AssetKind == FieldMigrationAssetKind::ReplayData) {
            ++result.Summary.ReplayDataMigrationCount;
        } else if (record.AssetKind == FieldMigrationAssetKind::AutomationBaseline) {
            ++result.Summary.AutomationBaselineMigrationCount;
        }

        if (record.TransformKind == FieldMigrationTransformKind::SaveCompatibilityNormalization) {
            ++result.Summary.SaveCompatibilityNormalizationCount;
        } else if (record.TransformKind == FieldMigrationTransformKind::ReplayDeterminismNormalization) {
            ++result.Summary.ReplayDeterminismNormalizationCount;
        } else if (record.TransformKind == FieldMigrationTransformKind::AutomationBaselineNormalization) {
            ++result.Summary.AutomationBaselineNormalizationCount;
        }

        if (record.Rollback.RollbackRequired) {
            ++result.Summary.RollbackSafeMigrationCount;
        }
    }

    result.Summary.TotalMigrationCount = static_cast<uint32_t>(result.MigrationRecords.size());
    result.RollbackManifestDigest = ComputeRollbackManifestDigest(result);
    result.DeterministicDigest = ComputeResultDigest(result);
    return Result<FieldMigrationResult>::Success(std::move(result));
}

} // namespace Core::Remediation
