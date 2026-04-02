#pragma once

#include "Core/UI/Anchoring.h"
#include <string>
#include <cstdint>

namespace Core {
namespace ECS {

    /// @brief Widget types that can be displayed
    enum class WidgetType : uint8_t {
        None = 0,
        Label,          ///< Text label
        HealthBar,      ///< Health/progress bar
        ProgressBar,    ///< Generic progress bar
        Panel,          ///< Container panel
        Crosshair,      ///< Crosshair/reticle
        ObjectiveMarker,///< Objective waypoint marker
        MiniMap,        ///< Mini-map display
        MessageBox,     ///< Narrative text box
        AlertBox,       ///< Warning/alert popup
        Custom          ///< Custom widget (managed externally)
    };

    /// @brief UI Component for screen-space anchored widgets
    /// 
    /// This component defines UI elements that are anchored to the viewport
    /// and rendered in screen-space. Use WorldUIComponent for 3D-positioned UI.
    struct UIComponent {
        // Positioning
        UI::Anchor Anchor = UI::Anchor::TopLeft;
        glm::vec2 Offset = {0.0f, 0.0f};        ///< Offset from anchor in pixels
        glm::vec2 Size = {100.0f, 100.0f};      ///< Widget size in pixels
        UI::Anchor Pivot = UI::Anchor::TopLeft;  ///< Pivot point for positioning

        // Widget configuration
        WidgetType Type = WidgetType::None;
        std::string WidgetId;                    ///< Unique identifier for widget lookup
        std::string Text;                        ///< Text content (for Label, MessageBox)

        // Visual properties
        glm::vec4 Color = {1.0f, 1.0f, 1.0f, 1.0f};  ///< Tint color with alpha
        glm::vec4 BackgroundColor = {0.0f, 0.0f, 0.0f, 0.5f};
        float FontSize = 16.0f;
        std::string FontFamily = "default";

        // State
        bool Visible = true;
        bool Interactive = false;               ///< Can receive input focus
        int32_t ZOrder = 0;                     ///< Render order (higher = on top)
        float Progress = 1.0f;                  ///< For progress bars (0.0-1.0)

        // Animation
        float AnimationTime = 0.0f;            ///< Accumulated animation time
        bool FadeIn = false;
        bool FadeOut = false;
        float FadeDuration = 0.3f;

        // Dirty flag for render optimization
        bool IsDirty = true;
    };

    /// @brief Converts WidgetType to string
    inline const char* WidgetTypeToString(WidgetType type) {
        switch (type) {
            case WidgetType::None: return "none";
            case WidgetType::Label: return "label";
            case WidgetType::HealthBar: return "health_bar";
            case WidgetType::ProgressBar: return "progress_bar";
            case WidgetType::Panel: return "panel";
            case WidgetType::Crosshair: return "crosshair";
            case WidgetType::ObjectiveMarker: return "objective_marker";
            case WidgetType::MiniMap: return "mini_map";
            case WidgetType::MessageBox: return "message_box";
            case WidgetType::AlertBox: return "alert_box";
            case WidgetType::Custom: return "custom";
            default: return "unknown";
        }
    }

    /// @brief Converts string to WidgetType
    inline WidgetType StringToWidgetType(const std::string& str) {
        if (str == "label") return WidgetType::Label;
        if (str == "health_bar") return WidgetType::HealthBar;
        if (str == "progress_bar") return WidgetType::ProgressBar;
        if (str == "panel") return WidgetType::Panel;
        if (str == "crosshair") return WidgetType::Crosshair;
        if (str == "objective_marker") return WidgetType::ObjectiveMarker;
        if (str == "mini_map") return WidgetType::MiniMap;
        if (str == "message_box") return WidgetType::MessageBox;
        if (str == "alert_box") return WidgetType::AlertBox;
        if (str == "custom") return WidgetType::Custom;
        return WidgetType::None;
    }

} // namespace ECS
} // namespace Core
