#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace Core {
namespace UI {
namespace World {

    constexpr const char* UI_WORLD_WIDGET_INVALID_REQUEST = "UI_WORLD_WIDGET_INVALID_REQUEST";

    enum class WorldWidgetDepthMode : uint8_t {
        DepthTest = 0,
        DepthFade,
        Overlay
    };

    struct WorldSpaceWidgetRenderRequest {
        std::string WidgetInstanceId;
        glm::vec3 WorldPosition = {0.0f, 0.0f, 0.0f};
        glm::vec3 CameraPosition = {0.0f, 0.0f, 0.0f};
        glm::mat4 ViewProjection = glm::mat4(1.0f);
        WorldWidgetDepthMode DepthMode = WorldWidgetDepthMode::DepthFade;
        bool RouteInteraction = true;
        bool PerformOcclusionCheck = false;
        bool EnableDistanceFade = true;
        float FadeStartDistance = 30.0f;
        float FadeEndDistance = 50.0f;
        bool ScaleWithDistance = true;
        float MinScale = 0.5f;
        float MaxScale = 2.0f;
        float ReferenceDistance = 10.0f;
    };

    struct WorldSpaceWidgetVisibilityDiagnostics {
        bool OnScreen = false;
        bool DepthOccluded = false;
        float Alpha = 0.0f;
        float DistanceToCamera = 0.0f;
        float Scale = 1.0f;
        glm::vec2 ScreenPosition = {-1000.0f, -1000.0f};
        std::string InteractionTarget;
    };

    struct WorldSpaceWidgetRenderResult {
        bool Success = false;
        std::string ErrorCode;
        std::string Message;
        WorldSpaceWidgetVisibilityDiagnostics Visibility;
    };

    class WorldSpaceWidgetRenderer {
    public:
        static WorldSpaceWidgetRenderer& Get();

        WorldSpaceWidgetRenderResult RenderWorldSpaceWidget(const WorldSpaceWidgetRenderRequest& request);

        std::optional<WorldSpaceWidgetVisibilityDiagnostics> GetDiagnosticsForWidget(
            const std::string& widgetId) const;
        const std::unordered_map<std::string, WorldSpaceWidgetVisibilityDiagnostics>&
        GetDiagnosticsSnapshot() const {
            return m_VisibilityDiagnostics;
        }
        void ClearDiagnostics();

    private:
        WorldSpaceWidgetRenderer() = default;

        static glm::vec2 ClipToScreen(const glm::vec4& clipPosition, bool* outOnScreen);
        static bool IsOccluded(const glm::vec4& clipPosition);

    private:
        std::unordered_map<std::string, WorldSpaceWidgetVisibilityDiagnostics> m_VisibilityDiagnostics;
    };

} // namespace World
} // namespace UI
} // namespace Core

