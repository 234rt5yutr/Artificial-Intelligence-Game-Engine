#pragma once

/**
 * @file ParticleSystem.h
 * @brief GPU-driven particle system manager using compute shaders
 * 
 * Implements a fully GPU-based particle simulation system capable of
 * handling millions of particles efficiently. Uses compute shaders for:
 * - Particle emission (spawning new particles)
 * - Particle update (physics simulation, aging, death)
 * - Particle sorting (depth-based for correct alpha blending)
 * - Indirect draw generation
 */

#include "Core/Renderer/Particles/ParticlePool.h"
#include "Core/RHI/RHIBuffer.h"
#include "Core/RHI/RHIDevice.h"
#include "Core/RHI/RHICommandList.h"
#include "Core/Math/Math.h"
#include <memory>
#include <vector>
#include <array>
#include <cstdint>
#include <string>
#include <functional>

namespace Core {
namespace Renderer {
namespace Particles {

    //=========================================================================
    // Forward Declarations
    //=========================================================================

    class ParticlePool;

    //=========================================================================
    // Emitter Shape Types
    //=========================================================================

    enum class EmitterShape : uint32_t {
        Point = 0,          // Single point emission
        Sphere = 1,         // Spherical volume
        Hemisphere = 2,     // Half-sphere
        Cone = 3,           // Cone shaped
        Box = 4,            // Box volume
        Circle = 5,         // Circle (2D ring)
        Edge = 6            // Line segment
    };

    //=========================================================================
    // Simulation Space
    //=========================================================================

    enum class SimulationSpace : uint32_t {
        World = 0,          // Particles move in world space
        Local = 1           // Particles move relative to emitter
    };

    //=========================================================================
    // GPU-aligned Emitter Parameters (matches shader)
    //=========================================================================

    /**
     * @brief Emitter configuration for GPU emission
     * 
     * This structure is uploaded to GPU and used by the emit compute shader.
     * Must remain 16-byte aligned and match GLSL layout.
     */
    struct alignas(16) EmitterParams {
        // Spawn position and timing
        Math::Vec3 Position;            // Emitter world position
        float SpawnRate;                // Particles per second

        // Direction and spread
        Math::Vec3 Direction;           // Primary emission direction (normalized)
        float DirectionSpread;          // Cone angle spread (radians)

        // Velocity
        Math::Vec2 VelocityMinMax;      // Min/max initial speed
        Math::Vec2 LifetimeMinMax;      // Min/max lifetime in seconds

        // Size
        Math::Vec2 SizeMin;             // Minimum size (width, height)
        Math::Vec2 SizeMax;             // Maximum size (width, height)

        // Color
        Math::Vec4 ColorStart;          // Initial color (RGBA)
        Math::Vec4 ColorEnd;            // Final color (RGBA)

        // Shape parameters
        Math::Vec3 ShapeScale;          // Scale for emission shape
        float ShapeRadius;              // Radius for spherical shapes

        // Rotation
        float RotationMin;              // Minimum initial rotation (radians)
        float RotationMax;              // Maximum initial rotation (radians)
        float RotationSpeedMin;         // Min rotation speed (rad/s)
        float RotationSpeedMax;         // Max rotation speed (rad/s)

        // Emitter properties
        uint32_t Shape;                 // EmitterShape enum value
        uint32_t EmitterId;             // Unique emitter identifier
        uint32_t Flags;                 // Emitter flags (billboarding, etc.)
        uint32_t MaxParticles;          // Maximum particles for this emitter

        // Simulation
        Math::Vec3 Gravity;             // Gravity acceleration
        float Drag;                     // Air resistance (0-1)

        // Frame data
        float DeltaTime;                // Time since last update
        float Time;                     // Total elapsed time
        float EmitAccumulator;          // Fractional emit accumulator
        uint32_t Padding0;

        // Padding to reach 192 bytes (multiple of 16)
        Math::Vec4 Reserved;            // Reserved for future use

        EmitterParams()
            : Position(0.0f)
            , SpawnRate(100.0f)
            , Direction(0.0f, 1.0f, 0.0f)
            , DirectionSpread(0.5f)
            , VelocityMinMax(1.0f, 5.0f)
            , LifetimeMinMax(1.0f, 3.0f)
            , SizeMin(0.1f, 0.1f)
            , SizeMax(0.5f, 0.5f)
            , ColorStart(1.0f)
            , ColorEnd(1.0f, 1.0f, 1.0f, 0.0f)
            , ShapeScale(1.0f)
            , ShapeRadius(1.0f)
            , RotationMin(0.0f)
            , RotationMax(6.28318f)
            , RotationSpeedMin(0.0f)
            , RotationSpeedMax(1.0f)
            , Shape(0)
            , EmitterId(0)
            , Flags(0)
            , MaxParticles(10000)
            , Gravity(0.0f, -9.81f, 0.0f)
            , Drag(0.0f)
            , DeltaTime(0.016f)
            , Time(0.0f)
            , EmitAccumulator(0.0f)
            , Padding0(0)
            , Reserved(0.0f)
        {}
    };
    static_assert(sizeof(EmitterParams) == 192, "EmitterParams must be 192 bytes for GPU alignment");

    //=========================================================================
    // GPU-aligned Update Parameters (matches shader)
    //=========================================================================

    /**
     * @brief Update parameters for GPU particle simulation
     */
    struct alignas(16) UpdateParams {
        Math::Mat4 ViewProjection;      // View-projection matrix for depth

        Math::Vec3 CameraPosition;      // Camera world position
        float DeltaTime;                // Frame delta time

        Math::Vec3 Gravity;             // Global gravity
        float Drag;                     // Global drag coefficient

        Math::Vec3 WindDirection;       // Wind direction
        float WindStrength;             // Wind force

        uint32_t MaxParticles;          // Maximum particle count
        uint32_t EnableCollision;       // Collision enabled flag
        uint32_t EnableSorting;         // Depth sorting enabled
        uint32_t Padding0;

        Math::Vec4 CollisionPlane;      // Simple ground plane (nx, ny, nz, d)

        UpdateParams()
            : ViewProjection(1.0f)
            , CameraPosition(0.0f)
            , DeltaTime(0.016f)
            , Gravity(0.0f, -9.81f, 0.0f)
            , Drag(0.0f)
            , WindDirection(1.0f, 0.0f, 0.0f)
            , WindStrength(0.0f)
            , MaxParticles(0)
            , EnableCollision(0)
            , EnableSorting(1)
            , Padding0(0)
            , CollisionPlane(0.0f, 1.0f, 0.0f, 0.0f)
        {}
    };
    static_assert(sizeof(UpdateParams) == 144, "UpdateParams must be 144 bytes for GPU alignment");

    //=========================================================================
    // Particle System Configuration
    //=========================================================================

    struct ParticleSystemConfig {
        uint32_t MaxParticles = DEFAULT_POOL_SIZE;
        uint32_t MaxEmitters = 64;
        bool EnableSorting = true;
        bool EnableCollision = false;
        bool EnableWind = false;
        Math::Vec3 GlobalGravity = Math::Vec3(0.0f, -9.81f, 0.0f);
        std::string DebugName = "ParticleSystem";
    };

    //=========================================================================
    // Particle System Statistics
    //=========================================================================

    struct ParticleSystemStats {
        uint32_t ActiveParticles = 0;
        uint32_t ParticlesSpawned = 0;
        uint32_t ParticlesDied = 0;
        uint32_t ActiveEmitters = 0;
        double EmitTimeMs = 0.0;
        double UpdateTimeMs = 0.0;
        double SortTimeMs = 0.0;
        double TotalTimeMs = 0.0;

        void Reset() {
            ActiveParticles = 0;
            ParticlesSpawned = 0;
            ParticlesDied = 0;
            ActiveEmitters = 0;
            EmitTimeMs = 0.0;
            UpdateTimeMs = 0.0;
            SortTimeMs = 0.0;
            TotalTimeMs = 0.0;
        }
    };

    //=========================================================================
    // Emitter Handle
    //=========================================================================

    struct EmitterHandle {
        uint32_t Id = UINT32_MAX;
        bool IsValid() const { return Id != UINT32_MAX; }
        bool operator==(const EmitterHandle& other) const { return Id == other.Id; }
        bool operator!=(const EmitterHandle& other) const { return Id != other.Id; }
    };

    //=========================================================================
    // Particle System Class
    //=========================================================================

    /**
     * @brief GPU-driven particle system manager
     * 
     * This class manages the complete particle simulation pipeline:
     * 1. Emission: Spawns new particles based on emitter configurations
     * 2. Update: Simulates physics, aging, and death
     * 3. Sort: Orders particles by depth for correct blending
     * 4. Draw: Generates indirect draw commands for rendering
     * 
     * All simulation runs on the GPU via compute shaders for maximum performance.
     */
    class ParticleSystem {
    public:
        ParticleSystem();
        ~ParticleSystem();

        // Non-copyable
        ParticleSystem(const ParticleSystem&) = delete;
        ParticleSystem& operator=(const ParticleSystem&) = delete;

        //---------------------------------------------------------------------
        // Initialization
        //---------------------------------------------------------------------

        /**
         * @brief Initialize the particle system
         * @param device The RHI device for resource creation
         * @param config System configuration
         */
        void Initialize(std::shared_ptr<RHI::RHIDevice> device, const ParticleSystemConfig& config = {});

        /**
         * @brief Shutdown and release all resources
         */
        void Shutdown();

        /**
         * @brief Check if system is initialized
         */
        bool IsInitialized() const { return m_Initialized; }

        //---------------------------------------------------------------------
        // Configuration
        //---------------------------------------------------------------------

        /**
         * @brief Get current configuration
         */
        const ParticleSystemConfig& GetConfig() const { return m_Config; }

        /**
         * @brief Set global gravity
         */
        void SetGravity(const Math::Vec3& gravity);

        /**
         * @brief Set wind parameters
         */
        void SetWind(const Math::Vec3& direction, float strength);

        /**
         * @brief Enable/disable particle sorting
         */
        void SetSortingEnabled(bool enabled);

        /**
         * @brief Enable/disable collision
         */
        void SetCollisionEnabled(bool enabled);

        /**
         * @brief Set collision ground plane
         */
        void SetCollisionPlane(const Math::Vec4& plane);

        //---------------------------------------------------------------------
        // Emitter Management
        //---------------------------------------------------------------------

        /**
         * @brief Create a new particle emitter
         * @param params Emitter configuration
         * @return Handle to the created emitter
         */
        EmitterHandle CreateEmitter(const EmitterParams& params);

        /**
         * @brief Update an existing emitter
         * @param handle Handle to the emitter
         * @param params New emitter parameters
         */
        void UpdateEmitter(EmitterHandle handle, const EmitterParams& params);

        /**
         * @brief Destroy an emitter
         * @param handle Handle to the emitter
         */
        void DestroyEmitter(EmitterHandle handle);

        /**
         * @brief Set emitter position
         */
        void SetEmitterPosition(EmitterHandle handle, const Math::Vec3& position);

        /**
         * @brief Set emitter direction
         */
        void SetEmitterDirection(EmitterHandle handle, const Math::Vec3& direction);

        /**
         * @brief Set emitter spawn rate
         */
        void SetEmitterSpawnRate(EmitterHandle handle, float rate);

        /**
         * @brief Enable/disable an emitter
         */
        void SetEmitterEnabled(EmitterHandle handle, bool enabled);

        /**
         * @brief Get emitter parameters
         */
        const EmitterParams* GetEmitterParams(EmitterHandle handle) const;

        /**
         * @brief Get active emitter count
         */
        uint32_t GetActiveEmitterCount() const { return m_ActiveEmitterCount; }

        /**
         * @brief Burst emit particles from emitter
         * @param handle Emitter handle
         * @param count Number of particles to emit immediately
         */
        void Burst(EmitterHandle handle, uint32_t count);

        //---------------------------------------------------------------------
        // Camera Update
        //---------------------------------------------------------------------

        /**
         * @brief Update camera data for sorting and culling
         * @param viewProjection View-projection matrix
         * @param cameraPosition Camera world position
         */
        void UpdateCamera(const Math::Mat4& viewProjection, const Math::Vec3& cameraPosition);

        //---------------------------------------------------------------------
        // Simulation
        //---------------------------------------------------------------------

        /**
         * @brief Execute full particle simulation frame
         * 
         * This runs all compute shaders in sequence:
         * 1. Emit new particles
         * 2. Update existing particles (physics, aging)
         * 3. Compact alive list
         * 4. Sort particles by depth (if enabled)
         * 5. Generate indirect draw commands
         * 
         * @param commandList Command list for compute dispatches
         * @param deltaTime Time since last frame in seconds
         */
        void Update(std::shared_ptr<RHI::RHICommandList> commandList, float deltaTime);

        /**
         * @brief Emit particles (separate call for custom pipelines)
         */
        void Emit(std::shared_ptr<RHI::RHICommandList> commandList);

        /**
         * @brief Update particles (separate call for custom pipelines)
         */
        void Simulate(std::shared_ptr<RHI::RHICommandList> commandList, float deltaTime);

        /**
         * @brief Sort particles by depth (separate call for custom pipelines)
         */
        void Sort(std::shared_ptr<RHI::RHICommandList> commandList);

        //---------------------------------------------------------------------
        // Rendering Buffers
        //---------------------------------------------------------------------

        /**
         * @brief Get particle data buffer for rendering
         */
        std::shared_ptr<RHI::RHIBuffer> GetParticleBuffer() const;

        /**
         * @brief Get sorted particle indices buffer
         */
        std::shared_ptr<RHI::RHIBuffer> GetSortedIndexBuffer() const;

        /**
         * @brief Get alive particle list buffer
         */
        std::shared_ptr<RHI::RHIBuffer> GetAliveListBuffer() const;

        /**
         * @brief Get indirect draw command buffer for vkCmdDrawIndirect
         */
        std::shared_ptr<RHI::RHIBuffer> GetDrawIndirectBuffer() const;

        /**
         * @brief Get particle pool for advanced access
         */
        ParticlePool* GetPool() { return m_Pool.get(); }
        const ParticlePool* GetPool() const { return m_Pool.get(); }

        //---------------------------------------------------------------------
        // Statistics
        //---------------------------------------------------------------------

        /**
         * @brief Get system statistics
         */
        const ParticleSystemStats& GetStats() const { return m_Stats; }

        /**
         * @brief Update statistics from GPU (requires readback, may stall)
         */
        void UpdateStats();

        /**
         * @brief Get active particle count (cached from last UpdateStats)
         */
        uint32_t GetActiveParticleCount() const { return m_Stats.ActiveParticles; }

        /**
         * @brief Get maximum particle capacity
         */
        uint32_t GetMaxParticles() const { return m_Config.MaxParticles; }

        //---------------------------------------------------------------------
        // Clear
        //---------------------------------------------------------------------

        /**
         * @brief Clear all particles immediately
         */
        void ClearAllParticles();

        /**
         * @brief Clear particles for a specific emitter
         */
        void ClearEmitterParticles(EmitterHandle handle);

    private:
        void CreateBuffers();
        void UpdateEmitterBuffer();
        void UpdateParamsBuffer();
        uint32_t CalculateDispatchGroups(uint32_t count, uint32_t workgroupSize) const;

        // Emitter data structure
        struct EmitterData {
            EmitterParams Params;
            bool Active = false;
            bool Enabled = true;
            float EmitAccumulator = 0.0f;
        };

    private:
        std::shared_ptr<RHI::RHIDevice> m_Device;
        ParticleSystemConfig m_Config;
        ParticleSystemStats m_Stats;
        bool m_Initialized = false;

        // Particle pool (owns GPU particle buffers)
        std::unique_ptr<ParticlePool> m_Pool;

        // Emitter data
        std::vector<EmitterData> m_Emitters;
        uint32_t m_ActiveEmitterCount = 0;
        uint32_t m_NextEmitterId = 0;
        bool m_EmitterBufferDirty = true;

        // GPU Uniform buffers
        std::shared_ptr<RHI::RHIBuffer> m_EmitterParamsBuffer;
        std::shared_ptr<RHI::RHIBuffer> m_UpdateParamsBuffer;

        // Update parameters (CPU-side)
        UpdateParams m_UpdateParams;

        // Timing
        float m_TotalTime = 0.0f;
    };

} // namespace Particles
} // namespace Renderer
} // namespace Core
