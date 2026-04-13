#include "Core/UI/Authoring/WidgetBlueprintAsset.h"

#include <unordered_set>

namespace Core {
namespace UI {
namespace Authoring {
namespace {

    Json SerializeTransitionChannel(const WidgetBlueprintTransitionChannel& channel) {
        return {
            {"propertyPath", channel.PropertyPath},
            {"fromValue", channel.FromValue},
            {"toValue", channel.ToValue},
            {"easing", channel.Easing}
        };
    }

    std::optional<WidgetBlueprintTransitionChannel> DeserializeTransitionChannel(
        const Json& payload,
        WidgetBlueprintValidationResult* validation) {

        if (!payload.is_object()) {
            if (validation != nullptr) {
                validation->Valid = false;
                validation->ErrorCode = UI_WIDGET_BLUEPRINT_INVALID_FIELD;
                validation->Message = "Transition channel must be an object.";
            }
            return std::nullopt;
        }

        WidgetBlueprintTransitionChannel channel;
        channel.PropertyPath = payload.value("propertyPath", "");
        channel.FromValue = payload.value("fromValue", 0.0f);
        channel.ToValue = payload.value("toValue", 0.0f);
        channel.Easing = payload.value("easing", "linear");

        if (channel.PropertyPath.empty()) {
            if (validation != nullptr) {
                validation->Valid = false;
                validation->ErrorCode = UI_WIDGET_BLUEPRINT_INVALID_FIELD;
                validation->Message = "Transition channel propertyPath cannot be empty.";
            }
            return std::nullopt;
        }

        return channel;
    }

    Json SerializeTransition(const WidgetBlueprintTransition& transition) {
        Json channels = Json::array();
        for (const WidgetBlueprintTransitionChannel& channel : transition.Channels) {
            channels.push_back(SerializeTransitionChannel(channel));
        }

        return {
            {"transitionId", transition.TransitionId},
            {"interruptPolicy", transition.InterruptPolicy},
            {"durationSeconds", transition.DurationSeconds},
            {"channels", std::move(channels)}
        };
    }

    std::optional<WidgetBlueprintTransition> DeserializeTransition(
        const Json& payload,
        WidgetBlueprintValidationResult* validation) {

        if (!payload.is_object()) {
            if (validation != nullptr) {
                validation->Valid = false;
                validation->ErrorCode = UI_WIDGET_BLUEPRINT_INVALID_FIELD;
                validation->Message = "Transition definition must be an object.";
            }
            return std::nullopt;
        }

        WidgetBlueprintTransition transition;
        transition.TransitionId = payload.value("transitionId", "");
        transition.InterruptPolicy = payload.value("interruptPolicy", "cancel_previous");
        transition.DurationSeconds = payload.value("durationSeconds", 0.0f);

        if (transition.TransitionId.empty()) {
            if (validation != nullptr) {
                validation->Valid = false;
                validation->ErrorCode = UI_WIDGET_BLUEPRINT_INVALID_FIELD;
                validation->Message = "TransitionId cannot be empty.";
            }
            return std::nullopt;
        }

        const Json channelsPayload = payload.value("channels", Json::array());
        if (!channelsPayload.is_array()) {
            if (validation != nullptr) {
                validation->Valid = false;
                validation->ErrorCode = UI_WIDGET_BLUEPRINT_INVALID_FIELD;
                validation->Message = "Transition channels must be an array.";
            }
            return std::nullopt;
        }

        transition.Channels.reserve(channelsPayload.size());
        for (const Json& channelPayload : channelsPayload) {
            std::optional<WidgetBlueprintTransitionChannel> channel =
                DeserializeTransitionChannel(channelPayload, validation);
            if (!channel.has_value()) {
                return std::nullopt;
            }
            transition.Channels.push_back(std::move(channel.value()));
        }

        return transition;
    }

} // namespace

    Json SerializeWidgetBlueprintAsset(const WidgetBlueprintAsset& asset) {
        Json bindableProperties = Json::array();
        for (const std::string& property : asset.BindableProperties) {
            bindableProperties.push_back(property);
        }

        Json transitions = Json::array();
        for (const WidgetBlueprintTransition& transition : asset.DefaultTransitions) {
            transitions.push_back(SerializeTransition(transition));
        }

        return {
            {"blueprintId", asset.BlueprintId},
            {"schemaVersion", asset.SchemaVersion},
            {"parentBlueprintId", asset.ParentBlueprintId},
            {"widgetType", asset.WidgetType},
            {"styleOverrides", asset.StyleOverrides},
            {"bindableProperties", std::move(bindableProperties)},
            {"defaultTransitions", std::move(transitions)}
        };
    }

    std::optional<WidgetBlueprintAsset> DeserializeWidgetBlueprintAsset(
        const Json& payload,
        WidgetBlueprintValidationResult* validation) {

        WidgetBlueprintValidationResult localValidation;
        WidgetBlueprintValidationResult* validationOut =
            validation != nullptr ? validation : &localValidation;
        validationOut->Valid = false;

        if (!payload.is_object()) {
            validationOut->ErrorCode = UI_WIDGET_BLUEPRINT_PARSE_ERROR;
            validationOut->Message = "Widget blueprint payload must be an object.";
            return std::nullopt;
        }

        WidgetBlueprintAsset asset;
        asset.BlueprintId = payload.value("blueprintId", "");
        asset.SchemaVersion = payload.value("schemaVersion", WIDGET_BLUEPRINT_SCHEMA_VERSION);
        asset.ParentBlueprintId = payload.value("parentBlueprintId", "");
        asset.WidgetType = payload.value("widgetType", "");
        asset.StyleOverrides = payload.value("styleOverrides", Json::object());

        const Json bindablePropertiesPayload = payload.value("bindableProperties", Json::array());
        if (!bindablePropertiesPayload.is_array()) {
            validationOut->ErrorCode = UI_WIDGET_BLUEPRINT_INVALID_FIELD;
            validationOut->Message = "bindableProperties must be an array.";
            return std::nullopt;
        }
        for (const Json& property : bindablePropertiesPayload) {
            if (!property.is_string()) {
                validationOut->ErrorCode = UI_WIDGET_BLUEPRINT_INVALID_FIELD;
                validationOut->Message = "bindableProperties entries must be strings.";
                return std::nullopt;
            }
            asset.BindableProperties.push_back(property.get<std::string>());
        }

        const Json transitionsPayload = payload.value("defaultTransitions", Json::array());
        if (!transitionsPayload.is_array()) {
            validationOut->ErrorCode = UI_WIDGET_BLUEPRINT_INVALID_FIELD;
            validationOut->Message = "defaultTransitions must be an array.";
            return std::nullopt;
        }
        for (const Json& transitionPayload : transitionsPayload) {
            std::optional<WidgetBlueprintTransition> transition =
                DeserializeTransition(transitionPayload, validationOut);
            if (!transition.has_value()) {
                return std::nullopt;
            }
            asset.DefaultTransitions.push_back(std::move(transition.value()));
        }

        *validationOut = ValidateWidgetBlueprintAsset(asset);
        if (!validationOut->Valid) {
            return std::nullopt;
        }

        return asset;
    }

    WidgetBlueprintValidationResult ValidateWidgetBlueprintAsset(const WidgetBlueprintAsset& asset) {
        WidgetBlueprintValidationResult result;
        result.Valid = false;

        if (asset.BlueprintId.empty()) {
            result.ErrorCode = UI_WIDGET_BLUEPRINT_INVALID_SCHEMA;
            result.Message = "blueprintId is required.";
            return result;
        }

        if (asset.SchemaVersion != WIDGET_BLUEPRINT_SCHEMA_VERSION) {
            result.ErrorCode = UI_WIDGET_BLUEPRINT_INVALID_SCHEMA;
            result.Message = "Unsupported widget blueprint schemaVersion.";
            return result;
        }

        if (asset.WidgetType.empty()) {
            result.ErrorCode = UI_WIDGET_BLUEPRINT_INVALID_SCHEMA;
            result.Message = "widgetType is required.";
            return result;
        }

        if (!asset.StyleOverrides.is_object()) {
            result.ErrorCode = UI_WIDGET_BLUEPRINT_INVALID_FIELD;
            result.Message = "styleOverrides must be an object.";
            return result;
        }

        std::unordered_set<std::string> propertySet;
        for (const std::string& property : asset.BindableProperties) {
            if (property.empty()) {
                result.ErrorCode = UI_WIDGET_BLUEPRINT_INVALID_FIELD;
                result.Message = "bindableProperties entries cannot be empty.";
                return result;
            }
            if (!propertySet.insert(property).second) {
                result.ErrorCode = UI_WIDGET_BLUEPRINT_DUPLICATE_PROPERTY;
                result.Message = "Duplicate bindable property path detected.";
                return result;
            }
        }

        std::unordered_set<std::string> transitionIds;
        for (const WidgetBlueprintTransition& transition : asset.DefaultTransitions) {
            if (transition.TransitionId.empty()) {
                result.ErrorCode = UI_WIDGET_BLUEPRINT_INVALID_FIELD;
                result.Message = "defaultTransitions entries require transitionId.";
                return result;
            }
            if (!transitionIds.insert(transition.TransitionId).second) {
                result.ErrorCode = UI_WIDGET_BLUEPRINT_DUPLICATE_TRANSITION;
                result.Message = "Duplicate default transition ID detected.";
                return result;
            }
            if (transition.DurationSeconds < 0.0f) {
                result.ErrorCode = UI_WIDGET_BLUEPRINT_INVALID_FIELD;
                result.Message = "defaultTransitions durationSeconds cannot be negative.";
                return result;
            }
        }

        result.Valid = true;
        return result;
    }

    uint64_t ComputeWidgetBlueprintDeterministicHash(const WidgetBlueprintAsset& asset) {
        const Json canonicalPayload = SerializeWidgetBlueprintAsset(asset);
        const std::string canonicalText = canonicalPayload.dump();

        uint64_t hash = 14695981039346656037ULL;
        for (const unsigned char byte : canonicalText) {
            hash ^= static_cast<uint64_t>(byte);
            hash *= 1099511628211ULL;
        }

        return hash;
    }

    Json SerializeWidgetBlueprintCookedMetadata(const WidgetBlueprintCookedMetadata& metadata) {
        return {
            {"schemaVersion", metadata.SchemaVersion},
            {"assetType", "widget_blueprint"},
            {"blueprintId", metadata.BlueprintId},
            {"parentBlueprintId", metadata.ParentBlueprintId},
            {"widgetType", metadata.WidgetType},
            {"sourceHash", metadata.SourceHash},
            {"bindablePropertyCount", metadata.BindablePropertyCount},
            {"transitionCount", metadata.TransitionCount}
        };
    }

    WidgetBlueprintCookedMetadata BuildWidgetBlueprintCookedMetadata(
        const WidgetBlueprintAsset& asset,
        uint64_t sourceHash) {

        WidgetBlueprintCookedMetadata metadata;
        metadata.SchemaVersion = asset.SchemaVersion;
        metadata.BlueprintId = asset.BlueprintId;
        metadata.ParentBlueprintId = asset.ParentBlueprintId;
        metadata.WidgetType = asset.WidgetType;
        metadata.SourceHash = sourceHash;
        metadata.BindablePropertyCount = static_cast<uint32_t>(asset.BindableProperties.size());
        metadata.TransitionCount = static_cast<uint32_t>(asset.DefaultTransitions.size());
        return metadata;
    }

    std::vector<std::string> CollectDeprecatedWidgetBlueprintFields(const Json& payload) {
        if (!payload.is_object()) {
            return {};
        }

        static const std::vector<std::string> kDeprecatedFields = {
            "style",
            "defaultStyle",
            "legacyBindings",
            "transitionPresets"
        };

        std::vector<std::string> deprecated;
        deprecated.reserve(kDeprecatedFields.size());
        for (const std::string& field : kDeprecatedFields) {
            if (payload.contains(field)) {
                deprecated.push_back(field);
            }
        }
        return deprecated;
    }

    bool DetectWidgetBlueprintInheritanceLoop(
        const WidgetBlueprintAsset& candidate,
        const std::unordered_map<std::string, WidgetBlueprintAsset>& knownBlueprints,
        std::string* loopBlueprintId) {

        if (candidate.BlueprintId.empty() || candidate.ParentBlueprintId.empty()) {
            return false;
        }

        std::unordered_set<std::string> visited;
        std::string current = candidate.ParentBlueprintId;

        while (!current.empty()) {
            if (current == candidate.BlueprintId) {
                if (loopBlueprintId != nullptr) {
                    *loopBlueprintId = current;
                }
                return true;
            }

            if (!visited.insert(current).second) {
                if (loopBlueprintId != nullptr) {
                    *loopBlueprintId = current;
                }
                return true;
            }

            const auto parentIt = knownBlueprints.find(current);
            if (parentIt == knownBlueprints.end()) {
                return false;
            }
            current = parentIt->second.ParentBlueprintId;
        }

        return false;
    }

} // namespace Authoring
} // namespace UI
} // namespace Core

