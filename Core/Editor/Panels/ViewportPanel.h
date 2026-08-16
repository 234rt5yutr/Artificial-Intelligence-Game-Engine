#pragma once

// Editor scene viewport.
//
// The editor had a hierarchy and an inspector but nowhere to see the world: no
// viewport, no camera you can fly, no way to click an object, and no
// play-in-editor transition. All three panels were driving a scene the user
// could not look at.
//
// This panel displays the renderer's own post-upscale output as an ImGui image,
// so it shows exactly what the game sees - GI, upscaling and all - rather than a
// second, simplified render path that would drift from the real one.

#include "Core/Editor/EditorContext.h"
#include "Core/Math/Math.h"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace Core {
namespace RHI { class VulkanContext; }

namespace Editor {

    enum class GizmoMode : uint8_t {
        None = 0,
        Translate,
        Rotate,
        Scale
    };

    struct ViewportPanelState {
        bool Open = true;
        bool UseEditorCamera = false;
        bool ShowGrid = true;
        bool ShowStats = true;
        GizmoMode Gizmo = GizmoMode::Translate;
        float CameraSpeed = 6.0f;
        float MouseSensitivity = 0.0035f;
    };

    class ViewportPanel {
    public:
        // `context` may be null in headless runs; the panel then does nothing.
        void Initialize(RHI::VulkanContext* context);
        void Shutdown();

        // Draws the panel. Must run on the thread that owns the simulation,
        // because it reads and writes scene components.
        void Render(EditorContext& editorContext, float deltaTime);

        // True when the editor camera is driving the frame instead of the
        // scene's camera entities; fills the matrices in that case.
        bool GetCameraOverride(Math::Mat4& view, Math::Mat4& projection) const;

        ViewportPanelState& GetState() { return m_State; }
        const ViewportPanelState& GetState() const { return m_State; }

        void FocusOnSelection(EditorContext& editorContext);

        // Place the editor camera explicitly. Without this the camera could only
        // be flown by hand with the mouse, so nothing outside the window could
        // decide what the frame looks at - which made every headless check
        // depend on whatever the camera happened to be pointing at.
        void PlaceCamera(const Math::Vec3& position, const Math::Vec3& target);
        Math::Vec3 GetCameraPosition() const { return m_CameraPosition; }
        Math::Vec3 GetCameraForward() const;

    private:
        // Rebinds the ImGui descriptor when the renderer's output view changes
        // (resize, upscaler quality change).
        void EnsureTextureBinding();
        void UpdateEditorCamera(float deltaTime, bool viewportHovered);
        void HandlePicking(EditorContext& editorContext, const Math::Vec2& imageMin,
                           const Math::Vec2& imageSize);
        void DrawGizmo(EditorContext& editorContext, const Math::Vec2& imageMin,
                       const Math::Vec2& imageSize);
        void DrawToolbar(EditorContext& editorContext);
        Math::Mat4 BuildProjection() const;
        Math::Mat4 BuildView() const;

        RHI::VulkanContext* m_Context = nullptr;
        ViewportPanelState m_State{};

        VkDescriptorSet m_ImGuiTexture = VK_NULL_HANDLE;
        VkImageView m_BoundView = VK_NULL_HANDLE;

        Math::Vec3 m_CameraPosition{0.0f, 3.0f, 8.0f};
        float m_CameraYaw = -1.5707963f;   // looking down -Z
        float m_CameraPitch = -0.2f;
        float m_Aspect = 16.0f / 9.0f;

        // Gizmo drag state. The axis is latched on mouse-down so a fast drag
        // cannot slip onto a neighbouring handle mid-gesture.
        int m_DragAxis = -1;
        Math::Vec3 m_DragStartPosition{0.0f};
        Math::Vec2 m_DragStartMouse{0.0f};
    };

} // namespace Editor
} // namespace Core
