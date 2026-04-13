#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Core {
namespace UI {
namespace Authoring {

    using Json = nlohmann::json;

    constexpr uint32_t WIDGET_LAYOUT_SCHEMA_VERSION = 1;

    constexpr const char* UI_WIDGET_LAYOUT_INVALID_SCHEMA = "UI_WIDGET_LAYOUT_INVALID_SCHEMA";
    constexpr const char* UI_WIDGET_LAYOUT_INVALID_FIELD = "UI_WIDGET_LAYOUT_INVALID_FIELD";
    constexpr const char* UI_WIDGET_LAYOUT_DUPLICATE_NODE = "UI_WIDGET_LAYOUT_DUPLICATE_NODE";
    constexpr const char* UI_WIDGET_LAYOUT_UNKNOWN_PARENT = "UI_WIDGET_LAYOUT_UNKNOWN_PARENT";
    constexpr const char* UI_WIDGET_LAYOUT_PARSE_ERROR = "UI_WIDGET_LAYOUT_PARSE_ERROR";

    enum class WidgetInstanceResetPolicy : uint8_t {
        ResetToLayoutDefaults = 0,
        PreserveRuntimeState
    };

    struct WidgetPoolingHint {
        uint32_t PrewarmCount = 0;
        WidgetInstanceResetPolicy ResetPolicy = WidgetInstanceResetPolicy::ResetToLayoutDefaults;
    };

    struct WidgetLayoutNode {
        std::string NodeId;
        std::string ParentNodeId;
        std::string BlueprintId;
        int32_t Order = 0;
        std::string ModalAnchor;
        Json PropertyOverrides = Json::object();
        std::vector<std::string> StyleDependencyIds;
        WidgetPoolingHint Pooling;
    };

    struct WidgetLayoutAsset {
        std::string LayoutId;
        uint32_t SchemaVersion = WIDGET_LAYOUT_SCHEMA_VERSION;
        std::vector<WidgetLayoutNode> Nodes;
    };

    struct WidgetLayoutValidationResult {
        bool Valid = false;
        std::string ErrorCode;
        std::string Message;
        std::vector<std::string> Diagnostics;
    };

    struct WidgetLayoutDependencySet {
        std::vector<std::string> BlueprintDependencies;
        std::vector<std::string> StyleDependencies;
    };

    Json SerializeWidgetLayoutAsset(const WidgetLayoutAsset& asset);
    std::optional<WidgetLayoutAsset> DeserializeWidgetLayoutAsset(
        const Json& payload,
        WidgetLayoutValidationResult* validation = nullptr);
    WidgetLayoutValidationResult ValidateWidgetLayoutAsset(const WidgetLayoutAsset& asset);
    WidgetLayoutDependencySet CollectWidgetLayoutDependencies(const WidgetLayoutAsset& asset);

    void SortLayoutNodesDeterministically(std::vector<WidgetLayoutNode>& nodes);
    const char* ToString(WidgetInstanceResetPolicy policy);
    WidgetInstanceResetPolicy ParseWidgetInstanceResetPolicy(const std::string& policy);

} // namespace Authoring
} // namespace UI
} // namespace Core

