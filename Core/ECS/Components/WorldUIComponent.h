#pragma once

#include "UIComponent.h"
#include <glm/glm.hpp>
#include <string>

namespace Core {
namespace ECS {

    /// @brief Billboard mode for 3D widgets
    enum class BillboardMode : uint8_t {
        None = 0,           ///< Fixed orientation (use entity's rotation)
        FaceCamera,         ///< Always face the camera (full billboard)
        FaceCameraY,        ///< Face camera but only rotate around Y axis
        FaceCameraUp        ///< Face camera with world up vector
    };

    /// @brief World UI Component for 3D world-space positioned widgets
    /// 
    /// This component defines UI elements that exist in 3D world space
    /// and are projected to screen space during rendering. Used for
    /// health bars floating above enemies, nameplates, objective markers, etc.
    struct WorldUIComponent {
        // 3D Positioning (relative to entity's transform)
        glm::vec3 LocalOffset = {0.0f, 0.0f, 0.0f};  ///< Offset from entity position
        glm::vec2 ScreenOffset = {0.0f, 0.0f};       ///< Additional screen-space offset in pixels

        // Size
        glm::vec2 Size = {100.0f, 20.0f};           ///< Widget size in pixels (screen space)
        bool ScaleWithDistance = true;              ///< Scale size based on distance to camera
        float MinScale = 0.5f;                      ///< Minimum scale factor
        float MaxScale = 2.0f;                      ///< Maximum scale factor
        float ReferenceDistance = 10.0f;            ///< Distance at which scale = 1.0

        // Billboard
        BillboardMode Billboard = BillboardMode::FaceCamera;

        // Widget configuration (same as UIComponent)
        WidgetType Type = WidgetType::Label;
        std::string WidgetId;
        std::string Text;
        std::string BlueprintId;                ///< Optional Stage 27 blueprint reference
        std::string LayoutId;                   ///< Optional Stage 27 layout reference
        std::string DefaultModalDialogId;       ///< Optional modal launched by interaction
        bool RouteInteractionToModal = false;

        // Visual properties
        glm::vec4 Color = {1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 BackgroundColor = {0.0f, 0.0f, 0.0f, 0.7f};
        float FontSize = 14.0f;
        std::string FontFamily = "default";

        // Distance fade
        bool EnableDistanceFade = true;
        float FadeStartDistance = 30.0f;           ///< Start fading at this distance
        float FadeEndDistance = 50.0f;             ///< Fully transparent at this distance
        float CurrentAlpha = 1.0f;                 ///< Computed alpha after distance fade

        // Occlusion
        bool OccludeByGeometry = false;            ///< Test against depth buffer
        bool ClampToScreen = true;                 ///< Keep on screen edges when out of view

        // State
        bool Visible = true;
        bool IsOnScreen = true;                    ///< Computed each frame
        glm::vec2 ScreenPosition = {0.0f, 0.0f};  ///< Computed screen position
        float DistanceToCamera = 0.0f;            ///< Computed distance
        float Progress = 1.0f;                     ///< For progress bars

        // Render order
        int32_t ZOrder = 0;

        // Dirty flag
        bool IsDirty = true;
    };

    /// @brief Converts BillboardMode to string
    inline const char* BillboardModeToString(BillboardMode mode) {
        switch (mode) {
            case BillboardMode::None: return "none";
            case BillboardMode::FaceCamera: return "face_camera";
            case BillboardMode::FaceCameraY: return "face_camera_y";
            case BillboardMode::FaceCameraUp: return "face_camera_up";
            default: return "unknown";
        }
    }

    /// @brief Converts string to BillboardMode
    inline BillboardMode StringToBillboardMode(const std::string& str) {
        if (str == "none") return BillboardMode::None;
        if (str == "face_camera") return BillboardMode::FaceCamera;
        if (str == "face_camera_y") return BillboardMode::FaceCameraY;
        if (str == "face_camera_up") return BillboardMode::FaceCameraUp;
        return BillboardMode::FaceCamera;
    }

} // namespace ECS
} // namespace Core
