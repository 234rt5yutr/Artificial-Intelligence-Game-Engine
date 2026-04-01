#pragma once

// SkyboxSystem - Manages atmospheric skybox and day/night cycle
// Updates sun/moon positions, directional light, and sky parameters based on time

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/SkyboxComponent.h"
#include "Core/ECS/Components/TimeOfDayComponent.h"
#include "Core/ECS/Components/LightComponent.h"
#include "Core/ECS/Components/CameraComponent.h"
#include "Core/Renderer/Skybox/AtmosphericSkybox.h"
#include "Core/Profile.h"
#include "Core/Log.h"

#include <memory>
#include <chrono>

namespace Core {

// Forward declarations
namespace RHI {
    class RHIDevice;
    class RHICommandList;
}

namespace ECS {

    // Configuration for skybox system
    struct SkyboxSystemConfig {
        bool AutoUpdateDirectionalLight = true;
        bool EnableStars = true;
        bool EnableMoon = true;
        float TransitionSmoothness = 5.0f;
    };

    class SkyboxSystem {
    public:
        SkyboxSystem();
        ~SkyboxSystem();

        // Non-copyable
        SkyboxSystem(const SkyboxSystem&) = delete;
        SkyboxSystem& operator=(const SkyboxSystem&) = delete;

        // Initialize with RHI device
        void Initialize(std::shared_ptr<RHI::RHIDevice> device);

        // Shutdown and cleanup
        void Shutdown();

        // Update skybox and time of day
        void Update(Scene& scene, float deltaTime);

        // Render skybox (should be called during rendering)
        void Render(std::shared_ptr<RHI::RHICommandList> commandList,
                    const Math::Mat4& invViewProjection);

        // Configuration
        void SetConfig(const SkyboxSystemConfig& config) { m_Config = config; }
        const SkyboxSystemConfig& GetConfig() const { return m_Config; }

        // Time of day control
        void SetTimeOfDay(float time);
        float GetTimeOfDay() const { return m_CurrentTime; }

        // Get calculated sun direction
        Math::Vec3 GetSunDirection() const { return m_SunDirection; }

        // Get calculated ambient light intensity
        float GetAmbientIntensity() const { return m_AmbientIntensity; }

        // Access atmospheric skybox renderer
        Renderer::AtmosphericSkybox* GetAtmosphericSkybox() { return m_AtmosphericSkybox.get(); }

    private:
        // Update time progression
        void UpdateTime(TimeOfDayComponent& timeOfDay, float deltaTime);

        // Calculate sun/moon positions
        void CalculateCelestialPositions(const TimeOfDayComponent& timeOfDay);

        // Update linked directional light
        void UpdateDirectionalLight(Scene& scene, const TimeOfDayComponent& timeOfDay);

        // Calculate sky colors for current time
        void CalculateSkyColors(const TimeOfDayComponent& timeOfDay);

        // Get system time for real-time mode
        float GetSystemTimeOfDay();

        // Configuration
        SkyboxSystemConfig m_Config;

        // Atmospheric skybox renderer
        std::unique_ptr<Renderer::AtmosphericSkybox> m_AtmosphericSkybox;

        // RHI device reference
        std::shared_ptr<RHI::RHIDevice> m_Device;

        // Current time state
        float m_CurrentTime = 12.0f;
        
        // Calculated celestial positions
        Math::Vec3 m_SunDirection{0.0f, 1.0f, 0.0f};
        Math::Vec3 m_MoonDirection{0.0f, -1.0f, 0.0f};
        
        // Calculated lighting parameters
        Math::Vec3 m_SunColor{1.0f, 1.0f, 1.0f};
        float m_SunIntensity = 1.0f;
        float m_AmbientIntensity = 0.3f;
        
        // Sky colors
        Math::Vec3 m_ZenithColor{0.15f, 0.35f, 0.65f};
        Math::Vec3 m_HorizonColor{0.4f, 0.55f, 0.7f};

        // State
        bool m_IsInitialized = false;
    };

} // namespace ECS
} // namespace Core
