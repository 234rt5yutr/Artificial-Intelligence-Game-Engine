#include "UISystem.h"
#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/UI/TextRenderer.h"
#include "Core/UI/World/WorldSpaceWidgetRenderer.h"
#include "Core/Log.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Core {
namespace ECS {

// ============================================================================
// Lifecycle
// ============================================================================

void UISystem::Initialize(Scene* scene) {
    m_Scene = scene;
    m_Initialized = true;
    ENGINE_CORE_INFO("UISystem initialized");
}

void UISystem::Shutdown() {
    m_Scene = nullptr;
    m_Initialized = false;
    ENGINE_CORE_INFO("UISystem shut down");
}

// ============================================================================
// Update
// ============================================================================

void UISystem::Update(float deltaTime, const glm::vec2& viewportSize) {
    if (!m_Initialized || !m_Scene) {
        return;
    }

    auto& registry = m_Scene->GetRegistry();

    // Update screen-space UI components
    auto uiView = registry.view<UIComponent>();
    for (auto entity : uiView) {
        auto& ui = uiView.get<UIComponent>(entity);
        UpdateUIComponent(ui, deltaTime, viewportSize);
    }
}

void UISystem::UpdateWorldUI(const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                             const glm::vec3& cameraPosition, const glm::vec2& viewportSize) {
    if (!m_Initialized || !m_Scene) {
        return;
    }

    auto& registry = m_Scene->GetRegistry();
    glm::mat4 viewProj = projMatrix * viewMatrix;

    // Update world-space UI components
    auto worldUIView = registry.view<WorldUIComponent, TransformComponent>();
    for (auto entity : worldUIView) {
        auto& worldUI = worldUIView.get<WorldUIComponent>(entity);
        auto& transform = worldUIView.get<TransformComponent>(entity);
        
        glm::vec3 worldPos = transform.Position + worldUI.LocalOffset;
        UpdateWorldUIComponent(worldUI, worldPos, viewProj, cameraPosition, viewportSize);
    }
}

void UISystem::UpdateUIComponent(UIComponent& ui, float deltaTime, const glm::vec2& viewportSize) {
    (void)viewportSize;
    // Update animation
    if (ui.FadeIn || ui.FadeOut) {
        ui.AnimationTime += deltaTime;
        float t = ui.AnimationTime / ui.FadeDuration;
        
        if (t >= 1.0f) {
            ui.AnimationTime = 0.0f;
            if (ui.FadeIn) {
                ui.FadeIn = false;
                ui.Color.a = 1.0f;
            }
            if (ui.FadeOut) {
                ui.FadeOut = false;
                ui.Color.a = 0.0f;
                ui.Visible = false;
            }
        } else {
            // Smooth step
            t = t * t * (3.0f - 2.0f * t);
            if (ui.FadeIn) {
                ui.Color.a = t;
            } else if (ui.FadeOut) {
                ui.Color.a = 1.0f - t;
            }
        }
    }
}

void UISystem::UpdateWorldUIComponent(WorldUIComponent& worldUI, const glm::vec3& entityPosition,
                                       const glm::mat4& viewProj, const glm::vec3& cameraPosition,
                                       const glm::vec2& viewportSize) {
    (void)viewportSize;
    UI::World::WorldSpaceWidgetRenderRequest request;
    request.WidgetInstanceId = worldUI.WidgetId;
    request.WorldPosition = entityPosition;
    request.CameraPosition = cameraPosition;
    request.ViewProjection = viewProj;
    request.PerformOcclusionCheck = worldUI.OccludeByGeometry;
    request.EnableDistanceFade = worldUI.EnableDistanceFade;
    request.FadeStartDistance = worldUI.FadeStartDistance;
    request.FadeEndDistance = worldUI.FadeEndDistance;
    request.ScaleWithDistance = worldUI.ScaleWithDistance;
    request.MinScale = worldUI.MinScale;
    request.MaxScale = worldUI.MaxScale;
    request.ReferenceDistance = worldUI.ReferenceDistance;
    request.RouteInteraction = true;
    if (worldUI.OccludeByGeometry && worldUI.EnableDistanceFade) {
        request.DepthMode = UI::World::WorldWidgetDepthMode::DepthFade;
    } else if (worldUI.OccludeByGeometry) {
        request.DepthMode = UI::World::WorldWidgetDepthMode::DepthTest;
    } else {
        request.DepthMode = UI::World::WorldWidgetDepthMode::Overlay;
    }

    const UI::World::WorldSpaceWidgetRenderResult renderResult =
        UI::World::WorldSpaceWidgetRenderer::Get().RenderWorldSpaceWidget(request);
    if (!renderResult.Success) {
        worldUI.DistanceToCamera = glm::length(entityPosition - cameraPosition);
        worldUI.ScreenPosition = WorldToScreen(entityPosition, viewProj, viewportSize, worldUI.IsOnScreen);
        worldUI.CurrentAlpha = worldUI.IsOnScreen ? 1.0f : 0.0f;
        return;
    }

    worldUI.DistanceToCamera = renderResult.Visibility.DistanceToCamera;
    worldUI.ScreenPosition = renderResult.Visibility.ScreenPosition;
    worldUI.IsOnScreen = renderResult.Visibility.OnScreen;
    worldUI.CurrentAlpha = renderResult.Visibility.Alpha;
}

glm::vec2 UISystem::WorldToScreen(const glm::vec3& worldPos, const glm::mat4& viewProj,
                                   const glm::vec2& viewportSize, bool& outOnScreen) {
    // Transform to clip space
    glm::vec4 clipPos = viewProj * glm::vec4(worldPos, 1.0f);

    // Check if behind camera
    if (clipPos.w <= 0.0f) {
        outOnScreen = false;
        return {-1000.0f, -1000.0f};
    }

    // Perspective divide to NDC
    glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;

    // Check if within NDC bounds
    outOnScreen = (ndc.x >= -1.0f && ndc.x <= 1.0f &&
                   ndc.y >= -1.0f && ndc.y <= 1.0f &&
                   ndc.z >= 0.0f && ndc.z <= 1.0f);

    // Convert to screen coordinates (flip Y for screen-space)
    glm::vec2 screenPos;
    screenPos.x = (ndc.x + 1.0f) * 0.5f * viewportSize.x;
    screenPos.y = (1.0f - ndc.y) * 0.5f * viewportSize.y;

    return screenPos;
}

// ============================================================================
// Render
// ============================================================================

void UISystem::Render(UI::TextRenderer& textRenderer, const glm::vec2& viewportSize) {
    if (!m_Initialized || !m_Scene) {
        return;
    }

    auto& registry = m_Scene->GetRegistry();

    // Render screen-space UI
    auto uiView = registry.view<UIComponent>();
    for (auto entity : uiView) {
        auto& ui = uiView.get<UIComponent>(entity);
        if (ui.Visible && ui.Color.a > 0.01f) {
            RenderUIComponent(ui, textRenderer, viewportSize);
        }
    }

    // Render world-space UI
    auto worldUIView = registry.view<WorldUIComponent>();
    for (auto entity : worldUIView) {
        auto& worldUI = worldUIView.get<WorldUIComponent>(entity);
        if (worldUI.Visible && worldUI.IsOnScreen && worldUI.CurrentAlpha > 0.01f) {
            RenderWorldUIComponent(worldUI, textRenderer);
        }
    }
}

void UISystem::RenderUIComponent(const UIComponent& ui, UI::TextRenderer& textRenderer,
                                  const glm::vec2& viewportSize) {
    if (ui.Type == WidgetType::None) {
        return;
    }

    // Calculate screen position
    glm::vec2 screenPos = UI::AnchorUtils::CalculatePosition(ui.Anchor, ui.Offset, viewportSize);

    // Apply pivot offset
    glm::vec2 pivotOffset = UI::AnchorUtils::GetNormalizedAnchor(ui.Pivot) * ui.Size;
    screenPos -= pivotOffset;

    // Render based on widget type
    if (ui.Type == WidgetType::Label || ui.Type == WidgetType::MessageBox) {
        UI::TextStyle style;
        style.fontFamily = ui.FontFamily;
        style.fontSize = ui.FontSize;
        style.color = ui.Color;
        textRenderer.DrawText(ui.Text, screenPos, style);
    }
    // Other widget types would be rendered here
}

void UISystem::RenderWorldUIComponent(const WorldUIComponent& worldUI, UI::TextRenderer& textRenderer) {
    if (worldUI.Type == WidgetType::None) {
        return;
    }

    glm::vec2 screenPos = worldUI.ScreenPosition + worldUI.ScreenOffset;

    // Apply alpha
    glm::vec4 color = worldUI.Color;
    color.a *= worldUI.CurrentAlpha;

    if (worldUI.Type == WidgetType::Label) {
        UI::TextStyle style;
        style.fontFamily = worldUI.FontFamily;
        style.fontSize = worldUI.FontSize;
        style.color = color;
        style.hAlign = UI::HorizontalAlign::Center;
        style.vAlign = UI::VerticalAlign::Center;
        textRenderer.DrawText(worldUI.Text, screenPos, style);
    }
}

// ============================================================================
// Widget Management
// ============================================================================

entt::entity UISystem::FindWidgetById(const std::string& widgetId) {
    if (!m_Scene) {
        return entt::null;
    }

    auto& registry = m_Scene->GetRegistry();

    // Search screen-space UI
    auto uiView = registry.view<UIComponent>();
    for (auto entity : uiView) {
        if (uiView.get<UIComponent>(entity).WidgetId == widgetId) {
            return entity;
        }
    }

    // Search world-space UI
    auto worldUIView = registry.view<WorldUIComponent>();
    for (auto entity : worldUIView) {
        if (worldUIView.get<WorldUIComponent>(entity).WidgetId == widgetId) {
            return entity;
        }
    }

    return entt::null;
}

bool UISystem::SetWidgetVisible(const std::string& widgetId, bool visible) {
    if (!m_Scene) return false;

    auto entity = FindWidgetById(widgetId);
    if (entity == entt::null) return false;

    auto& registry = m_Scene->GetRegistry();
    
    if (registry.all_of<UIComponent>(entity)) {
        registry.get<UIComponent>(entity).Visible = visible;
        return true;
    }
    if (registry.all_of<WorldUIComponent>(entity)) {
        registry.get<WorldUIComponent>(entity).Visible = visible;
        return true;
    }

    return false;
}

bool UISystem::SetWidgetText(const std::string& widgetId, const std::string& text) {
    if (!m_Scene) return false;

    auto entity = FindWidgetById(widgetId);
    if (entity == entt::null) return false;

    auto& registry = m_Scene->GetRegistry();
    
    if (registry.all_of<UIComponent>(entity)) {
        registry.get<UIComponent>(entity).Text = text;
        registry.get<UIComponent>(entity).IsDirty = true;
        return true;
    }
    if (registry.all_of<WorldUIComponent>(entity)) {
        registry.get<WorldUIComponent>(entity).Text = text;
        registry.get<WorldUIComponent>(entity).IsDirty = true;
        return true;
    }

    return false;
}

bool UISystem::SetWidgetProgress(const std::string& widgetId, float progress) {
    if (!m_Scene) return false;

    auto entity = FindWidgetById(widgetId);
    if (entity == entt::null) return false;

    auto& registry = m_Scene->GetRegistry();
    
    if (registry.all_of<UIComponent>(entity)) {
        registry.get<UIComponent>(entity).Progress = glm::clamp(progress, 0.0f, 1.0f);
        registry.get<UIComponent>(entity).IsDirty = true;
        return true;
    }
    if (registry.all_of<WorldUIComponent>(entity)) {
        registry.get<WorldUIComponent>(entity).Progress = glm::clamp(progress, 0.0f, 1.0f);
        registry.get<WorldUIComponent>(entity).IsDirty = true;
        return true;
    }

    return false;
}

} // namespace ECS
} // namespace Core
