#pragma once

#include "Core/UI/Authoring/WidgetBlueprintAsset.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Core {
namespace UI {
namespace Authoring {

    constexpr const char* UI_ASSET_AUTHORING_INVALID_REQUEST = "UI_ASSET_AUTHORING_INVALID_REQUEST";
    constexpr const char* UI_ASSET_AUTHORING_PATH_VALIDATION_FAILED = "UI_ASSET_AUTHORING_PATH_VALIDATION_FAILED";
    constexpr const char* UI_ASSET_AUTHORING_SOURCE_EXISTS = "UI_ASSET_AUTHORING_SOURCE_EXISTS";
    constexpr const char* UI_ASSET_AUTHORING_WRITE_FAILED = "UI_ASSET_AUTHORING_WRITE_FAILED";
    constexpr const char* UI_ASSET_AUTHORING_MANIFEST_INVALID = "UI_ASSET_AUTHORING_MANIFEST_INVALID";
    constexpr const char* UI_ASSET_AUTHORING_INHERITANCE_LOOP = "UI_ASSET_AUTHORING_INHERITANCE_LOOP";

    struct CreateWidgetBlueprintAssetRequest {
        WidgetBlueprintAsset Blueprint;
        std::filesystem::path SourceBlueprintPath;
        std::filesystem::path CookedMetadataPath;
        std::filesystem::path ManifestPath;
        std::optional<Json> SourcePayload;
        bool OverwriteExisting = false;
    };

    struct CreateWidgetBlueprintAssetResult {
        bool Success = false;
        std::string ErrorCode;
        std::string Message;
        uint64_t DeterministicSourceHash = 0;
        std::filesystem::path SourceBlueprintPath;
        std::filesystem::path CookedMetadataPath;
        std::filesystem::path ManifestPath;
        Json ManifestEntry = Json::object();
        std::vector<std::string> Diagnostics;
    };

    class UIAssetAuthoringService {
    public:
        static UIAssetAuthoringService& Get();

        CreateWidgetBlueprintAssetResult CreateWidgetBlueprintAsset(
            const CreateWidgetBlueprintAssetRequest& request);

        std::optional<WidgetBlueprintAsset> FindBlueprintById(const std::string& blueprintId) const;
        size_t GetKnownBlueprintCount() const { return m_KnownBlueprints.size(); }

    private:
        UIAssetAuthoringService() = default;

        std::unordered_map<std::string, WidgetBlueprintAsset> m_KnownBlueprints;
    };

} // namespace Authoring
} // namespace UI
} // namespace Core

