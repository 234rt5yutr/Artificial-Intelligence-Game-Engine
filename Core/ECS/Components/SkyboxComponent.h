#pragma once

#include "Core/Math/Math.h"

namespace Core {
namespace ECS {

    enum class SkyboxType : uint8_t {
        Procedural = 0,  // Volumetric atmospheric scattering
        Cubemap = 1      // Traditional cubemap skybox
    };

    struct SkyboxComponent {
        SkyboxType Type = SkyboxType::Procedural;

        // Sun properties
        Math::Vec3 SunDirection{ 0.0f, 1.0f, 0.0f };
        Math::Vec3 SunColor{ 1.0f, 0.98f, 0.95f };
        float SunIntensity = 22.0f;

        // Rayleigh scattering (wavelength-dependent for R, G, B)
        // Standard coefficients for Earth's atmosphere at sea level
        Math::Vec3 RayleighCoefficient{ 5.5e-6f, 13.0e-6f, 22.4e-6f };

        // Mie scattering (haze/aerosols)
        float MieCoefficient = 21e-6f;
        float MieDirectionalG = 0.76f;  // Range: 0.76-0.999, controls sun glow shape

        // Atmosphere geometry
        float AtmosphereHeight = 100000.0f;  // 100km
        float PlanetRadius = 6371000.0f;      // Earth radius in meters

        // Visual quality parameters
        float Turbidity = 2.0f;             // Haziness factor (1.0 = clear, 10.0 = very hazy)
        Math::Vec3 GroundAlbedo{ 0.3f, 0.3f, 0.3f };

        // Tone mapping
        float ExposureMultiplier = 1.0f;

        // Sun disc rendering
        float SunAngularDiameter = 0.0093f;  // Radians (~0.533 degrees)
        bool RenderSunDisc = true;

        // Cubemap fallback (used when Type == Cubemap)
        uint32_t CubemapTextureID = 0;

        SkyboxComponent() = default;

        // Factory method for procedural atmospheric skybox
        static SkyboxComponent CreateProcedural(float turbidity = 2.0f, float sunIntensity = 22.0f)
        {
            SkyboxComponent skybox;
            skybox.Type = SkyboxType::Procedural;
            skybox.Turbidity = turbidity;
            skybox.SunIntensity = sunIntensity;
            return skybox;
        }

        // Factory method for cubemap skybox
        static SkyboxComponent CreateCubemap(uint32_t textureID)
        {
            SkyboxComponent skybox;
            skybox.Type = SkyboxType::Cubemap;
            skybox.CubemapTextureID = textureID;
            return skybox;
        }

        // Factory method with custom atmospheric parameters
        static SkyboxComponent CreateCustomAtmosphere(
            const Math::Vec3& rayleighCoeff,
            float mieCoeff,
            float mieG,
            float atmosphereHeight,
            float planetRadius)
        {
            SkyboxComponent skybox;
            skybox.Type = SkyboxType::Procedural;
            skybox.RayleighCoefficient = rayleighCoeff;
            skybox.MieCoefficient = mieCoeff;
            skybox.MieDirectionalG = mieG;
            skybox.AtmosphereHeight = atmosphereHeight;
            skybox.PlanetRadius = planetRadius;
            return skybox;
        }
    };

} // namespace ECS
} // namespace Core
