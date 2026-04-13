#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace Core {
namespace UI {
namespace Authoring {

    using Json = nlohmann::json;

    constexpr uint32_t WIDGET_BLUEPRINT_SCHEMA_VERSION = 1;

    constexpr const char* UI_WIDGET_BLUEPRINT_INVALID_SCHEMA = "UI_WIDGET_BLUEPRINT_INVALID_SCHEMA";
    constexpr const char* UI_WIDGET_BLUEPRINT_INVALID_FIELD = "UI_WIDGET_BLUEPRINT_INVALID_FIELD";
    constexpr const char* UI_WIDGET_BLUEPRINT_INHERITANCE_LOOP = "UI_WIDGET_BLUEPRINT_INHERITANCE_LOOP";
    constexpr const char* UI_WIDGET_BLUEPRINT_DUPLICATE_PROPERTY = "UI_WIDGET_BLUEPRINT_DUPLICATE_PROPERTY";
    constexpr const char* UI_WIDGET_BLUEPRINT_DUPLICATE_TRANSITION = "UI_WIDGET_BLUEPRINT_DUPLICATE_TRANSITION";
    constexpr const char* UI_WIDGET_BLUEPRINT_PARSE_ERROR = "UI_WIDGET_BLUEPRINT_PARSE_ERROR";

    struct WidgetBlueprintTransitionChannel {
        std::string PropertyPath;
        float FromValue = 0.0f;
        float ToValue = 0.0f;
        std::string Easing = "linear";
    };

    struct WidgetBlueprintTransition {
        std::string TransitionId;
        std::string InterruptPolicy = "cancel_previous";
        float DurationSeconds = 0.0f;
        std::vector<WidgetBlueprintTransitionChannel> Channels;
    };

    struct WidgetBlueprintAsset {
        std::string BlueprintId;
        uint32_t SchemaVersion = WIDGET_BLUEPRINT_SCHEMA_VERSION;
        std::string ParentBlueprintId;
        std::string WidgetType;
        Json StyleOverrides = Json::object();
        std::vector<std::string> BindableProperties;
        std::vector<WidgetBlueprintTransition> DefaultTransitions;
    };

    struct WidgetBlueprintValidationResult {
        bool Valid = false;
        std::string ErrorCode;
        std::string Message;
        std::vector<std::string> Diagnostics;
        std::vector<std::string> DeprecatedFields;
    };

    struct WidgetBlueprintCookedMetadata {
        uint32_t SchemaVersion = WIDGET_BLUEPRINT_SCHEMA_VERSION;
        std::string BlueprintId;
        std::string ParentBlueprintId;
        std::string WidgetType;
        uint64_t SourceHash = 0;
        uint32_t BindablePropertyCount = 0;
        uint32_t TransitionCount = 0;
    };

    Json SerializeWidgetBlueprintAsset(const WidgetBlueprintAsset& asset);
    std::optional<WidgetBlueprintAsset> DeserializeWidgetBlueprintAsset(
        const Json& payload,
        WidgetBlueprintValidationResult* validation = nullptr);

    WidgetBlueprintValidationResult ValidateWidgetBlueprintAsset(const WidgetBlueprintAsset& asset);
    uint64_t ComputeWidgetBlueprintDeterministicHash(const WidgetBlueprintAsset& asset);

    Json SerializeWidgetBlueprintCookedMetadata(const WidgetBlueprintCookedMetadata& metadata);
    WidgetBlueprintCookedMetadata BuildWidgetBlueprintCookedMetadata(
        const WidgetBlueprintAsset& asset,
        uint64_t sourceHash);

    std::vector<std::string> CollectDeprecatedWidgetBlueprintFields(const Json& payload);

    bool DetectWidgetBlueprintInheritanceLoop(
        const WidgetBlueprintAsset& candidate,
        const std::unordered_map<std::string, WidgetBlueprintAsset>& knownBlueprints,
        std::string* loopBlueprintId = nullptr);

} // namespace Authoring
} // namespace UI
} // namespace Core

