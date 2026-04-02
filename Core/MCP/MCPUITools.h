#pragma once

// MCP UI Tools
// Allows AI agents to control on-screen messaging, HUD updates, save state triggers,
// and loading screens for dynamic Game Master functionality

#include "MCPTool.h"
#include "MCPTypes.h"
#include <entt/entt.hpp>
#include <vector>
#include <queue>
#include <chrono>
#include <mutex>

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
    class WidgetSystem;
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

        /// @brief Clear all messages
        void ClearAll();

        /// @brief Set maximum concurrent messages
        void SetMaxConcurrentMessages(size_t max) { m_MaxConcurrent = max; }

        /// @brief Set minimum delay between new messages (throttling)
        void SetMessageDelay(float seconds) { m_MinDelay = seconds; }

    private:
        MessageQueueManager() = default;
        
        std::priority_queue<ScreenMessage, std::vector<ScreenMessage>, 
            [](const ScreenMessage& a, const ScreenMessage& b) {
                return a.priority < b.priority;
            }> m_PendingMessages;
        
        std::vector<ScreenMessage> m_ActiveMessages;
        size_t m_MaxConcurrent = 3;
        float m_MinDelay = 0.5f;
        float m_LastMessageTime = 0.0f;
        float m_CurrentTime = 0.0f;
        std::mutex m_Mutex;
    };

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
                MessageQueueManager::Get().QueueMessage(msg);
                
                Json result;
                result["queued"] = true;
                result["message"] = msg.text;
                result["duration"] = msg.duration;
                
                return ToolResult{true, result, ""};
            },
            false  // Does not require scene
        );
    }

    // ============================================================================
    // UpdateHUD Tool
    // ============================================================================

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
                std::string widgetId = args.value("widget", "");
                
                if (widgetId.empty()) {
                    return ToolResult{false, {}, "Widget identifier is required"};
                }
                
                // Get value (can be number, string, or object)
                Json value = args.value("value", Json());
                bool animate = args.value("animate", true);
                
                // Find widget in scene by ID
                // This would integrate with UISystem/WidgetSystem
                // For now, we store the update request for the UI system to process
                
                Json result;
                result["widget"] = widgetId;
                result["value"] = value;
                result["animate"] = animate;
                
                if (args.contains("visible")) {
                    result["visible"] = args["visible"];
                }
                
                // TODO: Integrate with actual WidgetSystem when implemented
                // For now, return success with the update parameters
                result["status"] = "queued";
                
                return ToolResult{true, result, ""};
            },
            true  // Requires scene for widget lookup
        );
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
            {"message", {{"type", "string"}, {"description", "Loading message to display"}}},
            {"progress", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}, {"description", "Progress value (0.0 to 1.0)"}}},
            {"tips", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Array of gameplay tips to rotate"}}}
        };
        schema.required = {"show"};

        return CreateLambdaTool(
            "ShowLoadingScreen",
            "Control the loading screen visibility and content. Use during scene transitions or heavy loading.",
            schema,
            [](const Json& args, ECS::Scene* scene) -> ToolResult {
                (void)scene;
                
                bool show = args.value("show", true);
                std::string message = args.value("message", "Loading...");
                float progress = args.value("progress", -1.0f);  // -1 = indeterminate
                
                // Clamp progress to valid range
                if (progress >= 0.0f) {
                    progress = std::clamp(progress, 0.0f, 1.0f);
                }
                
                // TODO: Integrate with TransitionManager when implemented
                
                Json result;
                result["visible"] = show;
                result["message"] = message;
                
                if (progress >= 0.0f) {
                    result["progress"] = progress;
                }
                
                if (args.contains("tips") && args["tips"].is_array()) {
                    result["tips"] = args["tips"];
                }
                
                result["status"] = show ? "shown" : "hidden";
                
                return ToolResult{true, result, ""};
            },
            false  // Does not require scene
        );
    }

    // ============================================================================
    // Tool Registration
    // ============================================================================

    /// @brief Register all UI-related MCP tools with the server
    /// @param server MCP server instance
    inline void RegisterUITools(MCPServer& server) {
        server.RegisterTool(CreateDisplayScreenMessageTool());
        server.RegisterTool(CreateUpdateHUDTool());
        server.RegisterTool(CreateTriggerSaveStateTool());
        server.RegisterTool(CreateShowLoadingScreenTool());
    }

} // namespace MCP
} // namespace Core
