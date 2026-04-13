#pragma once

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/LightComponent.h"
#include "Core/Renderer/ForwardPlus.h"
#include "Core/Profile.h"
#include "Core/Log.h"
#include <vector>

namespace Core {
namespace ECS {

    // GPU-ready light data structures
    struct DirectionalLightData {
        Math::Vec3 Direction;
        float _pad0;
        Math::Vec3 Color;
        float Intensity;
    };

    struct PointLightData {
        Math::Vec3 Position;
        float Radius;
        Math::Vec3 Color;
        float Intensity;
    };

    struct SpotLightData {
        Math::Vec3 Position;
        float Radius;
        Math::Vec3 Direction;
        float InnerCutoff;
        Math::Vec3 Color;
        float OuterCutoff;
        float Intensity;
        float _pad0;
        float _pad1;
        float _pad2;
    };

    enum class LightQualityTier : uint8_t {
        Low = 0,
        Medium = 1,
        High = 2
    };

    struct LightRoutingHint {
        uint64_t CacheKey = 0;
        LightQualityTier ShadowTier = LightQualityTier::Medium;
        bool CastShadows = false;
        bool PreferRTShadow = false;
    };

    class LightSystem {
    public:
        LightSystem() = default;
        ~LightSystem() = default;

        // Update light buffers from scene entities
        void Update(Scene& scene);

        // Access collected light data
        const std::vector<DirectionalLightData>& GetDirectionalLights() const { return m_DirectionalLights; }
        const std::vector<PointLightData>& GetPointLights() const { return m_PointLights; }
        const std::vector<SpotLightData>& GetSpotLights() const { return m_SpotLights; }

        // Get Forward+ compatible point light data
        const std::vector<Renderer::PointLight>& GetForwardPlusLights() const { return m_ForwardPlusLights; }
        const std::vector<LightRoutingHint>& GetLightRoutingHints() const { return m_LightRoutingHints; }

        // Statistics
        uint32_t GetDirectionalLightCount() const { return static_cast<uint32_t>(m_DirectionalLights.size()); }
        uint32_t GetPointLightCount() const { return static_cast<uint32_t>(m_PointLights.size()); }
        uint32_t GetSpotLightCount() const { return static_cast<uint32_t>(m_SpotLights.size()); }
        uint32_t GetTotalLightCount() const { return GetDirectionalLightCount() + GetPointLightCount() + GetSpotLightCount(); }

    private:
        std::vector<DirectionalLightData> m_DirectionalLights;
        std::vector<PointLightData> m_PointLights;
        std::vector<SpotLightData> m_SpotLights;
        std::vector<Renderer::PointLight> m_ForwardPlusLights;
        std::vector<LightRoutingHint> m_LightRoutingHints;
    };

} // namespace ECS
} // namespace Core
