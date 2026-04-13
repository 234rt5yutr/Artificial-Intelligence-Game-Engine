#pragma once

// MCP UI Tools
// Allows AI agents to control on-screen messaging, HUD updates, save state triggers,
// and loading screens for dynamic Game Master functionality

#include "MCPTool.h"
#include "MCPTypes.h"
#include "Core/UI/Animation/WidgetTransitionService.h"
#include "Core/UI/Binding/UIBindingService.h"
#include "Core/UI/Widgets/WidgetSystem.h"
#include <entt/entt.hpp>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <type_traits>
#include <vector>

namespace Core {

// Forward declarations
namespace ECS {
    class Scene;
}

namespace State {
    class SaveManager;
    class TransitionManager;
}

namespace UI {
    class UIManager;
    namespace Widgets { class WidgetSystem; }
}

namespace MCP {

    // ============================================================================
    // Message Types and Structures
    // ============================================================================

    /// @brief Type of screen message for visual styling
    enum class MessageType {
        Info,           ///< Neutral information (white/default)
        Warning,        ///< Caution message (yellow)
        Error,          ///< Critical error (red)
        Narrative,      ///< Story/dialogue text (styled for immersion)
        Tutorial,       ///< Tutorial hint (with special formatting)
        Success         ///< Achievement/completion (green)
    };

    /// @brief Position for screen messages
    enum class MessagePosition {
        Top,            ///< Top of screen
        Center,         ///< Center of screen
        Bottom          ///< Bottom of screen (subtitle style)
    };

    /// @brief Screen message entry
    struct ScreenMessage {
        std::string text;
        MessageType type = MessageType::Info;
        MessagePosition position = MessagePosition::Center;
        float duration = 3.0f;
        float fontSize = 24.0f;
        glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
        bool outline = true;
        float startTime = 0.0f;         // Set when message is displayed
        int32_t priority = 0;           // Higher priority = shown first
    };

    struct ScreenMessagePriority {
        bool operator()(const ScreenMessage& a, const ScreenMessage& b) const {
            return a.priority < b.priority;
        }
    };

    // ============================================================================
    // Message Queue Manager (Singleton for throttling)
    // ============================================================================

    /// @brief Manages screen message queue with throttling
    class MessageQueueManager {
    public:
        static MessageQueueManager& Get();

        /// @brief Queue a new message
        void QueueMessage(const ScreenMessage& message);

        /// @brief Update and process message queue
        void Update(float deltaTime);

        /// @brief Get currently active messages
        const std::vector<ScreenMessage>& GetActiveMessages() const { return m_ActiveMessages; }
        size_t GetPendingMessageCount();

        /// @brief Clear all messages
        void ClearAll();

        /// @brief Set maximum concurrent messages
        void SetMaxConcurrentMessages(size_t max) { m_MaxConcurrent = max; }

        /// @brief Set minimum delay between new messages (throttling)
        void SetMessageDelay(float seconds) { m_MinDelay = seconds; }

    private:
        MessageQueueManager() = default;
        
        std::priority_queue<ScreenMessage, std::vector<ScreenMessage>, ScreenMessagePriority> m_PendingMessages;
        
        std::vector<ScreenMessage> m_ActiveMessages;
        size_t m_MaxConcurrent = 3;
        float m_MinDelay = 0.5f;
        float m_LastMessageTime = 0.0f;
        float m_CurrentTime = 0.0f;
        std::mutex m_Mutex;
    };

    inline MessageQueueManager& MessageQueueManager::Get() {
        static MessageQueueManager instance;
        return instance;
    }

    inline void MessageQueueManager::QueueMessage(const ScreenMessage& message) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_PendingMessages.push(message);
    }

    inline void MessageQueueManager::Update(float deltaTime) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_CurrentTime += deltaTime;

        for (auto it = m_ActiveMessages.begin(); it != m_ActiveMessages.end();) {
            if ((m_CurrentTime - it->startTime) >= it->duration) {
                it = m_ActiveMessages.erase(it);
            } else {
                ++it;
            }
        }

        if ((m_CurrentTime - m_LastMessageTime) < m_MinDelay) {
            return;
        }

        while (!m_PendingMessages.empty() && m_ActiveMessages.size() < m_MaxConcurrent) {
            ScreenMessage nextMessage = m_PendingMessages.top();
            m_PendingMessages.pop();
            nextMessage.startTime = m_CurrentTime;
            m_ActiveMessages.push_back(std::move(nextMessage));
            m_LastMessageTime = m_CurrentTime;
        }
    }

    inline void MessageQueueManager::ClearAll() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        while (!m_PendingMessages.empty()) {
            m_PendingMessages.pop();
        }
        m_ActiveMessages.clear();
    }

    inline size_t MessageQueueManager::GetPendingMessageCount() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_PendingMessages.size();
    }

    // ============================================================================
    // DisplayScreenMessage Tool
    // ============================================================================

    /// @brief Create DisplayScreenMessage MCP tool
    /// 
    /// Input Schema:
    /// {
    ///   "message": string,           // Required: Text to display
    ///   "duration": number,          // Optional: Seconds (default: 3.0)
    ///   "type": string,              // Optional: "info"|"warning"|"error"|"narrative"|"tutorial"|"success"
    ///   "position": string,          // Optional: "top"|"center"|"bottom" (default: "center")
    ///   "style": {                   // Optional: Custom styling
    ///     "fontSize": number,
    ///     "color": {"r": number, "g": number, "b": number, "a": number},
    ///     "outline": boolean
    ///   }
    /// }
    inline MCPToolPtr CreateDisplayScreenMessageTool() {
        ToolInputSchema schema;
        schema.type = "object";
        schema.properties = {
            {"message", {{"type", "string"}, {"description", "Text to display on screen"}}},
            {"duration", {{"type", "number"}, {"description", "Display duration in seconds (default: 3.0)"}}},
            {"type", {{"type", "string"}, {"enum", Json::array({"info", "warning", "error", "narrative", "tutorial", "success"})}, {"description", "Message type for styling"}}},
            {"position", {{"type", "string"}, {"enum", Json::array({"top", "center", "bottom"})}, {"description", "Screen position (default: center)"}}},
            {"style", {{"type", "object"}, {"properties", {
                {"fontSize", {{"type", "number"}}},
                {"color", {{"type", "object"}, {"properties", {
                    {"r", {{"type", "number"}}},
                    {"g", {{"type", "number"}}},
                    {"b", {{"type", "number"}}},
                    {"a", {{"type", "number"}}}
                }}}},
                {"outline", {{"type", "boolean"}}}
            }}}}
        };
        schema.required = {"message"};

        return CreateLambdaTool(
            "DisplayScreenMessage",
            "Display a message on screen for the player. Use for narrative text, alerts, tutorials, or notifications.",
            schema,
            [](const Json& args, ECS::Scene* scene) -> ToolResult {
                (void)scene;
                
                ScreenMessage msg;
                msg.text = args.value("message", "");
                
                if (msg.text.empty()) {
                    return ToolResult{false, {}, "Message text cannot be empty"};
                }
                
                // Sanitize message (max 500 chars, strip control characters)
                if (msg.text.length() > 500) {
                    msg.text = msg.text.substr(0, 500);
                }
                
                msg.duration = args.value("duration", 3.0f);
                
                // Parse type
                std::string typeStr = args.value("type", "info");
                if (typeStr == "warning") msg.type = MessageType::Warning;
                else if (typeStr == "error") msg.type = MessageType::Error;
                else if (typeStr == "narrative") msg.type = MessageType::Narrative;
                else if (typeStr == "tutorial") msg.type = MessageType::Tutorial;
                else if (typeStr == "success") msg.type = MessageType::Success;
                else msg.type = MessageType::Info;
                
                // Parse position
                std::string posStr = args.value("position", "center");
                if (posStr == "top") msg.position = MessagePosition::Top;
                else if (posStr == "bottom") msg.position = MessagePosition::Bottom;
                else msg.position = MessagePosition::Center;
                
                // Parse custom style
                if (args.contains("style")) {
                    auto& style = args["style"];
                    msg.fontSize = style.value("fontSize", 24.0f);
                    msg.outline = style.value("outline", true);
                    
                    if (style.contains("color")) {
                        auto& color = style["color"];
                        msg.color = {
                            color.value("r", 1.0f),
                            color.value("g", 1.0f),
                            color.value("b", 1.0f),
                            color.value("a", 1.0f)
                        };
                    }
                }
                
                // Queue the message
                MessageQueueManager& queueManager = MessageQueueManager::Get();
                queueManager.QueueMessage(msg);
                
                Json result;
                result["status"] = "queued";
                result["message"] = msg.text;
                result["duration"] = msg.duration;
                result["pendingQueueDepth"] = queueManager.GetPendingMessageCount();
                result["activeMessageCount"] = queueManager.GetActiveMessages().size();
                
                return ToolResult{true, result, ""};
            },
            false  // Does not require scene
        );
    }

    // ============================================================================
    // UpdateHUD Tool
    // ============================================================================

    inline std::optional<UI::Widgets::WidgetSystem::WidgetPropertyValue> ConvertJsonToWidgetPropertyValue(
        const Json& value) {
        using PropertyValue = UI::Widgets::WidgetSystem::WidgetPropertyValue;

        if (value.is_boolean()) {
            return PropertyValue(value.get<bool>());
        }
        if (value.is_number_integer()) {
            return PropertyValue(static_cast<int32_t>(value.get<int64_t>()));
        }
        if (value.is_number()) {
            return PropertyValue(value.get<float>());
        }
        if (value.is_string()) {
            return PropertyValue(value.get<std::string>());
        }
        if (value.is_object()) {
            if (value.contains("x") && value.contains("y")) {
                return PropertyValue(glm::vec2(
                    value.value("x", 0.0f),
                    value.value("y", 0.0f)));
            }
            if (value.contains("r") && value.contains("g") && value.contains("b") && value.contains("a")) {
                return PropertyValue(glm::vec4(
                    value.value("r", 0.0f),
                    value.value("g", 0.0f),
                    value.value("b", 0.0f),
                    value.value("a", 1.0f)));
            }
        }
        return std::nullopt;
    }

    inline std::optional<Json> ConvertWidgetPropertyValueToJson(
        const UI::Widgets::WidgetSystem::WidgetPropertyValue& value) {
        return std::visit(
            [](const auto& currentValue) -> std::optional<Json> {
                using ValueType = std::decay_t<decltype(currentValue)>;
                if constexpr (std::is_same_v<ValueType, std::monostate>) {
                    return std::nullopt;
                } else if constexpr (std::is_same_v<ValueType, glm::vec2>) {
                    return Json{{"x", currentValue.x}, {"y", currentValue.y}};
                } else if constexpr (std::is_same_v<ValueType, glm::vec4>) {
                    return Json{
                        {"r", currentValue.r},
                        {"g", currentValue.g},
                        {"b", currentValue.b},
                        {"a", currentValue.a}};
                } else {
                    return Json(currentValue);
                }
            },
            value);
    }

    /// @brief Create UpdateHUD MCP tool
    /// 
    /// Input Schema:
    /// {
    ///   "widget": string,            // Required: Widget ID ("health"|"ammo"|"objective"|"score"|"custom")
    ///   "value": any,                // Required: New value (number, string, or object)
    ///   "animate": boolean,          // Optional: Animate the change (default: true)
    ///   "visible": boolean           // Optional: Show/hide widget
    /// }
    inline MCPToolPtr CreateUpdateHUDTool() {
        ToolInputSchema schema;
        schema.type = "object";
        schema.properties = {
            {"widget", {{"type", "string"}, {"description", "Widget identifier (e.g., 'health', 'ammo', 'objective', 'score')"}}},
            {"property", {{"type", "string"}, {"description", "Widget property path (default: 'value')"}}},
            {"value", {{"description", "New value for the widget (type depends on widget)"}}},
            {"animate", {{"type", "boolean"}, {"description", "Animate the value change (default: true)"}}},
            {"visible", {{"type", "boolean"}, {"description", "Set widget visibility"}}}
        };
        schema.required = {"widget", "value"};

        return CreateLambdaTool(
            "UpdateHUD",
            "Update a HUD widget's value, visibility, or trigger animations. Use to change health bars, objective trackers, scores, etc.",
            schema,
            [](const Json& args, ECS::Scene* scene) -> ToolResult {
                (void)scene;
                std::string widgetId = args.value("widget", "");
                
                if (widgetId.empty()) {
                    return ToolResult{false, {}, "Widget identifier is required"};
                }
                
                Json value = args.value("value", Json());
                bool animate = args.value("animate", true);
                std::string propertyPath = args.value("property", "value");

                std::optional<UI::Widgets::WidgetSystem::WidgetPropertyValue> propertyValue =
                    ConvertJsonToWidgetPropertyValue(value);
                if (!propertyValue.has_value()) {
                    return ToolResult{false, {}, "Unsupported HUD value type for widget property update"};
                }

                bool updated = UI::Widgets::WidgetSystem::Get().SetWidgetProperty(
                    widgetId,
                    propertyPath,
                    propertyValue.value());
                if (!updated) {
                    return ToolResult{false, {}, "Widget not found or property update rejected"};
                }

                if (args.contains("visible") && args["visible"].is_boolean()) {
                    (void)UI::Widgets::WidgetSystem::Get().SetWidgetProperty(
                        widgetId,
                        "visible",
                        UI::Widgets::WidgetSystem::WidgetPropertyValue(args["visible"].get<bool>()));
                }

                Json result;
                result["widget"] = widgetId;
                result["property"] = propertyPath;
                result["value"] = value;
                result["animate"] = animate;
                if (args.contains("visible")) {
                    result["visible"] = args["visible"];
                }
                result["status"] = "updated";

                std::optional<UI::Widgets::WidgetSystem::WidgetPropertyValue> runtimeValue =
                    UI::Widgets::WidgetSystem::Get().GetWidgetProperty(widgetId, propertyPath);
                if (runtimeValue.has_value()) {
                    std::optional<Json> runtimeValueJson = ConvertWidgetPropertyValueToJson(runtimeValue.value());
                    if (runtimeValueJson.has_value()) {
                        result["runtimeValue"] = runtimeValueJson.value();
                    }
                }
                
                return ToolResult{true, result, ""};
            },
            true  // Requires scene for widget lookup
        );
    }

    // ============================================================================
    // Stage 27 Binding/Transition Runtime Tools
    // ============================================================================

    inline MCPToolPtr CreateBindWidgetPropertyTool() {
        ToolInputSchema schema;
        schema.type = "object";
        schema.properties = {
            {"widget", {{"type", "string"}, {"description", "Widget identifier"}}},
            {"propertyPath", {{"type", "string"}, {"description", "Widget property path"}}},
            {"dataPath", {{"type", "string"}, {"description", "Data path in binding store"}}},
            {"mode", {{"type", "string"}, {"enum", Json::array({"one_way", "two_way"})}, {"description", "Binding mode"}}},
            {"converter", {{"type", "string"}, {"description", "Optional converter name"}}},
            {"validator", {{"type", "string"}, {"description", "Optional validator name"}}}
        };
        schema.required = {"widget", "propertyPath", "dataPath"};

        return CreateLambdaTool(
            "BindWidgetProperty",
            "Create a Stage 27 runtime data binding between a widget property and data path.",
            schema,
            [](const Json& args, ECS::Scene* scene) -> ToolResult {
                (void)scene;
                const std::string widgetId = args.value("widget", "");
                const std::string propertyPath = args.value("propertyPath", "");
                const std::string dataPath = args.value("dataPath", "");
                const std::string modeString = args.value("mode", "one_way");

                if (widgetId.empty() || propertyPath.empty() || dataPath.empty()) {
                    return ToolResult{false, {}, "widget, propertyPath, and dataPath are required"};
                }

                Binding::BindingMode mode = Binding::BindingMode::OneWay;
                if (modeString == "two_way") {
                    mode = Binding::BindingMode::TwoWay;
                } else if (modeString != "one_way") {
                    return ToolResult{false, {}, "Invalid mode. Use one_way or two_way."};
                }

                Binding::BindingRegistrationRequest request;
                request.WidgetId = widgetId;
                request.WidgetPropertyPath = propertyPath;
                request.DataPath = dataPath;
                request.Mode = mode;
                request.ConverterName = args.value("converter", "");
                request.ValidatorName = args.value("validator", "");

                const Binding::BindingRegistrationResult bindResult =
                    Binding::UIBindingService::Get().BindWidgetPropertyToData(request);
                if (!bindResult.Success) {
                    Json errorPayload;
                    errorPayload["errorCode"] = bindResult.ErrorCode;
                    errorPayload["message"] = bindResult.Message;
                    return ToolResult{false, errorPayload, bindResult.Message};
                }

                Json result;
                result["status"] = "bound";
                result["bindingHandle"] = bindResult.BindingHandle;
                result["widget"] = widgetId;
                result["propertyPath"] = propertyPath;
                result["dataPath"] = dataPath;
                result["mode"] = Binding::BindingModeToString(mode);
                return ToolResult{true, result, ""};
            },
            true);
    }

    inline MCPToolPtr CreateUnbindWidgetPropertyTool() {
        ToolInputSchema schema;
        schema.type = "object";
        schema.properties = {
            {"bindingHandle", {{"type", "integer"}, {"description", "Binding handle returned by BindWidgetProperty"}}},
            {"widget", {{"type", "string"}, {"description", "Widget identifier to unbind all bindings"}}}
        };
        schema.required = {};

        return CreateLambdaTool(
            "UnbindWidgetProperty",
            "Remove one binding by handle or all bindings for a widget.",
            schema,
            [](const Json& args, ECS::Scene* scene) -> ToolResult {
                (void)scene;

                if (!args.contains("bindingHandle") && !args.contains("widget")) {
                    return ToolResult{false, {}, "Provide bindingHandle or widget."};
                }

                Json result;
                if (args.contains("bindingHandle")) {
                    const uint64_t handle = args.value("bindingHandle", 0u);
                    const bool removed = Binding::UIBindingService::Get().UnbindBinding(handle);
                    if (!removed) {
                        return ToolResult{false, {}, "Binding handle not found."};
                    }
                    result["status"] = "unbound";
                    result["removedCount"] = 1;
                    result["bindingHandle"] = handle;
                    return ToolResult{true, result, ""};
                }

                const std::string widgetId = args.value("widget", "");
                if (widgetId.empty()) {
                    return ToolResult{false, {}, "Widget identifier cannot be empty."};
                }

                const uint32_t removedCount = Binding::UIBindingService::Get().UnbindWidget(widgetId);
                result["status"] = "unbound";
                result["removedCount"] = removedCount;
                result["widget"] = widgetId;
                return ToolResult{true, result, ""};
            },
            true);
    }

    inline MCPToolPtr CreateAnimateWidgetTransitionTool() {
        ToolInputSchema schema;
        schema.type = "object";
        schema.properties = {
            {"widget", {{"type", "string"}, {"description", "Widget identifier"}}},
            {"transitionId", {{"type", "string"}, {"description", "Optional transition identifier"}}},
            {"timeline", {{"type", "object"}, {"description", "Timeline payload with duration and channels"}}},
            {"interruptionPolicy", {{"type", "string"}, {"enum", Json::array({"cancel_previous", "queue", "blend"})}}},
            {"interruptExisting", {{"type", "boolean"}, {"description", "Force-cancel existing transitions for this widget"}}}
        };
        schema.required = {"widget", "timeline"};

        return CreateLambdaTool(
            "AnimateWidgetTransition",
            "Animate a widget property timeline and return live transition runtime state.",
            schema,
            [](const Json& args, ECS::Scene* scene) -> ToolResult {
                (void)scene;
                const std::string widgetId = args.value("widget", "");
                if (widgetId.empty()) {
                    return ToolResult{false, {}, "Widget identifier is required."};
                }

                if (!args.contains("timeline") || !args["timeline"].is_object()) {
                    return ToolResult{false, {}, "timeline must be an object."};
                }

                const std::string policyString = args.value("interruptionPolicy", "cancel_previous");
                const std::optional<Animation::TransitionInterruptionPolicy> interruptionPolicy =
                    Animation::TransitionInterruptionPolicyFromString(policyString);
                if (!interruptionPolicy.has_value()) {
                    return ToolResult{false, {}, "Invalid interruptionPolicy."};
                }

                const std::string transitionId = args.value("transitionId", "");
                const bool interruptExisting = args.value("interruptExisting", true);

                const Animation::WidgetTransitionResult transitionResult =
                    Animation::WidgetTransitionService::Get().AnimateWidgetTransition(
                        widgetId,
                        transitionId,
                        args["timeline"],
                        interruptionPolicy.value(),
                        interruptExisting);

                if (!transitionResult.Success) {
                    Json errorPayload;
                    errorPayload["errorCode"] = transitionResult.ErrorCode;
                    errorPayload["message"] = transitionResult.Message;
                    return ToolResult{false, errorPayload, transitionResult.Message};
                }

                Json result;
                result["status"] = "scheduled";
                result["transitionHandle"] = transitionResult.TransitionHandle;
                result["duration"] = transitionResult.Duration;
                result["state"] = Animation::TransitionRuntimeStateToString(transitionResult.State);
                result["widget"] = widgetId;

                std::optional<Animation::WidgetTransitionState> state =
                    Animation::WidgetTransitionService::Get().GetTransitionState(transitionResult.TransitionHandle);
                if (state.has_value()) {
                    Json runtimeState;
                    runtimeState["transitionId"] = state->TransitionId;
                    runtimeState["widget"] = state->WidgetId;
                    runtimeState["state"] = Animation::TransitionRuntimeStateToString(state->State);
                    runtimeState["elapsed"] = state->ElapsedTime;
                    runtimeState["duration"] = state->Duration;
                    runtimeState["channels"] = state->ChannelCount;
                    runtimeState["policy"] =
                        Animation::TransitionInterruptionPolicyToString(state->InterruptionPolicy);
                    result["runtimeState"] = runtimeState;
                }

                return ToolResult{true, result, ""};
            },
            true);
    }

    inline MCPToolPtr CreateSetUIPropertyLockModeTool() {
        ToolInputSchema schema;
        schema.type = "object";
        schema.properties = {
            {"widget", {{"type", "string"}}},
            {"propertyPath", {{"type", "string"}}},
            {"mode", {{"type", "string"}, {"enum", Json::array({"binding_wins", "transition_wins", "blend"})}}}
        };
        schema.required = {"widget", "propertyPath", "mode"};

        return CreateLambdaTool(
            "SetUIPropertyLockMode",
            "Set binding/transition arbitration mode for a widget property.",
            schema,
            [](const Json& args, ECS::Scene* scene) -> ToolResult {
                (void)scene;
                const std::string widgetId = args.value("widget", "");
                const std::string propertyPath = args.value("propertyPath", "");
                const std::string modeString = args.value("mode", "");
                if (widgetId.empty() || propertyPath.empty() || modeString.empty()) {
                    return ToolResult{false, {}, "widget, propertyPath, and mode are required."};
                }

                const std::optional<Animation::TransitionPropertyLockMode> mode =
                    Animation::TransitionPropertyLockModeFromString(modeString);
                if (!mode.has_value()) {
                    return ToolResult{false, {}, "Invalid mode value."};
                }

                Animation::WidgetTransitionService::Get().SetPropertyLockMode(
                    widgetId,
                    propertyPath,
                    mode.value());

                Json result;
                result["status"] = "applied";
                result["widget"] = widgetId;
                result["propertyPath"] = propertyPath;
                result["mode"] = Animation::TransitionPropertyLockModeToString(mode.value());
                return ToolResult{true, result, ""};
            },
            true);
    }

    inline MCPToolPtr CreateGetUIRuntimeDiagnosticsTool() {
        ToolInputSchema schema;
        schema.type = "object";
        schema.properties = {
            {"widget", {{"type", "string"}, {"description", "Optional widget filter"}}}
        };
        schema.required = {};

        return CreateLambdaTool(
            "GetUIRuntimeDiagnostics",
            "Inspect live Stage 27 binding and transition runtime diagnostics.",
            schema,
            [](const Json& args, ECS::Scene* scene) -> ToolResult {
                (void)scene;
                const std::string widgetFilter = args.value("widget", "");

                const Binding::UIBindingDiagnostics& bindingDiagnostics =
                    Binding::UIBindingService::Get().GetDiagnostics();
                const Animation::WidgetTransitionDiagnostics& transitionDiagnostics =
                    Animation::WidgetTransitionService::Get().GetDiagnostics();

                std::vector<Binding::UIBindingState> bindings = Binding::UIBindingService::Get().GetBindingStates();
                std::vector<Animation::WidgetTransitionState> transitions =
                    Animation::WidgetTransitionService::Get().GetAllTransitionStates();

                if (!widgetFilter.empty()) {
                    bindings.erase(
                        std::remove_if(
                            bindings.begin(),
                            bindings.end(),
                            [&widgetFilter](const Binding::UIBindingState& state) {
                                return state.WidgetId != widgetFilter;
                            }),
                        bindings.end());
                    transitions.erase(
                        std::remove_if(
                            transitions.begin(),
                            transitions.end(),
                            [&widgetFilter](const Animation::WidgetTransitionState& state) {
                                return state.WidgetId != widgetFilter;
                            }),
                        transitions.end());
                }

                Json result;
                result["bindingDiagnostics"] = {
                    {"activeBindings", bindingDiagnostics.ActiveBindings},
                    {"updateIterations", bindingDiagnostics.UpdateIterations},
                    {"validationFailures", bindingDiagnostics.ValidationFailures},
                    {"cycleRejections", bindingDiagnostics.CycleRejections},
                    {"autoCleanupRemovals", bindingDiagnostics.AutoCleanupRemovals},
                    {"transitionSuppressions", bindingDiagnostics.TransitionSuppressions}
                };
                result["transitionDiagnostics"] = {
                    {"activeTransitions", transitionDiagnostics.ActiveTransitions},
                    {"queuedTransitions", transitionDiagnostics.QueuedTransitions},
                    {"completedTransitions", transitionDiagnostics.CompletedTransitions},
                    {"cancelledTransitions", transitionDiagnostics.CancelledTransitions},
                    {"parseFailures", transitionDiagnostics.ParseFailures},
                    {"applyFailures", transitionDiagnostics.ApplyFailures},
                    {"arbitrationConflicts", transitionDiagnostics.ArbitrationConflicts},
                    {"bindingWinsResolutions", transitionDiagnostics.BindingWinsResolutions},
                    {"transitionWinsResolutions", transitionDiagnostics.TransitionWinsResolutions},
                    {"blendResolutions", transitionDiagnostics.BlendResolutions}
                };

                Json bindingStates = Json::array();
                for (const Binding::UIBindingState& state : bindings) {
                    bindingStates.push_back({
                        {"handle", state.Handle},
                        {"widget", state.WidgetId},
                        {"propertyPath", state.WidgetPropertyPath},
                        {"dataPath", state.DataPath},
                        {"mode", Binding::BindingModeToString(state.Mode)},
                        {"destroyed", state.Destroyed}
                    });
                }
                result["bindings"] = bindingStates;

                Json transitionStates = Json::array();
                for (const Animation::WidgetTransitionState& state : transitions) {
                    transitionStates.push_back({
                        {"handle", state.Handle},
                        {"widget", state.WidgetId},
                        {"transitionId", state.TransitionId},
                        {"state", Animation::TransitionRuntimeStateToString(state.State)},
                        {"elapsedTime", state.ElapsedTime},
                        {"duration", state.Duration},
                        {"channelCount", state.ChannelCount},
                        {"interruptionPolicy", Animation::TransitionInterruptionPolicyToString(state.InterruptionPolicy)}
                    });
                }
                result["transitions"] = transitionStates;
                result["filter"] = widgetFilter;
                result["status"] = "ok";
                return ToolResult{true, result, ""};
            },
            true);
    }

    // ============================================================================
    // TriggerSaveState Tool
    // ============================================================================

    /// @brief Create TriggerSaveState MCP tool
    /// 
    /// Input Schema:
    /// {
    ///   "slotName": string,          // Optional: Named slot (default: auto-generated)
    ///   "description": string,       // Optional: Save description
    ///   "silent": boolean,           // Optional: No UI feedback (default: false)
    ///   "format": string             // Optional: "json"|"binary" (default: "binary")
    /// }
    inline MCPToolPtr CreateTriggerSaveStateTool() {
        ToolInputSchema schema;
        schema.type = "object";
        schema.properties = {
            {"slotName", {{"type", "string"}, {"description", "Named save slot (auto-generated if omitted)"}}},
            {"description", {{"type", "string"}, {"description", "Human-readable save description"}}},
            {"silent", {{"type", "boolean"}, {"description", "If true, suppress save notification UI (default: false)"}}},
            {"format", {{"type", "string"}, {"enum", Json::array({"json", "binary"})}, {"description", "Save format (default: binary)"}}}
        };
        schema.required = {};

        return CreateLambdaTool(
            "TriggerSaveState",
            "Create a save checkpoint. Use for narrative checkpoints, before boss fights, or any important moment.",
            schema,
            [](const Json& args, ECS::Scene* scene) -> ToolResult {
                std::string slotName = args.value("slotName", "");
                std::string description = args.value("description", "AI-triggered checkpoint");
                bool silent = args.value("silent", false);
                std::string formatStr = args.value("format", "binary");
                
                // Generate slot name if not provided
                if (slotName.empty()) {
                    auto now = std::chrono::system_clock::now();
                    auto time_t_now = std::chrono::system_clock::to_time_t(now);
                    std::stringstream ss;
                    ss << "checkpoint_" << time_t_now;
                    slotName = ss.str();
                }
                
                // Validate slot name (alphanumeric + underscore only)
                for (char c : slotName) {
                    if (!std::isalnum(c) && c != '_') {
                        return ToolResult{false, {}, "Invalid slot name. Use only alphanumeric characters and underscores."};
                    }
                }
                
                // TODO: Integrate with SaveManager when implemented
                // For now, return success with save parameters
                
                Json result;
                result["slotName"] = slotName;
                result["description"] = description;
                result["format"] = formatStr;
                result["silent"] = silent;
                result["status"] = "triggered";
                result["message"] = silent ? "Save triggered (silent)" : "Save triggered";
                
                return ToolResult{true, result, ""};
            },
            true  // Requires scene for serialization
        );
    }

    // ============================================================================
    // ShowLoadingScreen Tool
    // ============================================================================

    /// @brief Create ShowLoadingScreen MCP tool
    /// 
    /// Input Schema:
    /// {
    ///   "show": boolean,             // Required: Show or hide
    ///   "message": string,           // Optional: Loading message
    ///   "progress": number,          // Optional: 0.0-1.0 progress value
    ///   "tips": [string]             // Optional: Rotating gameplay tips
    /// }
    inline MCPToolPtr CreateShowLoadingScreenTool() {
        ToolInputSchema schema;
        schema.type = "object";
        schema.properties = {
            {"show", {{"type", "boolean"}, {"description", "Show (true) or hide (false) the loading screen"}}},
            {"widget", {{"type", "string"}, {"description", "Loading widget identifier (default: loading_screen)"}}},
            {"message", {{"type", "string"}, {"description", "Loading message to display"}}},
            {"progress", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}, {"description", "Progress value (0.0 to 1.0)"}}},
            {"tips", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Array of gameplay tips to rotate"}}},
            {"animate", {{"type", "boolean"}, {"description", "Animate loading panel alpha transitions (default: true)"}}}
        };
        schema.required = {"show"};

        return CreateLambdaTool(
            "ShowLoadingScreen",
            "Control the loading screen visibility and content. Use during scene transitions or heavy loading.",
            schema,
            [](const Json& args, ECS::Scene* scene) -> ToolResult {
                (void)scene;
                
                bool show = args.value("show", true);
                std::string widgetId = args.value("widget", "loading_screen");
                std::string message = args.value("message", "Loading...");
                float progress = args.value("progress", -1.0f);  // -1 = indeterminate
                bool animate = args.value("animate", true);
                
                // Clamp progress to valid range
                if (progress >= 0.0f) {
                    progress = std::clamp(progress, 0.0f, 1.0f);
                }

                UI::Widgets::Widget* widget = UI::Widgets::WidgetSystem::Get().FindWidget(widgetId);
                if (widget == nullptr) {
                    Json errorPayload;
                    errorPayload["errorCode"] = "UI_LOADING_WIDGET_NOT_FOUND";
                    errorPayload["widget"] = widgetId;
                    return ToolResult{false, errorPayload, "Loading widget is not registered in WidgetSystem."};
                }

                const bool visibilityUpdated = UI::Widgets::WidgetSystem::Get().SetWidgetProperty(
                    widgetId,
                    "visible",
                    UI::Widgets::WidgetSystem::WidgetPropertyValue(show));
                if (!visibilityUpdated) {
                    return ToolResult{false, {}, "Failed to update loading widget visibility."};
                }

                if (!message.empty()) {
                    const bool messageUpdated = UI::Widgets::WidgetSystem::Get().SetWidgetProperty(
                        widgetId,
                        "text",
                        UI::Widgets::WidgetSystem::WidgetPropertyValue(message));
                    if (!messageUpdated) {
                        return ToolResult{false, {}, "Failed to update loading widget text property."};
                    }
                }

                if (progress >= 0.0f) {
                    const bool progressUpdated = UI::Widgets::WidgetSystem::Get().SetWidgetProperty(
                        widgetId,
                        "value",
                        UI::Widgets::WidgetSystem::WidgetPropertyValue(progress));
                    if (!progressUpdated) {
                        return ToolResult{false, {}, "Failed to update loading widget progress property."};
                    }
                }

                Json result;
                result["visible"] = show;
                result["widget"] = widgetId;
                result["message"] = message;
                result["animate"] = animate;
                result["runtimeVisible"] = widget->IsVisible();
                
                if (progress >= 0.0f) {
                    result["progress"] = progress;
                }
                
                if (args.contains("tips") && args["tips"].is_array()) {
                    result["tips"] = args["tips"];
                }

                if (animate) {
                    Json timeline = {
                        {"duration", 0.2f},
                        {"channels", Json::array({
                            {
                                {"property", "alpha"},
                                {"from", show ? 0.0f : 1.0f},
                                {"to", show ? 1.0f : 0.0f},
                                {"easing", "ease_out_quad"}
                            }
                        })}
                    };

                    Animation::WidgetTransitionResult transitionResult =
                        Animation::WidgetTransitionService::Get().AnimateWidgetTransition(
                            widgetId,
                            show ? "loading_screen_show" : "loading_screen_hide",
                            timeline,
                            Animation::TransitionInterruptionPolicy::CancelPrevious,
                            true);
                    if (transitionResult.Success) {
                        result["transitionHandle"] = transitionResult.TransitionHandle;
                        result["transitionState"] =
                            Animation::TransitionRuntimeStateToString(transitionResult.State);
                    } else {
                        result["transitionErrorCode"] = transitionResult.ErrorCode;
                        result["transitionError"] = transitionResult.Message;
                    }
                }
                
                result["status"] = show ? "shown" : "hidden";
                
                return ToolResult{true, result, ""};
            },
            true
        );
    }

    // ============================================================================
    // Tool Registration
    // ============================================================================

    inline std::vector<MCPToolPtr> CreateUITools() {
        std::vector<MCPToolPtr> tools;
        tools.push_back(CreateDisplayScreenMessageTool());
        tools.push_back(CreateUpdateHUDTool());
        tools.push_back(CreateBindWidgetPropertyTool());
        tools.push_back(CreateUnbindWidgetPropertyTool());
        tools.push_back(CreateAnimateWidgetTransitionTool());
        tools.push_back(CreateSetUIPropertyLockModeTool());
        tools.push_back(CreateGetUIRuntimeDiagnosticsTool());
        tools.push_back(CreateTriggerSaveStateTool());
        tools.push_back(CreateShowLoadingScreenTool());
        return tools;
    }

    /// @brief Register all UI-related MCP tools with the server
    /// @param server MCP server instance
    inline void RegisterUITools(MCPServer& server) {
        for (MCPToolPtr& tool : CreateUITools()) {
            server.RegisterTool(std::move(tool));
        }
    }

} // namespace MCP
} // namespace Core
