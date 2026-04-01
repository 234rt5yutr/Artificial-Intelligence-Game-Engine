#pragma once

/**
 * @file ParticleRenderPass.h
 * @brief Specialized render pass for particle sorting and blending
 * 
 * Manages rendering of particles with proper blending, depth sorting,
 * and support for multiple blend modes. Integrates with the GPU-driven
 * particle system for efficient transparent particle rendering.
 */

#include "Core/RHI/RHIDevice.h"
#include "Core/RHI/RHIRenderPass.h"
#include "Core/RHI/RHICommandList.h"
#include "Core/RHI/RHITexture.h"
#include "Core/RHI/RHIPipelineState.h"
#include "Core/RHI/RHIBuffer.h"
#include "Core/Math/Math.h"
#include "Core/Renderer/Particles/ParticleSystem.h"
#include "Core/Renderer/Particles/TextureAtlas.h"
#include <memory>
#include <vector>
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace Core {
namespace Renderer {
namespace Particles {

    //=========================================================================
    // Particle Blend Mode
    //=========================================================================

    /**
     * @brief Blend modes for particle rendering
     * 
     * Different visual effects require different blend configurations:
     * - Additive: Fire, magic, glowing effects (bright and additive)
     * - AlphaBlend: Smoke, dust, clouds (standard transparency)
     * - Premultiplied: Pre-multiplied alpha textures (performance optimization)
     * - Multiply: Dark effects, shadows (darkening blend)
     * - SoftAdditive: Softer glow effects (less intense than pure additive)
     */
    enum class ParticleBlendMode : uint32_t {
        Additive = 0,       // srcAlpha + dst (fire, magic, glowing effects)
        AlphaBlend = 1,     // srcAlpha * src + (1-srcAlpha) * dst (smoke, dust, clouds)
        Premultiplied = 2,  // src + (1-srcAlpha) * dst (pre-multiplied alpha textures)
        Multiply = 3,       // src * dst (dark effects, shadows)
        SoftAdditive = 4,   // (1-srcColor) + dst (softer glow)
        Count = 5
    };

    /**
     * @brief Convert blend mode to string for debugging
     */
    inline const char* BlendModeToString(ParticleBlendMode mode) {
        switch (mode) {
            case ParticleBlendMode::Additive:      return "Additive";
            case ParticleBlendMode::AlphaBlend:    return "AlphaBlend";
            case ParticleBlendMode::Premultiplied: return "Premultiplied";
            case ParticleBlendMode::Multiply:      return "Multiply";
            case ParticleBlendMode::SoftAdditive:  return "SoftAdditive";
            default:                               return "Unknown";
        }
    }

    //=========================================================================
    // Render Pass Configuration
    //=========================================================================

    /**
     * @brief Configuration for the particle render pass
     */
    struct ParticleRenderPassConfig {
        uint32_t Width = 1920;
        uint32_t Height = 1080;
        bool EnableSoftParticles = true;          // Fade near geometry
        float SoftParticleScale = 0.5f;           // Soft particle fade distance
        bool EnableDepthSorting = true;           // Sort particles by depth
        bool UsePremultipliedAlpha = false;       // Output premultiplied alpha
        uint32_t MaxSortIterations = 16;          // Max bitonic sort iterations
        std::string DebugName = "ParticleRenderPass";
    };

    //=========================================================================
    // Particle Batch
    //=========================================================================

    /**
     * @brief Batch of particles sharing the same blend mode
     * 
     * Allows rendering multiple emitters with the same blend mode
     * in a single draw call for better performance.
     */
    struct ParticleBatch {
        ParticleBlendMode BlendMode = ParticleBlendMode::AlphaBlend;
        uint32_t ParticleOffset = 0;              // Offset into sorted buffer
        uint32_t ParticleCount = 0;               // Number of particles
        uint32_t EmitterId = 0;                   // Source emitter ID
        bool IsOpaque = false;                    // Opaque particles (no blending)
    };

    //=========================================================================
    // Sort Push Constants (GPU)
    //=========================================================================

    /**
     * @brief Push constants for particle sorting compute shader
     */
    struct alignas(16) SortPushConstants {
        uint32_t StageIndex = 0;           // Current bitonic sort stage
        uint32_t PassIndex = 0;            // Current pass within stage
        uint32_t SortDirection = 1;        // 0 = ascending, 1 = descending
        uint32_t MaxElements = 0;          // Number of elements to sort
    };
    static_assert(sizeof(SortPushConstants) == 16, "SortPushConstants must be 16 bytes");

    //=========================================================================
    // Render Push Constants (GPU) - Extended with Atlas Support
    //=========================================================================

    /**
     * @brief Push constants for particle rendering with texture atlas support
     * 
     * Extended to include all parameters needed for sprite sheet animation.
     * Must match the shader push constant layout exactly.
     * 
     * @note This struct is NOT 16-byte aligned because push constants have
     * different alignment requirements than uniform buffers. The shader
     * uses std430 push constant layout which allows tighter packing.
     */
    struct RenderPushConstants {
        //---------------------------------------------------------------------
        // Basic Rendering Parameters (original fields)
        //---------------------------------------------------------------------
        
        uint32_t UseSortedIndices = 1;     // Use sorted particle order
        uint32_t SoftParticles = 1;        // Enable soft particle edges
        float SoftParticleScale = 0.5f;    // Soft particle fade scale
        uint32_t UseTexture = 0;           // Use texture sampling
        uint32_t BlendMode = 0;            // Current blend mode (for shader variations)
        float DepthBias = 0.0f;            // Depth bias for layering
        
        //---------------------------------------------------------------------
        // Texture Atlas Parameters
        //---------------------------------------------------------------------
        
        /** Number of columns in the texture atlas (0 = no atlas) */
        uint32_t AtlasColumns = 0;
        
        /** Number of rows in the texture atlas */
        uint32_t AtlasRows = 0;
        
        /** Total number of animation frames in the atlas */
        uint32_t AtlasFrameCount = 0;
        
        /** Animation mode (AtlasAnimationMode enum value) */
        uint32_t AtlasAnimMode = 0;
        
        /** Frame rate for RealTime animation mode (frames per second) */
        float AtlasFrameRate = 30.0f;
        
        /** Minimum speed for BySpeed animation mode */
        float AtlasSpeedMin = 0.0f;
        
        /** Maximum speed for BySpeed animation mode */
        float AtlasSpeedMax = 10.0f;
        
        /** Loop animation flag (1 = loop, 0 = clamp to last frame) */
        uint32_t AtlasLoop = 1;

        //---------------------------------------------------------------------
        // Constructors
        //---------------------------------------------------------------------

        RenderPushConstants() = default;

        /**
         * @brief Construct push constants from a TextureAtlas
         */
        explicit RenderPushConstants(const TextureAtlas& atlas) {
            SetAtlas(atlas);
        }

        //---------------------------------------------------------------------
        // Helper Methods
        //---------------------------------------------------------------------

        /**
         * @brief Configure atlas parameters from a TextureAtlas struct
         */
        void SetAtlas(const TextureAtlas& atlas) {
            AtlasColumns = atlas.Columns;
            AtlasRows = atlas.Rows;
            AtlasFrameCount = atlas.FrameCount;
            AtlasAnimMode = static_cast<uint32_t>(atlas.AnimationMode);
            AtlasFrameRate = atlas.FrameRate;
            AtlasSpeedMin = atlas.SpeedMin;
            AtlasSpeedMax = atlas.SpeedMax;
            AtlasLoop = atlas.Loop ? 1 : 0;
        }

        /**
         * @brief Clear atlas parameters (disable atlas)
         */
        void ClearAtlas() {
            AtlasColumns = 0;
            AtlasRows = 0;
            AtlasFrameCount = 0;
            AtlasAnimMode = 0;
            AtlasFrameRate = 30.0f;
            AtlasSpeedMin = 0.0f;
            AtlasSpeedMax = 10.0f;
            AtlasLoop = 1;
        }

        /**
         * @brief Check if atlas is enabled
         */
        bool HasAtlas() const {
            return AtlasColumns > 0 && AtlasRows > 0 && AtlasFrameCount > 1;
        }
    };
    static_assert(sizeof(RenderPushConstants) == 56, "RenderPushConstants must be 56 bytes");

    //=========================================================================
    // Sort Stage Info
    //=========================================================================

    /**
     * @brief Information about a bitonic sort stage
     */
    struct SortStageInfo {
        uint32_t StageIndex = 0;
        uint32_t PassCount = 0;
        uint32_t DispatchSize = 0;
    };

    //=========================================================================
    // Particle Render Pass Statistics
    //=========================================================================

    /**
     * @brief Statistics for the particle render pass
     */
    struct ParticleRenderPassStats {
        uint32_t ParticlesRendered = 0;
        uint32_t OpaqueParticles = 0;
        uint32_t TransparentParticles = 0;
        uint32_t BatchCount = 0;
        uint32_t SortIterations = 0;
        double SortTimeMs = 0.0;
        double RenderTimeMs = 0.0;
        double TotalTimeMs = 0.0;

        void Reset() {
            ParticlesRendered = 0;
            OpaqueParticles = 0;
            TransparentParticles = 0;
            BatchCount = 0;
            SortIterations = 0;
            SortTimeMs = 0.0;
            RenderTimeMs = 0.0;
            TotalTimeMs = 0.0;
        }
    };

    //=========================================================================
    // Particle Render Pass Class
    //=========================================================================

    /**
     * @brief Specialized render pass for particle sorting and blending
     * 
     * This class manages:
     * - GPU-based depth sorting of particles (bitonic sort)
     * - Multiple blend mode pipelines for different particle effects
     * - Separate opaque and transparent particle passes
     * - Soft particles using depth buffer comparison
     * - View-space depth calculation for accurate sorting
     * 
     * Integrates with the existing particle system and follows the
     * render pass pattern used by ZPrepass and ShadowPass.
     */
    class ParticleRenderPass {
    public:
        ParticleRenderPass();
        ~ParticleRenderPass();

        // Non-copyable
        ParticleRenderPass(const ParticleRenderPass&) = delete;
        ParticleRenderPass& operator=(const ParticleRenderPass&) = delete;

        //---------------------------------------------------------------------
        // Initialization
        //---------------------------------------------------------------------

        /**
         * @brief Initialize the particle render pass
         * @param device The RHI device for resource creation
         * @param config Render pass configuration
         */
        void Initialize(
            std::shared_ptr<RHI::RHIDevice> device,
            const ParticleRenderPassConfig& config = {}
        );

        /**
         * @brief Shutdown and release all resources
         */
        void Shutdown();

        /**
         * @brief Check if render pass is initialized
         */
        bool IsInitialized() const { return m_Initialized; }

        /**
         * @brief Resize render targets
         * @param width New width
         * @param height New height
         */
        void Resize(uint32_t width, uint32_t height);

        //---------------------------------------------------------------------
        // Configuration
        //---------------------------------------------------------------------

        /**
         * @brief Get current configuration
         */
        const ParticleRenderPassConfig& GetConfig() const { return m_Config; }

        /**
         * @brief Enable/disable soft particles
         */
        void SetSoftParticlesEnabled(bool enabled);

        /**
         * @brief Set soft particle fade scale
         */
        void SetSoftParticleScale(float scale);

        /**
         * @brief Enable/disable depth sorting
         */
        void SetDepthSortingEnabled(bool enabled);

        //---------------------------------------------------------------------
        // Depth Texture Binding
        //---------------------------------------------------------------------

        /**
         * @brief Set the scene depth texture for soft particles
         * @param depthTexture The depth texture from the Z-prepass or main pass
         */
        void SetSceneDepthTexture(std::shared_ptr<RHI::RHITexture> depthTexture);

        //---------------------------------------------------------------------
        // Camera Update
        //---------------------------------------------------------------------

        /**
         * @brief Update camera matrices for depth sorting
         * @param view View matrix
         * @param projection Projection matrix
         * @param cameraPosition Camera world position
         */
        void UpdateCamera(
            const Math::Mat4& view,
            const Math::Mat4& projection,
            const Math::Vec3& cameraPosition
        );

        //---------------------------------------------------------------------
        // Sorting
        //---------------------------------------------------------------------

        /**
         * @brief Dispatch compute shader for bitonic sort
         * 
         * Sorts particles by view-space depth for correct transparency
         * rendering (back-to-front order).
         * 
         * @param commandList Command list for compute dispatches
         * @param particleSystem The particle system to sort
         */
        void SortParticlesByDepth(
            std::shared_ptr<RHI::RHICommandList> commandList,
            ParticleSystem* particleSystem
        );

        /**
         * @brief Calculate required sort stages for particle count
         * @param particleCount Number of particles to sort
         * @return Vector of sort stage information
         */
        std::vector<SortStageInfo> CalculateSortStages(uint32_t particleCount) const;

        //---------------------------------------------------------------------
        // Rendering
        //---------------------------------------------------------------------

        /**
         * @brief Begin the particle render pass
         * @param commandList Command list for rendering
         */
        void BeginPass(std::shared_ptr<RHI::RHICommandList> commandList);

        /**
         * @brief End the particle render pass
         * @param commandList Command list for rendering
         */
        void EndPass(std::shared_ptr<RHI::RHICommandList> commandList);

        /**
         * @brief Render all active particles with correct blending
         * 
         * This performs:
         * 1. Opaque particle pass (if any, with depth writing)
         * 2. Transparent particle pass (sorted, no depth writing)
         * 
         * @param commandList Command list for draw calls
         * @param particleSystem The particle system to render
         * @param blendMode Blend mode for all particles (default: AlphaBlend)
         */
        void RenderParticles(
            std::shared_ptr<RHI::RHICommandList> commandList,
            ParticleSystem* particleSystem,
            ParticleBlendMode blendMode = ParticleBlendMode::AlphaBlend
        );

        /**
         * @brief Render particles with a specific blend mode
         * @param commandList Command list for draw calls
         * @param particleSystem The particle system to render
         * @param blendMode The blend mode to use
         * @param particleOffset Start index in the particle buffer
         * @param particleCount Number of particles to render
         */
        void RenderParticleBatch(
            std::shared_ptr<RHI::RHICommandList> commandList,
            ParticleSystem* particleSystem,
            ParticleBlendMode blendMode,
            uint32_t particleOffset,
            uint32_t particleCount
        );

        /**
         * @brief Render multiple batches with different blend modes
         * @param commandList Command list for draw calls
         * @param particleSystem The particle system to render
         * @param batches Vector of particle batches to render
         */
        void RenderParticleBatches(
            std::shared_ptr<RHI::RHICommandList> commandList,
            ParticleSystem* particleSystem,
            const std::vector<ParticleBatch>& batches
        );

        //---------------------------------------------------------------------
        // Render Pass Access
        //---------------------------------------------------------------------

        /**
         * @brief Get the underlying render pass for opaque particles
         */
        std::shared_ptr<RHI::RHIRenderPass> GetOpaqueRenderPass() const { return m_OpaqueRenderPass; }

        /**
         * @brief Get the underlying render pass for transparent particles
         */
        std::shared_ptr<RHI::RHIRenderPass> GetTransparentRenderPass() const { return m_TransparentRenderPass; }

        /**
         * @brief Get the render target color texture
         */
        std::shared_ptr<RHI::RHITexture> GetColorTexture() const { return m_ColorTexture; }

        /**
         * @brief Get the pipeline for a specific blend mode
         * @param blendMode The blend mode
         * @return The pipeline state for that blend mode
         */
        std::shared_ptr<RHI::RHIPipelineState> GetBlendPipeline(ParticleBlendMode blendMode) const;

        //---------------------------------------------------------------------
        // Statistics
        //---------------------------------------------------------------------

        /**
         * @brief Get render pass statistics
         */
        const ParticleRenderPassStats& GetStats() const { return m_Stats; }

        /**
         * @brief Reset statistics
         */
        void ResetStats() { m_Stats.Reset(); }

        //---------------------------------------------------------------------
        // Blend State Helpers
        //---------------------------------------------------------------------

        /**
         * @brief Create a blend state for the given blend mode
         * @param blendMode The particle blend mode
         * @return Configured blend state
         */
        static RHI::RenderTargetBlendState CreateBlendState(ParticleBlendMode blendMode);

        /**
         * @brief Get the number of supported blend modes
         */
        static constexpr uint32_t GetBlendModeCount() { 
            return static_cast<uint32_t>(ParticleBlendMode::Count); 
        }

    private:
        //---------------------------------------------------------------------
        // Internal Resource Creation
        //---------------------------------------------------------------------

        void CreateRenderPasses();
        void CreatePipelines();
        void CreateSortPipeline();
        void CreateRenderTargets();
        void CreateDescriptorSets();
        void UpdateCameraBuffer();
        
        //---------------------------------------------------------------------
        // Sort Helpers
        //---------------------------------------------------------------------
        
        uint32_t CalculateDispatchGroups(uint32_t count, uint32_t workgroupSize) const;
        uint32_t NextPowerOfTwo(uint32_t n) const;

    private:
        std::shared_ptr<RHI::RHIDevice> m_Device;
        ParticleRenderPassConfig m_Config;
        ParticleRenderPassStats m_Stats;
        bool m_Initialized = false;

        //---------------------------------------------------------------------
        // Render Passes
        //---------------------------------------------------------------------

        // Opaque particle render pass (depth write enabled)
        std::shared_ptr<RHI::RHIRenderPass> m_OpaqueRenderPass;

        // Transparent particle render pass (depth write disabled)
        std::shared_ptr<RHI::RHIRenderPass> m_TransparentRenderPass;

        //---------------------------------------------------------------------
        // Pipelines
        //---------------------------------------------------------------------

        // Pipelines for each blend mode
        std::array<std::shared_ptr<RHI::RHIPipelineState>, 
                   static_cast<size_t>(ParticleBlendMode::Count)> m_BlendPipelines;

        // Opaque particle pipeline (no blending)
        std::shared_ptr<RHI::RHIPipelineState> m_OpaquePipeline;

        // Sort compute pipeline
        std::shared_ptr<RHI::RHIPipelineState> m_SortPipeline;

        //---------------------------------------------------------------------
        // Render Targets
        //---------------------------------------------------------------------

        std::shared_ptr<RHI::RHITexture> m_ColorTexture;
        std::shared_ptr<RHI::RHITexture> m_DepthTexture;

        // External scene depth for soft particles
        std::shared_ptr<RHI::RHITexture> m_SceneDepthTexture;

        //---------------------------------------------------------------------
        // Uniform Buffers
        //---------------------------------------------------------------------

        std::shared_ptr<RHI::RHIBuffer> m_CameraBuffer;

        //---------------------------------------------------------------------
        // Camera Data (CPU-side)
        //---------------------------------------------------------------------

        Math::Mat4 m_ViewMatrix{1.0f};
        Math::Mat4 m_ProjectionMatrix{1.0f};
        Math::Mat4 m_ViewProjectionMatrix{1.0f};
        Math::Vec3 m_CameraPosition{0.0f};
        Math::Vec3 m_CameraRight{1.0f, 0.0f, 0.0f};
        Math::Vec3 m_CameraUp{0.0f, 1.0f, 0.0f};
        bool m_CameraBufferDirty = true;

        //---------------------------------------------------------------------
        // State
        //---------------------------------------------------------------------

        bool m_PassActive = false;
    };

} // namespace Particles
} // namespace Renderer
} // namespace Core
