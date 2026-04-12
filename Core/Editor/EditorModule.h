#pragma once

#include "EditorContext.h"

namespace Core {
namespace Editor {

    class EditorModule {
    public:
        void Initialize(ECS::Scene* activeScene = nullptr);
        void Shutdown();

        void SetActiveScene(ECS::Scene* activeScene);
        ECS::Scene* GetActiveScene() const { return m_Context.ActiveScene; }

        void SetEnabled(bool enabled) { m_Enabled = enabled; }
        bool IsEnabled() const { return m_Enabled; }

        void Update(float deltaTime);
        void RenderPanels();

        EditorContext& GetContext() { return m_Context; }
        const EditorContext& GetContext() const { return m_Context; }

    private:
        EditorContext m_Context;
        bool m_Enabled = true;
    };

} // namespace Editor
} // namespace Core

