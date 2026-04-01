#include "Core/Renderer/Skybox/AtmosphericSkybox.h"
#include "Core/Log.h"
#include "Core/Profile.h"

#include <cmath>

namespace Core {
namespace Renderer {

    AtmosphericSkybox::AtmosphericSkybox()
    {
    }

    AtmosphericSkybox::~AtmosphericSkybox()
    {
        Shutdown();
    }

    void AtmosphericSkybox::Initialize(std::shared_ptr<RHI::RHIDevice> device)
    {
        PROFILE_FUNCTION();

        m_Device = device;

        if (m_Device) {
            CreateUniformBuffer();
        }

        m_IsInitialized = true;
        ENGINE_CORE_INFO("AtmosphericSkybox initialized");
    }

    void AtmosphericSkybox::Shutdown()
    {
        PROFILE_FUNCTION();

        if (!m_IsInitialized) return;

        m_UniformBuffer.reset();
        m_IsInitialized = false;

        ENGINE_CORE_INFO("AtmosphericSkybox shutdown");
    }

    void AtmosphericSkybox::UpdateFromComponent(const ECS::SkyboxComponent& skybox,
                                                 const ECS::TimeOfDayComponent* timeOfDay)
    {
        PROFILE_FUNCTION();

        // Basic skybox parameters
        m_Uniforms.SunColor = skybox.SunColor;
        m_Uniforms.SunIntensity = skybox.SunIntensity;
        m_Uniforms.RayleighCoefficient = skybox.RayleighCoefficient;
        m_Uniforms.MieCoefficient = skybox.MieCoefficient;
        m_Uniforms.MieDirectionalG = skybox.MieDirectionalG;
        m_Uniforms.AtmosphereHeight = skybox.AtmosphereHeight;
        m_Uniforms.PlanetRadius = skybox.PlanetRadius;
        m_Uniforms.Turbidity = skybox.Turbidity;
        m_Uniforms.GroundAlbedo = skybox.GroundAlbedo;
        m_Uniforms.ExposureMultiplier = skybox.ExposureMultiplier;
        m_Uniforms.SunAngularDiameter = skybox.SunAngularDiameter;

        // Time of day integration
        if (timeOfDay) {
            m_Uniforms.TimeOfDay = timeOfDay->CurrentTime;
            m_Uniforms.StarIntensity = timeOfDay->StarIntensity;
            m_Uniforms.MoonPhase = timeOfDay->MoonPhase;

            // Calculate sun direction from time
            m_Uniforms.SunDirection = CalculateSunDirection(
                timeOfDay->CurrentTime, 
                timeOfDay->Latitude);

            // Adjust sun color based on elevation
            float sunElevation = m_Uniforms.SunDirection.y;
            m_Uniforms.SunColor = CalculateSunColor(sunElevation);
        } else {
            m_Uniforms.SunDirection = skybox.SunDirection;
        }

        m_NeedsUpdate = true;
        UpdateUniformBuffer();
    }

    void AtmosphericSkybox::SetCameraPosition(const Math::Vec3& position)
    {
        m_Uniforms.CameraPosition = position;
        m_NeedsUpdate = true;
    }

    void AtmosphericSkybox::Render(std::shared_ptr<RHI::RHICommandList> commandList,
                                    const Math::Mat4& invViewProjection)
    {
        PROFILE_FUNCTION();

        if (!m_IsInitialized || !commandList) return;

        // Update uniform buffer if needed
        if (m_NeedsUpdate) {
            UpdateUniformBuffer();
        }

        // The actual draw call would be:
        // 1. Bind pipeline state for atmospheric_sky shaders
        // 2. Bind uniform buffer
        // 3. Set push constants (invViewProjection)
        // 4. Draw fullscreen triangle (3 vertices, no vertex buffer)

        ENGINE_CORE_TRACE("AtmosphericSkybox: Rendered skybox");
    }

    Math::Vec3 AtmosphericSkybox::CalculateSunDirection(float timeOfDay, float latitude)
    {
        // Convert time to radians (0 = midnight, 12 = noon)
        float hourAngle = (timeOfDay - 12.0f) * (3.14159265f / 12.0f);
        
        // Simplified sun position (ignoring seasonal variation)
        // declination angle (0 for equinox)
        float declination = 0.0f;
        
        // Convert latitude to radians
        float latRad = glm::radians(latitude);
        
        // Calculate sun elevation
        float sinElevation = glm::sin(latRad) * glm::sin(declination) +
                            glm::cos(latRad) * glm::cos(declination) * glm::cos(hourAngle);
        
        float elevation = glm::asin(glm::clamp(sinElevation, -1.0f, 1.0f));
        
        // Calculate sun azimuth
        float cosAzimuth = (glm::sin(declination) - glm::sin(latRad) * sinElevation) /
                          (glm::cos(latRad) * glm::cos(elevation) + 0.0001f);
        cosAzimuth = glm::clamp(cosAzimuth, -1.0f, 1.0f);
        
        float azimuth = glm::acos(cosAzimuth);
        if (hourAngle > 0) {
            azimuth = 2.0f * 3.14159265f - azimuth;
        }
        
        // Convert to direction vector (Y-up coordinate system)
        Math::Vec3 direction;
        direction.x = glm::cos(elevation) * glm::sin(azimuth);
        direction.y = glm::sin(elevation);
        direction.z = glm::cos(elevation) * glm::cos(azimuth);
        
        return glm::normalize(direction);
    }

    Math::Vec3 AtmosphericSkybox::CalculateSunColor(float sunElevation)
    {
        // Sunset/sunrise color shift
        // Higher elevation = whiter light, lower = more orange/red
        
        Math::Vec3 noonColor(1.0f, 0.98f, 0.95f);
        Math::Vec3 sunsetColor(1.0f, 0.6f, 0.3f);
        Math::Vec3 nightColor(0.1f, 0.1f, 0.2f);
        
        if (sunElevation > 0.2f) {
            // Daytime - interpolate from sunset to noon
            float t = glm::smoothstep(0.2f, 0.5f, sunElevation);
            return glm::mix(sunsetColor, noonColor, t);
        } else if (sunElevation > -0.1f) {
            // Sunset/sunrise transition
            float t = (sunElevation + 0.1f) / 0.3f;
            return glm::mix(nightColor, sunsetColor, t);
        } else {
            // Night
            return nightColor;
        }
    }

    void AtmosphericSkybox::CreateUniformBuffer()
    {
        PROFILE_FUNCTION();

        if (!m_Device) return;

        RHI::BufferDesc desc;
        desc.Size = sizeof(AtmosphereUniforms);
        desc.Usage = RHI::BufferUsage::UniformBuffer;
        desc.MemoryType = RHI::MemoryType::HostVisible;

        m_UniformBuffer = m_Device->CreateBuffer(desc, &m_Uniforms);
    }

    void AtmosphericSkybox::UpdateUniformBuffer()
    {
        PROFILE_FUNCTION();

        if (!m_UniformBuffer || !m_Device) return;

        m_Device->UpdateBuffer(m_UniformBuffer, &m_Uniforms, sizeof(AtmosphereUniforms));
        m_NeedsUpdate = false;
    }

} // namespace Renderer
} // namespace Core
