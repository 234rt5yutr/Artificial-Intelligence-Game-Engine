#pragma once

#include "EditorContext.h"
#include "Panels/ViewportPanel.h"

namespace Core {
namespace RHI { class VulkanContext; }
namespace Editor {

    class EditorModule {
    public:
        // `context` may be null (headless); the viewport then renders a
        // placeholder instead of the renderer's output.
        void Initialize(ECS::Scene* activeScene = nullptr, RHI::VulkanContext* context = nullptr);
        void Shutdown();

        void SetActiveScene(ECS::Scene* activeScene);
        ECS::Scene* GetActiveScene() const { return m_Context.ActiveScene; }

        void SetEnabled(bool enabled) { m_Enabled = enabled; }
        bool IsEnabled() const { return m_Enabled; }

        void Update(float deltaTime);
        void RenderPanels();

        ViewportPanel& GetViewportPanel() { return m_Viewport; }
        const ViewportPanel& GetViewportPanel() const { return m_Viewport; }

        EditorContext& GetContext() { return m_Context; }
        const EditorContext& GetContext() const { return m_Context; }

    private:
        EditorContext m_Context;
        ViewportPanel m_Viewport;
        float m_LastDeltaTime = 0.0f;
        bool m_Enabled = true;
    };

} // namespace Editor
} // namespace Core

