#pragma once

// AtmosphericSkybox - GPU-based atmospheric scattering renderer
// Implements Rayleigh/Mie scattering for physically-based sky rendering

#include "Core/ECS/Components/SkyboxComponent.h"
#include "Core/ECS/Components/TimeOfDayComponent.h"
#include "Core/RHI/RHIDevice.h"
#include "Core/RHI/RHIBuffer.h"
#include "Core/RHI/RHICommandList.h"
#include "Core/Math/Math.h"

#include <memory>

namespace Core {
namespace Renderer {

    // GPU-aligned uniform buffer for atmospheric parameters
    struct alignas(16) AtmosphereUniforms {
        Math::Vec3 SunDirection;
        float SunIntensity;
        
        Math::Vec3 RayleighCoefficient;
        float MieCoefficient;
        
        Math::Vec3 SunColor;
        float MieDirectionalG;
        
        Math::Vec3 CameraPosition;
        float AtmosphereHeight;
        
        Math::Vec3 GroundAlbedo;
        float PlanetRadius;
        
        float Turbidity;
        float ExposureMultiplier;
        float SunAngularDiameter;
        float TimeOfDay;
        
        float StarIntensity;
        float MoonPhase;
        float Padding0;
        float Padding1;

        AtmosphereUniforms()
            : SunDirection(0.0f, 1.0f, 0.0f)
            , SunIntensity(22.0f)
            , RayleighCoefficient(5.5e-6f, 13.0e-6f, 22.4e-6f)
            , MieCoefficient(21e-6f)
            , SunColor(1.0f, 0.98f, 0.95f)
            , MieDirectionalG(0.76f)
            , CameraPosition(0.0f)
            , AtmosphereHeight(100000.0f)
            , GroundAlbedo(0.3f)
            , PlanetRadius(6371000.0f)
            , Turbidity(2.0f)
            , ExposureMultiplier(1.0f)
            , SunAngularDiameter(0.0093f)
            , TimeOfDay(12.0f)
            , StarIntensity(1.0f)
            , MoonPhase(0.0f)
            , Padding0(0.0f)
            , Padding1(0.0f)
        {}
    };
    static_assert(sizeof(AtmosphereUniforms) == 128, "AtmosphereUniforms must be 128 bytes");

    class AtmosphericSkybox {
    public:
        AtmosphericSkybox();
        ~AtmosphericSkybox();

        // Non-copyable
        AtmosphericSkybox(const AtmosphericSkybox&) = delete;
        AtmosphericSkybox& operator=(const AtmosphericSkybox&) = delete;

        // Initialize with RHI device
        void Initialize(std::shared_ptr<RHI::RHIDevice> device);

        // Shutdown and cleanup
        void Shutdown();

        // Update uniforms from skybox component
        void UpdateFromComponent(const ECS::SkyboxComponent& skybox,
                                 const ECS::TimeOfDayComponent* timeOfDay = nullptr);

        // Update camera position
        void SetCameraPosition(const Math::Vec3& position);

        // Render the skybox
        void Render(std::shared_ptr<RHI::RHICommandList> commandList,
                    const Math::Mat4& invViewProjection);

        // Get uniform buffer for shader binding
        std::shared_ptr<RHI::RHIBuffer> GetUniformBuffer() const { return m_UniformBuffer; }

        // Calculate sun direction from time of day
        static Math::Vec3 CalculateSunDirection(float timeOfDay, float latitude = 45.0f);

        // Calculate sun color based on elevation
        static Math::Vec3 CalculateSunColor(float sunElevation);

        // Accessors
        const AtmosphereUniforms& GetUniforms() const { return m_Uniforms; }
        bool IsInitialized() const { return m_IsInitialized; }

    private:
        void CreateUniformBuffer();
        void UpdateUniformBuffer();

        std::shared_ptr<RHI::RHIDevice> m_Device;
        std::shared_ptr<RHI::RHIBuffer> m_UniformBuffer;
        
        AtmosphereUniforms m_Uniforms;
        
        bool m_IsInitialized = false;
        bool m_NeedsUpdate = true;
    };

} // namespace Renderer
} // namespace Core
