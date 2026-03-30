#include "Core/ECS/Systems/LightSystem.h"

namespace Core {
namespace ECS {

    void LightSystem::Update(Scene& scene)
    {
        PROFILE_FUNCTION();

        // Clear previous frame data
        m_DirectionalLights.clear();
        m_PointLights.clear();
        m_SpotLights.clear();
        m_ForwardPlusLights.clear();

        auto view = scene.View<TransformComponent, LightComponent>();

        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& light = view.get<LightComponent>(entity);

            // Skip disabled lights
            if (!light.Enabled) {
                continue;
            }

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
