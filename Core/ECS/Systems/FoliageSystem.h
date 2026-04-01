#pragma once

// FoliageSystem - Manages foliage scattering, wind animation, and rendering
// Uses compute shaders for GPU-driven instanced rendering of vegetation

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/FoliageComponent.h"
#include "Core/ECS/Components/CameraComponent.h"
#include "Core/Renderer/Foliage/FoliageScatter.h"
#include "Core/Profile.h"
#include "Core/Log.h"

#include <memory>
#include <unordered_map>

namespace Core {

// Forward declarations
namespace RHI {
    class RHIDevice;
    class RHICommandList;
}

namespace ECS {

    // Configuration for foliage system
    struct FoliageSystemConfig {
        float GlobalWindStrength = 1.0f;
        float GlobalDensityScale = 1.0f;
        Math::Vec3 WindDirection{1.0f, 0.0f, 0.5f};
        float WindSpeed = 1.0f;
        float WindGustiness = 0.3f;
        bool EnableShadows = true;
        bool EnableDistanceFade = true;
        float FadeStartDistance = 80.0f;
        float FadeEndDistance = 100.0f;
    };

    class FoliageSystem {
    public:
        FoliageSystem();
        ~FoliageSystem();

        // Non-copyable
        FoliageSystem(const FoliageSystem&) = delete;
        FoliageSystem& operator=(const FoliageSystem&) = delete;

        // Initialize with RHI device
        void Initialize(std::shared_ptr<RHI::RHIDevice> device);

        // Shutdown and cleanup
        void Shutdown();

        // Update foliage system (wind, LOD, etc.)
        void Update(Scene& scene, float deltaTime);

        // Dispatch compute shaders for foliage scattering
        void DispatchScatter(std::shared_ptr<RHI::RHICommandList> commandList);

        // Submit instanced draw calls for foliage
        void Render(std::shared_ptr<RHI::RHICommandList> commandList);

        // Configuration
        void SetConfig(const FoliageSystemConfig& config) { m_Config = config; }
        const FoliageSystemConfig& GetConfig() const { return m_Config; }

        // Wind control
        void SetWindDirection(const Math::Vec3& direction);
        void SetWindStrength(float strength);

        // Access foliage scatter system
        Renderer::FoliageScatter* GetFoliageScatter() { return m_FoliageScatter.get(); }
        const Renderer::FoliageScatter* GetFoliageScatter() const { return m_FoliageScatter.get(); }

        // Statistics
        uint32_t GetVisibleInstanceCount() const;
        uint32_t GetTotalInstanceCount() const;
        uint32_t GetFoliageTypeCount() const;

    private:
        // Register foliage components with scatter system
        void SyncFoliageComponents(Scene& scene);

        // Update camera for culling
        void UpdateCamera(Scene& scene);

        // Update wind animation
        void UpdateWind(float deltaTime);

        // Configuration
        FoliageSystemConfig m_Config;

        // Foliage scatter system
        std::unique_ptr<Renderer::FoliageScatter> m_FoliageScatter;

        // Entity to scatter index mapping
        std::unordered_map<uint32_t, uint32_t> m_EntityToScatterIndex;

        // RHI device reference
        std::shared_ptr<RHI::RHIDevice> m_Device;

        // Timing
        float m_TotalTime = 0.0f;
        float m_WindPhase = 0.0f;

        // State
        bool m_IsInitialized = false;
    };

} // namespace ECS
} // namespace Core
