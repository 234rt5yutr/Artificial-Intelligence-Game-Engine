#pragma once

#include "Core/ECS/Components/UIComponent.h"
#include "Core/ECS/Components/WorldUIComponent.h"
#include "Core/UI/Anchoring.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace Core {

// Forward declarations
namespace UI {
    class TextRenderer;
    class UIManager;
}

namespace ECS {

    class Scene;

    /// @brief System for updating UI components
    /// 
    /// UISystem handles:
    /// - Updating screen-space UIComponent positions based on anchors
    /// - Projecting WorldUIComponent positions to screen space
    /// - Managing visibility and distance fade for world UI
    /// - Handling widget dirty states
    class UISystem {
    public:
        UISystem() = default;
        ~UISystem() = default;

        /// @brief Initialize the UI system
        /// @param scene The ECS scene to operate on
        void Initialize(Scene* scene);

        /// @brief Shutdown the system
        void Shutdown();

        /// @brief Update all UI components
        /// @param deltaTime Time since last frame in seconds
        /// @param viewportSize Current viewport dimensions
        void Update(float deltaTime, const glm::vec2& viewportSize);

        /// @brief Update world UI components with camera information
        /// @param viewMatrix Camera view matrix
        /// @param projMatrix Camera projection matrix
        /// @param cameraPosition Camera world position
        /// @param viewportSize Viewport dimensions
        void UpdateWorldUI(const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                           const glm::vec3& cameraPosition, const glm::vec2& viewportSize);

        /// @brief Render all UI components
        /// @param textRenderer Text renderer for text elements
        /// @param viewportSize Viewport dimensions
        void Render(UI::TextRenderer& textRenderer, const glm::vec2& viewportSize);

        /// @brief Find widget by ID
        /// @param widgetId Widget identifier
        /// @return Entity with matching widget ID, or entt::null if not found
        entt::entity FindWidgetById(const std::string& widgetId);

        /// @brief Set widget visibility
        bool SetWidgetVisible(const std::string& widgetId, bool visible);

        /// @brief Update widget text
        bool SetWidgetText(const std::string& widgetId, const std::string& text);

        /// @brief Update widget progress (for progress bars)
        bool SetWidgetProgress(const std::string& widgetId, float progress);

    private:
        /// @brief Update a single UIComponent
        void UpdateUIComponent(UIComponent& ui, float deltaTime, const glm::vec2& viewportSize);

        /// @brief Update a single WorldUIComponent
        void UpdateWorldUIComponent(WorldUIComponent& worldUI, const glm::vec3& entityPosition,
                                     const glm::mat4& viewProj, const glm::vec3& cameraPosition,
                                     const glm::vec2& viewportSize);

        /// @brief Project world position to screen coordinates
        glm::vec2 WorldToScreen(const glm::vec3& worldPos, const glm::mat4& viewProj,
                                 const glm::vec2& viewportSize, bool& outOnScreen);

        /// @brief Render a UIComponent
        void RenderUIComponent(const UIComponent& ui, UI::TextRenderer& textRenderer,
                               const glm::vec2& viewportSize);

        /// @brief Render a WorldUIComponent
        void RenderWorldUIComponent(const WorldUIComponent& worldUI, UI::TextRenderer& textRenderer);

    private:
        Scene* m_Scene = nullptr;
        bool m_Initialized = false;
    };

} // namespace ECS
} // namespace Core
