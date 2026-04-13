#include "Core/UI/Authoring/WidgetLayoutAsset.h"

#include <algorithm>
#include <unordered_set>

namespace Core {
namespace UI {
namespace Authoring {
namespace {

    Json SerializeNode(const WidgetLayoutNode& node) {
        Json styleDeps = Json::array();
        for (const std::string& styleDependency : node.StyleDependencyIds) {
            styleDeps.push_back(styleDependency);
        }

        return {
            {"nodeId", node.NodeId},
            {"parentNodeId", node.ParentNodeId},
            {"blueprintId", node.BlueprintId},
            {"order", node.Order},
            {"modalAnchor", node.ModalAnchor},
            {"propertyOverrides", node.PropertyOverrides},
            {"styleDependencies", std::move(styleDeps)},
            {"pooling", {
                {"prewarmCount", node.Pooling.PrewarmCount},
                {"resetPolicy", ToString(node.Pooling.ResetPolicy)}
            }}
        };
    }

    std::optional<WidgetLayoutNode> DeserializeNode(
        const Json& payload,
        WidgetLayoutValidationResult* validation) {

        if (!payload.is_object()) {
            if (validation != nullptr) {
                validation->Valid = false;
                validation->ErrorCode = UI_WIDGET_LAYOUT_INVALID_FIELD;
                validation->Message = "Layout nodes must be objects.";
            }
            return std::nullopt;
        }

        WidgetLayoutNode node;
        node.NodeId = payload.value("nodeId", "");
        node.ParentNodeId = payload.value("parentNodeId", "");
        node.BlueprintId = payload.value("blueprintId", "");
        node.Order = payload.value("order", 0);
        node.ModalAnchor = payload.value("modalAnchor", "");
        node.PropertyOverrides = payload.value("propertyOverrides", Json::object());

        const Json styleDepsPayload = payload.value("styleDependencies", Json::array());
        if (!styleDepsPayload.is_array()) {
            if (validation != nullptr) {
                validation->Valid = false;
                validation->ErrorCode = UI_WIDGET_LAYOUT_INVALID_FIELD;
                validation->Message = "styleDependencies must be an array.";
            }
            return std::nullopt;
        }
        for (const Json& styleDependency : styleDepsPayload) {
            if (!styleDependency.is_string()) {
                if (validation != nullptr) {
                    validation->Valid = false;
                    validation->ErrorCode = UI_WIDGET_LAYOUT_INVALID_FIELD;
                    validation->Message = "styleDependencies entries must be strings.";
                }
                return std::nullopt;
            }
            node.StyleDependencyIds.push_back(styleDependency.get<std::string>());
        }

        const Json poolingPayload = payload.value("pooling", Json::object());
        if (!poolingPayload.is_object()) {
            if (validation != nullptr) {
                validation->Valid = false;
                validation->ErrorCode = UI_WIDGET_LAYOUT_INVALID_FIELD;
                validation->Message = "pooling must be an object.";
            }
            return std::nullopt;
        }
        node.Pooling.PrewarmCount = poolingPayload.value("prewarmCount", 0U);
        node.Pooling.ResetPolicy = ParseWidgetInstanceResetPolicy(
            poolingPayload.value("resetPolicy", "reset_to_layout_defaults"));

        return node;
    }

} // namespace

    Json SerializeWidgetLayoutAsset(const WidgetLayoutAsset& asset) {
        Json nodes = Json::array();
        for (const WidgetLayoutNode& node : asset.Nodes) {
            nodes.push_back(SerializeNode(node));
        }

        return {
            {"layoutId", asset.LayoutId},
            {"schemaVersion", asset.SchemaVersion},
            {"nodes", std::move(nodes)}
        };
    }

    std::optional<WidgetLayoutAsset> DeserializeWidgetLayoutAsset(
        const Json& payload,
        WidgetLayoutValidationResult* validation) {

        WidgetLayoutValidationResult localValidation;
        WidgetLayoutValidationResult* validationOut = validation != nullptr ? validation : &localValidation;
        validationOut->Valid = false;

        if (!payload.is_object()) {
            validationOut->ErrorCode = UI_WIDGET_LAYOUT_PARSE_ERROR;
            validationOut->Message = "Widget layout payload must be an object.";
            return std::nullopt;
        }

        WidgetLayoutAsset asset;
        asset.LayoutId = payload.value("layoutId", "");
        asset.SchemaVersion = payload.value("schemaVersion", WIDGET_LAYOUT_SCHEMA_VERSION);

        const Json nodesPayload = payload.value("nodes", Json::array());
        if (!nodesPayload.is_array()) {
            validationOut->ErrorCode = UI_WIDGET_LAYOUT_INVALID_FIELD;
            validationOut->Message = "nodes must be an array.";
            return std::nullopt;
        }

        asset.Nodes.reserve(nodesPayload.size());
        for (const Json& nodePayload : nodesPayload) {
            std::optional<WidgetLayoutNode> node = DeserializeNode(nodePayload, validationOut);
            if (!node.has_value()) {
                return std::nullopt;
            }
            asset.Nodes.push_back(std::move(node.value()));
        }

        *validationOut = ValidateWidgetLayoutAsset(asset);
        if (!validationOut->Valid) {
            return std::nullopt;
        }

        SortLayoutNodesDeterministically(asset.Nodes);
        return asset;
    }

    WidgetLayoutValidationResult ValidateWidgetLayoutAsset(const WidgetLayoutAsset& asset) {
        WidgetLayoutValidationResult result;
        result.Valid = false;

        if (asset.LayoutId.empty()) {
            result.ErrorCode = UI_WIDGET_LAYOUT_INVALID_SCHEMA;
            result.Message = "layoutId is required.";
            return result;
        }

        if (asset.SchemaVersion != WIDGET_LAYOUT_SCHEMA_VERSION) {
            result.ErrorCode = UI_WIDGET_LAYOUT_INVALID_SCHEMA;
            result.Message = "Unsupported widget layout schemaVersion.";
            return result;
        }

        std::unordered_set<std::string> nodeIds;
        for (const WidgetLayoutNode& node : asset.Nodes) {
            if (node.NodeId.empty()) {
                result.ErrorCode = UI_WIDGET_LAYOUT_INVALID_FIELD;
                result.Message = "nodeId is required for every layout node.";
                return result;
            }
            if (node.BlueprintId.empty()) {
                result.ErrorCode = UI_WIDGET_LAYOUT_INVALID_FIELD;
                result.Message = "blueprintId is required for every layout node.";
                return result;
            }
            if (!node.PropertyOverrides.is_object()) {
                result.ErrorCode = UI_WIDGET_LAYOUT_INVALID_FIELD;
                result.Message = "propertyOverrides must be an object.";
                return result;
            }
            if (!nodeIds.insert(node.NodeId).second) {
                result.ErrorCode = UI_WIDGET_LAYOUT_DUPLICATE_NODE;
                result.Message = "Duplicate layout nodeId detected.";
                return result;
            }
        }

        for (const WidgetLayoutNode& node : asset.Nodes) {
            if (!node.ParentNodeId.empty() && nodeIds.find(node.ParentNodeId) == nodeIds.end()) {
                result.ErrorCode = UI_WIDGET_LAYOUT_UNKNOWN_PARENT;
                result.Message = "Layout node references unknown parentNodeId.";
                return result;
            }
        }

        result.Valid = true;
        return result;
    }

    WidgetLayoutDependencySet CollectWidgetLayoutDependencies(const WidgetLayoutAsset& asset) {
        WidgetLayoutDependencySet dependencies;
        std::unordered_set<std::string> blueprintIds;
        std::unordered_set<std::string> styleIds;

        for (const WidgetLayoutNode& node : asset.Nodes) {
            if (!node.BlueprintId.empty() && blueprintIds.insert(node.BlueprintId).second) {
                dependencies.BlueprintDependencies.push_back(node.BlueprintId);
            }
            for (const std::string& styleDependency : node.StyleDependencyIds) {
                if (!styleDependency.empty() && styleIds.insert(styleDependency).second) {
                    dependencies.StyleDependencies.push_back(styleDependency);
                }
            }
        }

        return dependencies;
    }

    void SortLayoutNodesDeterministically(std::vector<WidgetLayoutNode>& nodes) {
        std::stable_sort(
            nodes.begin(),
            nodes.end(),
            [](const WidgetLayoutNode& lhs, const WidgetLayoutNode& rhs) {
                if (lhs.Order != rhs.Order) {
                    return lhs.Order < rhs.Order;
                }
                return lhs.NodeId < rhs.NodeId;
            });
    }

    const char* ToString(WidgetInstanceResetPolicy policy) {
        switch (policy) {
            case WidgetInstanceResetPolicy::ResetToLayoutDefaults:
                return "reset_to_layout_defaults";
            case WidgetInstanceResetPolicy::PreserveRuntimeState:
                return "preserve_runtime_state";
            default:
                return "reset_to_layout_defaults";
        }
    }

    WidgetInstanceResetPolicy ParseWidgetInstanceResetPolicy(const std::string& policy) {
        if (policy == "preserve_runtime_state") {
            return WidgetInstanceResetPolicy::PreserveRuntimeState;
        }
        return WidgetInstanceResetPolicy::ResetToLayoutDefaults;
    }

} // namespace Authoring
} // namespace UI
} // namespace Core

