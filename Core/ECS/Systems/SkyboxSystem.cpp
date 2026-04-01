#include "Core/ECS/Systems/SkyboxSystem.h"

#include <ctime>

namespace Core {
namespace ECS {

    SkyboxSystem::SkyboxSystem()
        : m_AtmosphericSkybox(std::make_unique<Renderer::AtmosphericSkybox>())
    {
    }

    SkyboxSystem::~SkyboxSystem()
    {
        Shutdown();
    }

    void SkyboxSystem::Initialize(std::shared_ptr<RHI::RHIDevice> device)
    {
        PROFILE_FUNCTION();

        m_Device = device;

        if (m_AtmosphericSkybox) {
            m_AtmosphericSkybox->Initialize(device);
        }

        m_IsInitialized = true;
        ENGINE_CORE_INFO("SkyboxSystem initialized");
    }

    void SkyboxSystem::Shutdown()
    {
        PROFILE_FUNCTION();

        if (!m_IsInitialized) return;

        if (m_AtmosphericSkybox) {
            m_AtmosphericSkybox->Shutdown();
        }

        m_IsInitialized = false;
        ENGINE_CORE_INFO("SkyboxSystem shutdown");
    }

    void SkyboxSystem::Update(Scene& scene, float deltaTime)
    {
        PROFILE_FUNCTION();

        if (!m_IsInitialized) return;

        // Get camera position for skybox rendering
        Math::Vec3 cameraPos(0.0f);
        auto cameraView = scene.View<TransformComponent, CameraComponent>();
        for (auto entity : cameraView) {
            auto& transform = cameraView.get<TransformComponent>(entity);
            auto& camera = cameraView.get<CameraComponent>(entity);
            if (camera.IsActive) {
                cameraPos = Math::Vec3(transform.WorldMatrix[3]);
                break;
            }
        }

        if (m_AtmosphericSkybox) {
            m_AtmosphericSkybox->SetCameraPosition(cameraPos);
        }

        // Process skybox and time of day entities
        auto skyboxView = scene.View<SkyboxComponent>();
        auto timeView = scene.View<TimeOfDayComponent>();

        // Find active skybox
        SkyboxComponent* activeSkybox = nullptr;
        for (auto entity : skyboxView) {
            activeSkybox = &skyboxView.get<SkyboxComponent>(entity);
            break;  // Use first skybox found
        }

        // Find active time of day
        TimeOfDayComponent* activeTimeOfDay = nullptr;
        for (auto entity : timeView) {
            activeTimeOfDay = &timeView.get<TimeOfDayComponent>(entity);
            break;  // Use first time of day found
        }

        // Update time progression
        if (activeTimeOfDay) {
            UpdateTime(*activeTimeOfDay, deltaTime);
            m_CurrentTime = activeTimeOfDay->CurrentTime;

            // Calculate celestial positions
            CalculateCelestialPositions(*activeTimeOfDay);

            // Calculate sky colors
            CalculateSkyColors(*activeTimeOfDay);

            // Update directional light
            if (m_Config.AutoUpdateDirectionalLight) {
                UpdateDirectionalLight(scene, *activeTimeOfDay);
            }
        }

        // Update atmospheric skybox
        if (activeSkybox && m_AtmosphericSkybox) {
            m_AtmosphericSkybox->UpdateFromComponent(*activeSkybox, activeTimeOfDay);
        }

        ENGINE_CORE_TRACE("SkyboxSystem: Time={:.2f}, SunY={:.3f}, Ambient={:.3f}",
                          m_CurrentTime, m_SunDirection.y, m_AmbientIntensity);
    }

    void SkyboxSystem::Render(std::shared_ptr<RHI::RHICommandList> commandList,
                               const Math::Mat4& invViewProjection)
    {
        PROFILE_FUNCTION();

        if (!m_IsInitialized || !m_AtmosphericSkybox) return;

        m_AtmosphericSkybox->Render(commandList, invViewProjection);
    }

    void SkyboxSystem::SetTimeOfDay(float time)
    {
        m_CurrentTime = glm::mod(time, 24.0f);
    }

    void SkyboxSystem::UpdateTime(TimeOfDayComponent& timeOfDay, float deltaTime)
    {
        PROFILE_FUNCTION();

        if (timeOfDay.IsRealTime) {
            timeOfDay.CurrentTime = GetSystemTimeOfDay();
        } else {
            // Advance time based on speed
            // TimeSpeed: 1.0 = real-time, 60.0 = 1 minute per game second
            float hoursPerSecond = timeOfDay.TimeSpeed / 3600.0f;
            timeOfDay.CurrentTime += hoursPerSecond * deltaTime;

            // Wrap around at 24 hours
            if (timeOfDay.CurrentTime >= 24.0f) {
                timeOfDay.CurrentTime -= 24.0f;
            }
            if (timeOfDay.CurrentTime < 0.0f) {
                timeOfDay.CurrentTime += 24.0f;
            }
        }
    }

    void SkyboxSystem::CalculateCelestialPositions(const TimeOfDayComponent& timeOfDay)
    {
        PROFILE_FUNCTION();

        // Calculate sun direction using AtmosphericSkybox's method
        m_SunDirection = Renderer::AtmosphericSkybox::CalculateSunDirection(
            timeOfDay.CurrentTime, timeOfDay.Latitude);

        // Moon is roughly opposite the sun (simplified)
        m_MoonDirection = -m_SunDirection;
        // Add slight offset for more realistic moon position
        m_MoonDirection.x += 0.1f;
        m_MoonDirection = glm::normalize(m_MoonDirection);

        // Calculate sun color based on elevation
        m_SunColor = Renderer::AtmosphericSkybox::CalculateSunColor(m_SunDirection.y);

        // Calculate sun intensity (reduced at low elevations)
        if (m_SunDirection.y > 0.0f) {
            m_SunIntensity = glm::smoothstep(0.0f, 0.3f, m_SunDirection.y);
        } else {
            m_SunIntensity = 0.0f;
        }

        // Calculate ambient intensity
        float dayAmbient = timeOfDay.DayAmbientIntensity;
        float nightAmbient = timeOfDay.NightAmbientIntensity;

        if (m_SunDirection.y > 0.1f) {
            m_AmbientIntensity = dayAmbient;
        } else if (m_SunDirection.y > -0.1f) {
            float t = (m_SunDirection.y + 0.1f) / 0.2f;
            m_AmbientIntensity = glm::mix(nightAmbient, dayAmbient, t);
        } else {
            m_AmbientIntensity = nightAmbient;
        }
    }

    void SkyboxSystem::UpdateDirectionalLight(Scene& scene, const TimeOfDayComponent& timeOfDay)
    {
        PROFILE_FUNCTION();

        // Find linked directional light
        if (timeOfDay.LinkedDirectionalLight == entt::null) {
            // Try to find any directional light
            auto lightView = scene.View<TransformComponent, LightComponent>();
            for (auto entity : lightView) {
                auto& light = lightView.get<LightComponent>(entity);
                if (light.Type == LightType::Directional) {
                    auto& transform = lightView.get<TransformComponent>(entity);

                    // Update light direction to match sun
                    // Convert sun direction to Euler angles
                    float pitch = glm::asin(-m_SunDirection.y);
                    float yaw = glm::atan(m_SunDirection.x, m_SunDirection.z);

                    transform.Rotation = Math::Vec3(pitch, yaw, 0.0f);
                    transform.SetDirty();

                    // Update light color and intensity
                    light.Color = m_SunColor;
                    light.Intensity = m_SunIntensity * 2.0f;

                    // Only update first directional light
                    break;
                }
            }
        } else {
            // Use linked light
            if (scene.IsValid(timeOfDay.LinkedDirectionalLight)) {
                auto& transform = scene.Get<TransformComponent>(timeOfDay.LinkedDirectionalLight);
                auto& light = scene.Get<LightComponent>(timeOfDay.LinkedDirectionalLight);

                float pitch = glm::asin(-m_SunDirection.y);
                float yaw = glm::atan(m_SunDirection.x, m_SunDirection.z);

                transform.Rotation = Math::Vec3(pitch, yaw, 0.0f);
                transform.SetDirty();

                light.Color = m_SunColor;
                light.Intensity = m_SunIntensity * 2.0f;
            }
        }
    }

    void SkyboxSystem::CalculateSkyColors(const TimeOfDayComponent& timeOfDay)
    {
        PROFILE_FUNCTION();

        float sunElevation = m_SunDirection.y;

        // Interpolate sky colors based on sun elevation
        if (sunElevation > 0.2f) {
            // Daytime
            float t = glm::smoothstep(0.2f, 0.5f, sunElevation);
            m_ZenithColor = glm::mix(timeOfDay.SunsetZenithColor, timeOfDay.NoonZenithColor, t);
            m_HorizonColor = glm::mix(timeOfDay.SunsetHorizonColor, timeOfDay.NoonHorizonColor, t);
        } else if (sunElevation > -0.1f) {
            // Sunset/sunrise
            float t = (sunElevation + 0.1f) / 0.3f;
            m_ZenithColor = glm::mix(timeOfDay.NightZenithColor, timeOfDay.SunsetZenithColor, t);
            m_HorizonColor = glm::mix(timeOfDay.NightHorizonColor, timeOfDay.SunsetHorizonColor, t);
        } else {
            // Night
            m_ZenithColor = timeOfDay.NightZenithColor;
            m_HorizonColor = timeOfDay.NightHorizonColor;
        }
    }

    float SkyboxSystem::GetSystemTimeOfDay()
    {
        // Get current system time
        std::time_t now = std::time(nullptr);
        std::tm* localTime = std::localtime(&now);

        float hours = static_cast<float>(localTime->tm_hour);
        float minutes = static_cast<float>(localTime->tm_min) / 60.0f;
        float seconds = static_cast<float>(localTime->tm_sec) / 3600.0f;

        return hours + minutes + seconds;
    }

} // namespace ECS
} // namespace Core
