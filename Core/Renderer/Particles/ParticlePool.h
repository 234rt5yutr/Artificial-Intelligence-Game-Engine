#pragma once

/**
 * @file ParticlePool.h
 * @brief GPU buffer pool management for particle data
 * 
 * Manages GPU memory allocation and recycling for particle systems.
 * Uses double-buffering to avoid GPU/CPU synchronization issues.
 */

#include "Core/RHI/RHIBuffer.h"
#include "Core/RHI/RHIDevice.h"
#include "Core/Math/Math.h"
#include <memory>
#include <vector>
#include <cstdint>
#include <string>

namespace Core {
namespace Renderer {
namespace Particles {

    //=========================================================================
    // Constants
    //=========================================================================

    constexpr uint32_t DEFAULT_POOL_SIZE = 1048576;        // 1M particles default
    constexpr uint32_t MAX_POOL_SIZE = 16777216;           // 16M particles max
    constexpr uint32_t MIN_POOL_SIZE = 1024;               // 1K particles min
    constexpr uint32_t PARTICLE_WORKGROUP_SIZE = 256;      // Compute workgroup size
    constexpr uint32_t SORT_WORKGROUP_SIZE = 256;          // Sort workgroup size

    //=========================================================================
    // GPU-aligned Particle Structure (matches shader)
    //=========================================================================

    /**
     * @brief GPU particle data structure
     * 
     * Layout must match exactly with GLSL struct in particle shaders.
     * Total size: 64 bytes (16-byte aligned)
     */
    struct alignas(16) Particle {
        Math::Vec3 Position;    // 12 bytes - World position
        float Lifetime;         // 4 bytes  - Total lifetime in seconds

        Math::Vec3 Velocity;    // 12 bytes - Velocity in units/second
        float Age;              // 4 bytes  - Current age in seconds

        Math::Vec4 Color;       // 16 bytes - RGBA color

        Math::Vec2 Size;        // 8 bytes  - Width, Height
        float Rotation;         // 4 bytes  - Rotation angle in radians
        uint32_t Flags;         // 4 bytes  - Active, emitter ID, etc.

        Particle()
            : Position(0.0f)
            , Lifetime(1.0f)
            , Velocity(0.0f)
            , Age(0.0f)
            , Color(1.0f)
            , Size(1.0f, 1.0f)
            , Rotation(0.0f)
            , Flags(0)
        {}
    };
    static_assert(sizeof(Particle) == 64, "Particle must be 64 bytes for GPU alignment");

    //=========================================================================
    // Particle Flags
    //=========================================================================

    enum class ParticleFlags : uint32_t {
        None        = 0,
        Active      = 1 << 0,     // Particle is alive
        NeedsSort   = 1 << 1,     // Needs depth sorting
        Billboard   = 1 << 2,     // Camera-facing billboard
        Stretched   = 1 << 3,     // Velocity-stretched
        Animated    = 1 << 4,     // Texture animation
        Lit         = 1 << 5,     // Affected by lighting
        Collision   = 1 << 6,     // Has collision
        SubEmitter  = 1 << 7      // Can spawn sub-particles
    };

    inline ParticleFlags operator|(ParticleFlags a, ParticleFlags b) {
        return static_cast<ParticleFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline ParticleFlags operator&(ParticleFlags a, ParticleFlags b) {
        return static_cast<ParticleFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    inline bool HasFlag(uint32_t flags, ParticleFlags flag) {
        return (flags & static_cast<uint32_t>(flag)) != 0;
    }

    //=========================================================================
    // Indirect Draw Command (matches VkDrawIndirectCommand)
    //=========================================================================

    struct DrawIndirectCommand {
        uint32_t VertexCount;
        uint32_t InstanceCount;
        uint32_t FirstVertex;
        uint32_t FirstInstance;
    };
    static_assert(sizeof(DrawIndirectCommand) == 16, "DrawIndirectCommand must be 16 bytes");

    //=========================================================================
    // Pool Configuration
    //=========================================================================

    struct ParticlePoolConfig {
        uint32_t MaxParticles = DEFAULT_POOL_SIZE;
        bool EnableSorting = true;
        bool EnableDoubleBuffering = true;
        std::string DebugName = "ParticlePool";
    };

    //=========================================================================
    // Pool Statistics
    //=========================================================================

    struct ParticlePoolStats {
        uint32_t AllocatedParticles = 0;
        uint32_t ActiveParticles = 0;
        uint32_t FreeSlots = 0;
        uint64_t TotalMemoryBytes = 0;
        double LastUpdateTimeMs = 0.0;

        void Reset() {
            AllocatedParticles = 0;
            ActiveParticles = 0;
            FreeSlots = 0;
            TotalMemoryBytes = 0;
            LastUpdateTimeMs = 0.0;
        }
    };

    //=========================================================================
    // Particle Pool Class
    //=========================================================================

    /**
     * @brief GPU buffer pool for particle data management
     * 
     * Manages:
     * - Particle data SSBO (double-buffered for ping-pong simulation)
     * - Alive/dead list buffers for GPU-side allocation
     * - Indirect draw command buffer
     * - Counter buffers for atomic operations
     */
    class ParticlePool {
    public:
        ParticlePool();
        ~ParticlePool();

        // Non-copyable, movable
        ParticlePool(const ParticlePool&) = delete;
        ParticlePool& operator=(const ParticlePool&) = delete;
        ParticlePool(ParticlePool&&) noexcept = default;
        ParticlePool& operator=(ParticlePool&&) noexcept = default;

        //---------------------------------------------------------------------
        // Initialization
        //---------------------------------------------------------------------

        /**
         * @brief Initialize the particle pool with given configuration
         * @param device The RHI device for resource creation
         * @param config Pool configuration options
         * @return true if initialization succeeded
         */
        bool Initialize(std::shared_ptr<RHI::RHIDevice> device, const ParticlePoolConfig& config);

        /**
         * @brief Shutdown and release all resources
         */
        void Shutdown();

        /**
         * @brief Check if pool is initialized
         */
        bool IsInitialized() const { return m_Initialized; }

        //---------------------------------------------------------------------
        // Configuration
        //---------------------------------------------------------------------

        /**
         * @brief Get current pool configuration
         */
        const ParticlePoolConfig& GetConfig() const { return m_Config; }

        /**
         * @brief Resize the pool (requires GPU idle)
         * @param newMaxParticles New maximum particle count
         * @return true if resize succeeded
         */
        bool Resize(uint32_t newMaxParticles);

        //---------------------------------------------------------------------
        // Buffer Access
        //---------------------------------------------------------------------

        /**
         * @brief Get the particle data buffer (current frame)
         */
        std::shared_ptr<RHI::RHIBuffer> GetParticleBuffer() const;

        /**
         * @brief Get the particle data buffer (previous frame, for read)
         */
        std::shared_ptr<RHI::RHIBuffer> GetParticleBufferPrev() const;

        /**
         * @brief Get the alive list buffer (indices of active particles)
         */
        std::shared_ptr<RHI::RHIBuffer> GetAliveListBuffer() const { return m_AliveListBuffer[m_CurrentBuffer]; }

        /**
         * @brief Get the previous alive list buffer
         */
        std::shared_ptr<RHI::RHIBuffer> GetAliveListBufferPrev() const { return m_AliveListBuffer[1 - m_CurrentBuffer]; }

        /**
         * @brief Get the dead list buffer (indices of free slots)
         */
        std::shared_ptr<RHI::RHIBuffer> GetDeadListBuffer() const { return m_DeadListBuffer; }

        /**
         * @brief Get the counter buffer (alive count, dead count, emit count)
         */
        std::shared_ptr<RHI::RHIBuffer> GetCounterBuffer() const { return m_CounterBuffer; }

        /**
         * @brief Get the indirect draw command buffer
         */
        std::shared_ptr<RHI::RHIBuffer> GetDrawIndirectBuffer() const { return m_DrawIndirectBuffer; }

        /**
         * @brief Get the sort dispatch indirect buffer
         */
        std::shared_ptr<RHI::RHIBuffer> GetSortDispatchBuffer() const { return m_SortDispatchBuffer; }

        /**
         * @brief Get sorted particle indices buffer (for depth sorting)
         */
        std::shared_ptr<RHI::RHIBuffer> GetSortedIndexBuffer() const { return m_SortedIndexBuffer; }

        /**
         * @brief Get sort key buffer (depth values for sorting)
         */
        std::shared_ptr<RHI::RHIBuffer> GetSortKeyBuffer() const { return m_SortKeyBuffer; }

        //---------------------------------------------------------------------
        // Frame Management
        //---------------------------------------------------------------------

        /**
         * @brief Swap buffers for next frame (ping-pong)
         */
        void SwapBuffers();

        /**
         * @brief Get current buffer index (0 or 1)
         */
        uint32_t GetCurrentBufferIndex() const { return m_CurrentBuffer; }

        /**
         * @brief Reset counters for new frame
         * @param commandList Command list for GPU operations
         */
        void ResetCounters(std::shared_ptr<RHI::RHICommandList> commandList);

        /**
         * @brief Initialize dead list with all particle indices
         */
        void InitializeDeadList();

        //---------------------------------------------------------------------
        // Statistics
        //---------------------------------------------------------------------

        /**
         * @brief Get pool statistics
         */
        const ParticlePoolStats& GetStats() const { return m_Stats; }

        /**
         * @brief Update statistics from GPU counters (requires readback)
         * @return Active particle count
         */
        uint32_t UpdateStats();

        /**
         * @brief Get maximum particle count
         */
        uint32_t GetMaxParticles() const { return m_Config.MaxParticles; }

    private:
        void CreateBuffers();
        void DestroyBuffers();

    private:
        std::shared_ptr<RHI::RHIDevice> m_Device;
        ParticlePoolConfig m_Config;
        ParticlePoolStats m_Stats;
        bool m_Initialized = false;

        // Particle data double-buffer (ping-pong)
        std::shared_ptr<RHI::RHIBuffer> m_ParticleBuffer[2];

        // Alive list double-buffer
        std::shared_ptr<RHI::RHIBuffer> m_AliveListBuffer[2];

        // Dead list (single buffer, persistent)
        std::shared_ptr<RHI::RHIBuffer> m_DeadListBuffer;

        // Counter buffer (4 uints: aliveCount, deadCount, emitCount, sortCount)
        std::shared_ptr<RHI::RHIBuffer> m_CounterBuffer;

        // Indirect draw command buffer
        std::shared_ptr<RHI::RHIBuffer> m_DrawIndirectBuffer;

        // Sort dispatch indirect buffer
        std::shared_ptr<RHI::RHIBuffer> m_SortDispatchBuffer;

        // Sorted indices and keys for depth sorting
        std::shared_ptr<RHI::RHIBuffer> m_SortedIndexBuffer;
        std::shared_ptr<RHI::RHIBuffer> m_SortKeyBuffer;

        // Staging buffer for readback
        std::shared_ptr<RHI::RHIBuffer> m_StagingBuffer;

        // Current buffer index for ping-pong
        uint32_t m_CurrentBuffer = 0;
    };

} // namespace Particles
} // namespace Renderer
} // namespace Core
