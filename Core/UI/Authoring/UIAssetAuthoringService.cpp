#include "Core/UI/Authoring/UIAssetAuthoringService.h"

#include "Core/Security/PathValidator.h"

#include <chrono>
#include <fstream>

namespace Core {
namespace UI {
namespace Authoring {
namespace {

    std::filesystem::path ResolveCookedMetadataPath(
        const std::filesystem::path& sourcePath,
        const std::filesystem::path& explicitPath) {
        if (!explicitPath.empty()) {
            return explicitPath;
        }
        return sourcePath.parent_path() /
               (sourcePath.stem().string() + ".widgetbp.meta.json");
    }

    std::filesystem::path ResolveManifestPath(
        const std::filesystem::path& sourcePath,
        const std::filesystem::path& explicitPath) {
        if (!explicitPath.empty()) {
            return explicitPath;
        }
        return sourcePath.parent_path() / "widget_blueprint_manifest.json";
    }

    bool WriteJsonDocument(const std::filesystem::path& path, const Json& document, std::string* errorMessage) {
        std::error_code ec;
        const std::filesystem::path parentPath = path.parent_path();
        if (!parentPath.empty()) {
            std::filesystem::create_directories(parentPath, ec);
            if (ec) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Failed to create directory for " + path.string();
                }
                return false;
            }
        }

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Failed to open file for writing: " + path.string();
            }
            return false;
        }

        output << document.dump(2);
        if (!output.good()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Failed to write JSON document: " + path.string();
            }
            return false;
        }

        return true;
    }

    bool LoadManifest(const std::filesystem::path& path, Json& manifest, std::string* errorMessage) {
        manifest = Json::object();
        manifest["schemaVersion"] = WIDGET_BLUEPRINT_SCHEMA_VERSION;
        manifest["assetType"] = "widget_blueprint_manifest";
        manifest["entries"] = Json::array();

        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || ec) {
            return true;
        }

        std::ifstream input(path, std::ios::binary);
        if (!input.is_open()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Failed to open widget blueprint manifest: " + path.string();
            }
            return false;
        }

        try {
            input >> manifest;
        } catch (const nlohmann::json::parse_error&) {
            if (errorMessage != nullptr) {
                *errorMessage = "Manifest is not valid JSON: " + path.string();
            }
            return false;
        }

        if (!manifest.is_object()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Manifest root must be an object: " + path.string();
            }
            return false;
        }

        if (!manifest.contains("entries")) {
            manifest["entries"] = Json::array();
            return true;
        }

        if (!manifest["entries"].is_array()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Manifest entries must be an array: " + path.string();
            }
            return false;
        }

        return true;
    }

    Json BuildManifestEntry(
        const WidgetBlueprintAsset& blueprint,
        uint64_t sourceHash,
        const std::filesystem::path& sourcePath,
        const std::filesystem::path& cookedMetadataPath) {

        const auto now = std::chrono::system_clock::now();
        const uint64_t authoredTimestampSeconds = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());

        return {
            {"blueprintId", blueprint.BlueprintId},
            {"schemaVersion", blueprint.SchemaVersion},
            {"parentBlueprintId", blueprint.ParentBlueprintId},
            {"widgetType", blueprint.WidgetType},
            {"sourcePath", sourcePath.string()},
            {"cookedMetadataPath", cookedMetadataPath.string()},
            {"sourceHash", sourceHash},
            {"authoredTimestamp", authoredTimestampSeconds}
        };
    }

} // namespace

    UIAssetAuthoringService& UIAssetAuthoringService::Get() {
        static UIAssetAuthoringService instance;
        return instance;
    }

    CreateWidgetBlueprintAssetResult UIAssetAuthoringService::CreateWidgetBlueprintAsset(
        const CreateWidgetBlueprintAssetRequest& request) {
        CreateWidgetBlueprintAssetResult result;
        result.Success = false;

        if (request.SourceBlueprintPath.empty()) {
            result.ErrorCode = UI_ASSET_AUTHORING_INVALID_REQUEST;
            result.Message = "SourceBlueprintPath cannot be empty.";
            return result;
        }

        WidgetBlueprintValidationResult validation = ValidateWidgetBlueprintAsset(request.Blueprint);
        if (!validation.Valid) {
            result.ErrorCode = validation.ErrorCode;
            result.Message = validation.Message;
            result.Diagnostics = validation.Diagnostics;
            return result;
        }

        const std::optional<std::filesystem::path> validatedSourcePath =
            Security::PathValidator::ValidateAssetPath(request.SourceBlueprintPath);
        if (!validatedSourcePath.has_value()) {
            result.ErrorCode = UI_ASSET_AUTHORING_PATH_VALIDATION_FAILED;
            result.Message = "Source blueprint path validation failed.";
            return result;
        }
        result.SourceBlueprintPath = validatedSourcePath.value();

        const std::filesystem::path requestedCookedPath = ResolveCookedMetadataPath(
            result.SourceBlueprintPath,
            request.CookedMetadataPath);
        const std::optional<std::filesystem::path> validatedCookedPath =
            Security::PathValidator::ValidateCookedPath(requestedCookedPath);
        if (!validatedCookedPath.has_value()) {
            result.ErrorCode = UI_ASSET_AUTHORING_PATH_VALIDATION_FAILED;
            result.Message = "Cooked metadata path validation failed.";
            return result;
        }
        result.CookedMetadataPath = validatedCookedPath.value();

        const std::filesystem::path requestedManifestPath = ResolveManifestPath(
            result.SourceBlueprintPath,
            request.ManifestPath);
        const std::optional<std::filesystem::path> validatedManifestPath =
            Security::PathValidator::ValidateAssetPath(requestedManifestPath);
        if (!validatedManifestPath.has_value()) {
            result.ErrorCode = UI_ASSET_AUTHORING_PATH_VALIDATION_FAILED;
            result.Message = "Manifest path validation failed.";
            return result;
        }
        result.ManifestPath = validatedManifestPath.value();

        std::error_code ec;
        if (!request.OverwriteExisting && std::filesystem::exists(result.SourceBlueprintPath, ec) && !ec) {
            result.ErrorCode = UI_ASSET_AUTHORING_SOURCE_EXISTS;
            result.Message = "Widget blueprint source already exists and overwrite is disabled.";
            return result;
        }

        std::string loopBlueprintId;
        if (DetectWidgetBlueprintInheritanceLoop(request.Blueprint, m_KnownBlueprints, &loopBlueprintId)) {
            result.ErrorCode = UI_ASSET_AUTHORING_INHERITANCE_LOOP;
            result.Message = "Widget blueprint inheritance loop detected at '" + loopBlueprintId + "'.";
            return result;
        }

        if (!request.Blueprint.ParentBlueprintId.empty() &&
            m_KnownBlueprints.find(request.Blueprint.ParentBlueprintId) == m_KnownBlueprints.end()) {
            result.Diagnostics.push_back(
                "Unresolved parentBlueprintId '" + request.Blueprint.ParentBlueprintId + "'.");
        }

        const Json sourcePayload = request.SourcePayload.has_value()
            ? request.SourcePayload.value()
            : SerializeWidgetBlueprintAsset(request.Blueprint);
        std::vector<std::string> deprecatedFields = CollectDeprecatedWidgetBlueprintFields(sourcePayload);
        for (const std::string& deprecatedField : deprecatedFields) {
            result.Diagnostics.push_back("Deprecated blueprint field: " + deprecatedField);
        }

        const Json canonicalBlueprint = SerializeWidgetBlueprintAsset(request.Blueprint);
        result.DeterministicSourceHash = ComputeWidgetBlueprintDeterministicHash(request.Blueprint);

        const WidgetBlueprintCookedMetadata metadata = BuildWidgetBlueprintCookedMetadata(
            request.Blueprint,
            result.DeterministicSourceHash);
        const Json cookedMetadataPayload = SerializeWidgetBlueprintCookedMetadata(metadata);

        std::string writeError;
        if (!WriteJsonDocument(result.SourceBlueprintPath, canonicalBlueprint, &writeError)) {
            result.ErrorCode = UI_ASSET_AUTHORING_WRITE_FAILED;
            result.Message = writeError;
            return result;
        }

        if (!WriteJsonDocument(result.CookedMetadataPath, cookedMetadataPayload, &writeError)) {
            result.ErrorCode = UI_ASSET_AUTHORING_WRITE_FAILED;
            result.Message = writeError;
            return result;
        }

        Json manifest;
        if (!LoadManifest(result.ManifestPath, manifest, &writeError)) {
            result.ErrorCode = UI_ASSET_AUTHORING_MANIFEST_INVALID;
            result.Message = writeError;
            return result;
        }

        Json& entries = manifest["entries"];
        for (auto entryIt = entries.begin(); entryIt != entries.end();) {
            if (entryIt->is_object() && entryIt->value("blueprintId", "") == request.Blueprint.BlueprintId) {
                entryIt = entries.erase(entryIt);
            } else {
                ++entryIt;
            }
        }

        result.ManifestEntry = BuildManifestEntry(
            request.Blueprint,
            result.DeterministicSourceHash,
            result.SourceBlueprintPath,
            result.CookedMetadataPath);
        entries.push_back(result.ManifestEntry);

        if (!WriteJsonDocument(result.ManifestPath, manifest, &writeError)) {
            result.ErrorCode = UI_ASSET_AUTHORING_WRITE_FAILED;
            result.Message = writeError;
            return result;
        }

        m_KnownBlueprints[request.Blueprint.BlueprintId] = request.Blueprint;

        result.Success = true;
        return result;
    }

    std::optional<WidgetBlueprintAsset> UIAssetAuthoringService::FindBlueprintById(
        const std::string& blueprintId) const {
        const auto blueprintIt = m_KnownBlueprints.find(blueprintId);
        if (blueprintIt == m_KnownBlueprints.end()) {
            return std::nullopt;
        }
        return blueprintIt->second;
    }

} // namespace Authoring
} // namespace UI
} // namespace Core

