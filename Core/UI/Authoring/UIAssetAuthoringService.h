#pragma once

#include "Core/UI/Authoring/WidgetBlueprintAsset.h"
#include "Core/UI/Authoring/WidgetLayoutAsset.h"

#include <filesystem>
#include <functional>
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
    constexpr const char* UI_ASSET_AUTHORING_LAYOUT_PARSE_FAILED = "UI_ASSET_AUTHORING_LAYOUT_PARSE_FAILED";
    constexpr const char* UI_ASSET_AUTHORING_LAYOUT_DEPENDENCY_MISSING = "UI_ASSET_AUTHORING_LAYOUT_DEPENDENCY_MISSING";
    constexpr const char* UI_ASSET_AUTHORING_LAYOUT_POOL_EMPTY = "UI_ASSET_AUTHORING_LAYOUT_POOL_EMPTY";

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

    struct LoadWidgetLayoutAssetRequest {
        std::filesystem::path LayoutSourcePath;
        bool BuildPoolOnLoad = true;
        std::function<void(const struct LoadWidgetLayoutAssetResult&)> CompletionCallback;
    };

    struct LayoutWidgetInstanceHandle {
        std::string LayoutId;
        std::string NodeId;
        uint64_t InstanceId = 0;
    };

    struct LoadWidgetLayoutAssetResult {
        bool Success = false;
        std::string ErrorCode;
        std::string Message;
        WidgetLayoutAsset Layout;
        std::vector<std::string> ResolvedBlueprintIds;
        std::vector<std::string> MissingDependencies;
        std::vector<std::string> Diagnostics;
    };

    class UIAssetAuthoringService {
    public:
        static UIAssetAuthoringService& Get();

        CreateWidgetBlueprintAssetResult CreateWidgetBlueprintAsset(
            const CreateWidgetBlueprintAssetRequest& request);
        LoadWidgetLayoutAssetResult LoadWidgetLayoutAsset(const LoadWidgetLayoutAssetRequest& request);

        std::optional<WidgetBlueprintAsset> FindBlueprintById(const std::string& blueprintId) const;
        std::optional<WidgetLayoutAsset> FindLayoutById(const std::string& layoutId) const;
        size_t GetKnownBlueprintCount() const { return m_KnownBlueprints.size(); }
        size_t GetKnownLayoutCount() const { return m_KnownLayouts.size(); }

        std::optional<LayoutWidgetInstanceHandle> CheckoutLayoutInstance(
            const std::string& layoutId,
            const std::string& nodeId);
        bool CheckinLayoutInstance(const LayoutWidgetInstanceHandle& handle);

    private:
        struct LayoutWidgetInstance {
            uint64_t InstanceId = 0;
            std::string LayoutId;
            std::string NodeId;
            Json RuntimeProperties = Json::object();
            WidgetInstanceResetPolicy ResetPolicy = WidgetInstanceResetPolicy::ResetToLayoutDefaults;
            bool InUse = false;
        };

        bool BuildLayoutPool(const WidgetLayoutAsset& layout, std::vector<std::string>* diagnostics);
        std::optional<WidgetBlueprintAsset> ResolveBlueprintForLayoutNode(
            const WidgetLayoutNode& node,
            std::vector<std::string>* diagnostics) const;

        UIAssetAuthoringService() = default;

        std::unordered_map<std::string, WidgetBlueprintAsset> m_KnownBlueprints;
        std::unordered_map<std::string, WidgetLayoutAsset> m_KnownLayouts;
        std::unordered_map<std::string, std::vector<LayoutWidgetInstance>> m_LayoutPools;
        uint64_t m_NextInstanceId = 1;
    };

} // namespace Authoring
} // namespace UI
} // namespace Core

