#include "Core/UI/World/WorldSpaceWidgetRenderer.h"

#include "Core/UI/Widgets/WidgetSystem.h"

namespace Core {
namespace UI {
namespace World {

    WorldSpaceWidgetRenderer& WorldSpaceWidgetRenderer::Get() {
        static WorldSpaceWidgetRenderer instance;
        return instance;
    }

    WorldSpaceWidgetRenderResult WorldSpaceWidgetRenderer::RenderWorldSpaceWidget(
        const WorldSpaceWidgetRenderRequest& request) {
        WorldSpaceWidgetRenderResult result;

        if (request.FadeEndDistance > 0.0f && request.FadeEndDistance < request.FadeStartDistance) {
            result.Success = false;
            result.ErrorCode = UI_WORLD_WIDGET_INVALID_REQUEST;
            result.Message = "FadeEndDistance must be greater than or equal to FadeStartDistance.";
            return result;
        }

        const glm::vec4 clipPosition =
            request.ViewProjection * glm::vec4(request.WorldPosition, 1.0f);
        bool onScreen = false;
        glm::vec2 screenPosition = ClipToScreen(clipPosition, &onScreen);

        const float distanceToCamera = glm::length(request.WorldPosition - request.CameraPosition);
        bool occluded = request.PerformOcclusionCheck && IsOccluded(clipPosition);

        float alpha = 1.0f;
        if (request.EnableDistanceFade && request.FadeEndDistance > request.FadeStartDistance) {
            if (distanceToCamera <= request.FadeStartDistance) {
                alpha = 1.0f;
            } else if (distanceToCamera >= request.FadeEndDistance) {
                alpha = 0.0f;
            } else {
                const float t =
                    (distanceToCamera - request.FadeStartDistance) /
                    (request.FadeEndDistance - request.FadeStartDistance);
                alpha = 1.0f - glm::clamp(t, 0.0f, 1.0f);
            }
        }

        float scale = 1.0f;
        if (request.ScaleWithDistance) {
            const float rawScale = request.ReferenceDistance / glm::max(distanceToCamera, 0.01f);
            scale = glm::clamp(rawScale, request.MinScale, request.MaxScale);
        }

        switch (request.DepthMode) {
            case WorldWidgetDepthMode::DepthTest:
                if (occluded) {
                    alpha = 0.0f;
                }
                break;
            case WorldWidgetDepthMode::DepthFade:
                if (occluded) {
                    alpha *= 0.35f;
                }
                break;
            case WorldWidgetDepthMode::Overlay:
            default:
                break;
        }

        if (!onScreen || clipPosition.w <= 0.0f) {
            alpha = 0.0f;
        }

        const bool shouldInteract = request.RouteInteraction && onScreen && !occluded && alpha > 0.01f;

        Widget* widget = nullptr;
        if (!request.WidgetInstanceId.empty()) {
            widget = Widgets::WidgetSystem::Get().FindWidget(request.WidgetInstanceId);
        }

        if (widget != nullptr) {
            (void)Widgets::WidgetSystem::Get().SetWidgetProperty(
                request.WidgetInstanceId,
                "position",
                Widgets::WidgetSystem::WidgetPropertyValue(screenPosition));
            (void)Widgets::WidgetSystem::Get().SetWidgetProperty(
                request.WidgetInstanceId,
                "scale",
                Widgets::WidgetSystem::WidgetPropertyValue(glm::vec2(scale, scale)));
            (void)Widgets::WidgetSystem::Get().SetWidgetProperty(
                request.WidgetInstanceId,
                "alpha",
                Widgets::WidgetSystem::WidgetPropertyValue(glm::clamp(alpha, 0.0f, 1.0f)));
            (void)Widgets::WidgetSystem::Get().SetWidgetProperty(
                request.WidgetInstanceId,
                "visible",
                Widgets::WidgetSystem::WidgetPropertyValue(alpha > 0.01f));
            if (request.RouteInteraction) {
                (void)Widgets::WidgetSystem::Get().SetWidgetProperty(
                    request.WidgetInstanceId,
                    "interactive",
                    Widgets::WidgetSystem::WidgetPropertyValue(shouldInteract));
            }
        }

        result.Success = true;
        result.Visibility.OnScreen = onScreen;
        result.Visibility.DepthOccluded = occluded;
        result.Visibility.Alpha = glm::clamp(alpha, 0.0f, 1.0f);
        result.Visibility.DistanceToCamera = distanceToCamera;
        result.Visibility.Scale = scale;
        result.Visibility.ScreenPosition = screenPosition;
        result.Visibility.InteractionTarget = shouldInteract ? request.WidgetInstanceId : std::string{};

        if (!request.WidgetInstanceId.empty()) {
            m_VisibilityDiagnostics[request.WidgetInstanceId] = result.Visibility;
        }
        return result;
    }

    std::optional<WorldSpaceWidgetVisibilityDiagnostics> WorldSpaceWidgetRenderer::GetDiagnosticsForWidget(
        const std::string& widgetId) const {
        auto diagnosticsIt = m_VisibilityDiagnostics.find(widgetId);
        if (diagnosticsIt == m_VisibilityDiagnostics.end()) {
            return std::nullopt;
        }
        return diagnosticsIt->second;
    }

    void WorldSpaceWidgetRenderer::ClearDiagnostics() {
        m_VisibilityDiagnostics.clear();
    }

    glm::vec2 WorldSpaceWidgetRenderer::ClipToScreen(
        const glm::vec4& clipPosition,
        bool* outOnScreen) {
        if (outOnScreen != nullptr) {
            *outOnScreen = false;
        }
        if (clipPosition.w <= 0.0f) {
            return {-1000.0f, -1000.0f};
        }

        const glm::vec3 ndc = glm::vec3(clipPosition) / clipPosition.w;
        if (outOnScreen != nullptr) {
            *outOnScreen = ndc.x >= -1.0f && ndc.x <= 1.0f &&
                           ndc.y >= -1.0f && ndc.y <= 1.0f &&
                           ndc.z >= 0.0f && ndc.z <= 1.0f;
        }

        return {
            (ndc.x + 1.0f) * 0.5f * Widgets::WidgetSystem::Get().GetScreenSize().x,
            (1.0f - ndc.y) * 0.5f * Widgets::WidgetSystem::Get().GetScreenSize().y
        };
    }

    bool WorldSpaceWidgetRenderer::IsOccluded(const glm::vec4& clipPosition) {
        if (clipPosition.w <= 0.0f) {
            return true;
        }
        const float ndcDepth = (clipPosition.z / clipPosition.w);
        return ndcDepth > 0.98f;
    }

} // namespace World
} // namespace UI
} // namespace Core

