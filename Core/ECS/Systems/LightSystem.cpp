#include "Core/ECS/Systems/LightSystem.h"

#include <algorithm>
#include <cstdint>

namespace Core {
namespace ECS {
    namespace {
        uint64_t ComposeLightCacheKey(const uint32_t entityId, const LightComponent& light)
        {
            uint64_t key = static_cast<uint64_t>(entityId) << 32U;
            key ^= static_cast<uint64_t>(light.Type) << 24U;
            key ^= static_cast<uint64_t>(light.ShadowMapResolution) << 8U;
            key ^= static_cast<uint64_t>(light.CastShadows ? 1U : 0U);
            return key;
        }

        LightQualityTier ComputeShadowTier(const LightComponent& light)
        {
            if (!light.CastShadows) {
                return LightQualityTier::Low;
            }
            if (light.ShadowMapResolution >= 2048U && light.Intensity >= 1.5f) {
                return LightQualityTier::High;
            }
            if (light.ShadowMapResolution <= 1024U || light.Intensity < 0.75f) {
                return LightQualityTier::Low;
            }
            return LightQualityTier::Medium;
        }
    } // namespace

    void LightSystem::Update(Scene& scene)
    {
        PROFILE_FUNCTION();

        // Clear previous frame data
        m_DirectionalLights.clear();
        m_PointLights.clear();
        m_SpotLights.clear();
        m_ForwardPlusLights.clear();
        m_LightRoutingHints.clear();

        auto view = scene.View<TransformComponent, LightComponent>();

        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& light = view.get<LightComponent>(entity);

            // Skip disabled lights
            if (!light.Enabled) {
                continue;
            }

            LightRoutingHint routingHint{};
            routingHint.CacheKey = ComposeLightCacheKey(static_cast<uint32_t>(entity), light);
            routingHint.CastShadows = light.CastShadows;
            routingHint.ShadowTier = ComputeShadowTier(light);
            routingHint.PreferRTShadow = light.CastShadows && light.ShadowMapResolution >= 2048U;
            m_LightRoutingHints.push_back(routingHint);

            switch (light.Type) {
                case LightType::Directional: {
                    DirectionalLightData dirLight;
                    // Direction is the forward vector of the transform (negative Z in local space)
                    dirLight.Direction = -transform.GetForward();
                    dirLight.Color = light.Color;
                    dirLight.Intensity = light.Intensity;
                    m_DirectionalLights.push_back(dirLight);
                    break;
                }

                case LightType::Point: {
                    PointLightData pointLight;
                    pointLight.Position = Math::Vec3(transform.WorldMatrix[3]);
                    pointLight.Radius = light.Radius;
                    pointLight.Color = light.Color;
                    pointLight.Intensity = light.Intensity;
                    m_PointLights.push_back(pointLight);

                    // Also add to Forward+ compatible format
                    Renderer::PointLight fpLight;
                    fpLight.positionAndRadius = Math::Vec4(pointLight.Position, light.Radius);
                    fpLight.colorAndIntensity = Math::Vec4(light.Color, light.Intensity);
                    m_ForwardPlusLights.push_back(fpLight);
                    break;
                }

                case LightType::Spot: {
                    SpotLightData spotLight;
                    spotLight.Position = Math::Vec3(transform.WorldMatrix[3]);
                    spotLight.Direction = -transform.GetForward();
                    spotLight.Radius = light.Radius;
                    spotLight.Color = light.Color;
                    spotLight.Intensity = light.Intensity;
                    spotLight.InnerCutoff = light.InnerCutoff;
                    spotLight.OuterCutoff = light.OuterCutoff;
                    m_SpotLights.push_back(spotLight);
                    break;
                }
            }
        }

        ENGINE_CORE_TRACE("LightSystem: {} dir, {} point, {} spot lights",
                          m_DirectionalLights.size(), m_PointLights.size(), m_SpotLights.size());
    }

} // namespace ECS
} // namespace Core
