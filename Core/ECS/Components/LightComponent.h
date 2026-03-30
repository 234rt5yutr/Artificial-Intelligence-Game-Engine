#pragma once

#include "Core/Math/Math.h"

namespace Core {
namespace ECS {

    enum class LightType : uint8_t {
        Directional = 0,
        Point = 1,
        Spot = 2
    };

    struct LightComponent {
        LightType Type = LightType::Point;

        // Common properties
        Math::Vec3 Color{ 1.0f, 1.0f, 1.0f };
        float Intensity = 1.0f;

        // Point/Spot light properties
        float Radius = 10.0f;           // Attenuation radius
        float InnerCutoff = 0.0f;       // Spot light inner cone angle (radians)
        float OuterCutoff = 0.0f;       // Spot light outer cone angle (radians)

        // Shadow properties
        bool CastShadows = false;
        float ShadowBias = 0.005f;
        uint32_t ShadowMapResolution = 1024;

        // State flags
        bool Enabled = true;

        LightComponent() = default;

        // Factory methods for convenience
        static LightComponent CreateDirectional(const Math::Vec3& color, float intensity, bool castShadows = true)
        {
            LightComponent light;
            light.Type = LightType::Directional;
            light.Color = color;
            light.Intensity = intensity;
            light.CastShadows = castShadows;
            return light;
        }

        static LightComponent CreatePoint(const Math::Vec3& color, float intensity, float radius)
        {
            LightComponent light;
            light.Type = LightType::Point;
            light.Color = color;
            light.Intensity = intensity;
            light.Radius = radius;
            return light;
        }

        static LightComponent CreateSpot(const Math::Vec3& color, float intensity, float radius,
                                          float innerCutoffDegrees, float outerCutoffDegrees)
        {
            LightComponent light;
            light.Type = LightType::Spot;
            light.Color = color;
            light.Intensity = intensity;
            light.Radius = radius;
            light.InnerCutoff = glm::radians(innerCutoffDegrees);
            light.OuterCutoff = glm::radians(outerCutoffDegrees);
            return light;
        }

        // Helper to get light direction from transform (for directional/spot lights)
        // Note: Direction is typically derived from the entity's TransformComponent
    };

} // namespace ECS
} // namespace Core
