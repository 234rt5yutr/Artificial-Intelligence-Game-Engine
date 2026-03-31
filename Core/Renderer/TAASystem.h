#pragma once

#include "Core/Math/Math.h"
#include "Core/RHI/RHIBuffer.h"
#include "Core/RHI/RHITexture.h"
#include "Core/RHI/RHIDevice.h"
#include "Core/RHI/RHICommandList.h"
#include <memory>
#include <array>
#include <cstdint>

namespace Core {
namespace Renderer {

    //=========================================================================
    // Constants
    //=========================================================================
    
    constexpr uint32_t TAA_WORKGROUP_SIZE = 8;
    constexpr uint32_t TAA_JITTER_SEQUENCE_LENGTH = 16;

    //=========================================================================
    // GPU-aligned Structures
    //=========================================================================

    // TAA parameters uniform buffer (must match shader)
    struct alignas(16) TAAParams {
        Math::Vec4 Resolution;        // xy = current resolution, zw = 1/resolution
        Math::Vec4 JitterOffset;      // xy = current jitter, zw = previous jitter
        float BlendFactor;            // Base blend factor (0.9-0.95 typical)
        float MotionScale;            // Motion vector scale
        float VarianceClipGamma;      // Variance clipping gamma (1.0-2.0)
        float Sharpness;              // Sharpening amount (0.0-1.0)
        uint32_t FrameIndex;
        uint32_t Flags;               // Bit 0: enable variance clipping, Bit 1: enable sharpening
        float Padding0;
        float Padding1;

        TAAParams()
            : Resolution(1920.0f, 1080.0f, 1.0f/1920.0f, 1.0f/1080.0f)
            , JitterOffset(0.0f)
            , BlendFactor(0.9f)
            , MotionScale(1.0f)
            , VarianceClipGamma(1.25f)
            , Sharpness(0.25f)
            , FrameIndex(0)
            , Flags(0x3)  // Both variance clipping and sharpening enabled
            , Padding0(0.0f)
            , Padding1(0.0f)
        {}
    };
    static_assert(sizeof(TAAParams) == 64, "TAAParams must be 64 bytes");

    // Motion vectors UBO for velocity pass
    struct alignas(16) MotionVectorUBO {
        Math::Mat4 CurrentModel;
        Math::Mat4 CurrentViewProjection;
        Math::Mat4 PreviousModel;
        Math::Mat4 PreviousViewProjection;
        Math::Vec2 JitterOffset;
        Math::Vec2 PrevJitterOffset;
    };
    static_assert(sizeof(MotionVectorUBO) == 272, "MotionVectorUBO must be 272 bytes");

    //=========================================================================
    // Jitter Patterns
    //=========================================================================
    
    enum class JitterPattern {
        Halton23,        // Halton sequence (base 2, 3) - low discrepancy
        R2,              // Roberts R2 sequence - optimal coverage
        BlueNoise,       // Blue noise dithered pattern
        Grid4x4,         // Simple 4x4 grid pattern
        None             // No jittering (disables TAA effectively)
    };

    //=========================================================================
    // TAA Quality Presets
    //=========================================================================
    
    enum class TAAQuality {
        Low,       // Fast, minimal ghosting reduction
        Medium,    // Balanced quality/performance
        High,      // High quality with variance clipping
        Ultra      // Maximum quality, all features enabled
    };

    struct TAAQualitySettings {
        float BlendFactor;
        float VarianceClipGamma;
        float Sharpness;
        bool EnableVarianceClipping;
        bool EnableSharpening;
        bool UseCatmullRomSampling;
        JitterPattern Pattern;
    };

    //=========================================================================
    // Configuration
    //=========================================================================
    
    struct TAAConfig {
        bool Enabled = true;
        TAAQuality Quality = TAAQuality::High;
        JitterPattern Pattern = JitterPattern::Halton23;
        
        // Manual override settings (used when Quality is custom)
        float BlendFactor = 0.9f;
        float VarianceClipGamma = 1.25f;
        float Sharpness = 0.25f;
        float MotionScale = 1.0f;
        
        bool EnableVarianceClipping = true;
        bool EnableSharpening = true;

        static TAAQualitySettings GetPresetSettings(TAAQuality quality);
    };

    //=========================================================================
    // TAA Statistics
    //=========================================================================
    
    struct TAAStats {
        uint32_t FrameIndex;
        float ResolveTimeMs;
        float MotionVectorTimeMs;
        Math::Vec2 CurrentJitter;
        float AverageMotionMagnitude;

        TAAStats() 
            : FrameIndex(0), ResolveTimeMs(0.0f), MotionVectorTimeMs(0.0f)
            , CurrentJitter(0.0f), AverageMotionMagnitude(0.0f) {}
    };

    //=========================================================================
    // Temporal Anti-Aliasing System
    //=========================================================================

    class TAASystem {
    public:
        TAASystem();
        ~TAASystem();

        // Non-copyable
        TAASystem(const TAASystem&) = delete;
        TAASystem& operator=(const TAASystem&) = delete;

        //---------------------------------------------------------------------
        // Initialization
        //---------------------------------------------------------------------
        
        void Initialize(std::shared_ptr<RHI::RHIDevice> device, uint32_t width, uint32_t height);
        void Shutdown();
        void Resize(uint32_t width, uint32_t height);

        //---------------------------------------------------------------------
        // Configuration
        //---------------------------------------------------------------------
        
        void SetConfig(const TAAConfig& config);
        const TAAConfig& GetConfig() const { return m_Config; }
        
        void SetEnabled(bool enabled) { m_Config.Enabled = enabled; }
        bool IsEnabled() const { return m_Config.Enabled; }
        
        void SetQuality(TAAQuality quality);
        void SetBlendFactor(float factor);
        void SetSharpness(float sharpness);
        void SetVarianceClipGamma(float gamma);
        void SetJitterPattern(JitterPattern pattern);

        //---------------------------------------------------------------------
        // Per-Frame Operations
        //---------------------------------------------------------------------
        
        // Call at the start of each frame to get jitter offset for the projection matrix
        Math::Vec2 GetJitterOffset() const { return m_CurrentJitter; }
        Math::Vec2 GetJitterOffsetPixels() const;
        
        // Advance to next frame (updates jitter, swaps history buffers)
        void BeginFrame();
        
        // Store previous frame matrices for motion vector calculation
        void SetPreviousMatrices(const Math::Mat4& view, const Math::Mat4& projection);
        void SetCurrentMatrices(const Math::Mat4& view, const Math::Mat4& projection);

        //---------------------------------------------------------------------
        // Render Passes
        //---------------------------------------------------------------------
        
        // Record motion vector pass commands
        // (Should be called during geometry rendering or as separate pass)
        void RecordMotionVectorPass(std::shared_ptr<RHI::RHICommandList> commandList);
        
        // Execute TAA resolve (dispatch compute shader)
        void ExecuteResolve(std::shared_ptr<RHI::RHICommandList> commandList,
                           std::shared_ptr<RHI::RHITexture> currentColor,
                           std::shared_ptr<RHI::RHITexture> currentDepth,
                           std::shared_ptr<RHI::RHITexture> motionVectors);

        //---------------------------------------------------------------------
        // Output Access
        //---------------------------------------------------------------------
        
        // Get resolved TAA output texture
        std::shared_ptr<RHI::RHITexture> GetResolvedTexture() const { return m_ResolvedColor; }
        
        // Get history texture (previous frame)
        std::shared_ptr<RHI::RHITexture> GetHistoryTexture() const { return m_HistoryColor[m_HistoryIndex]; }
        
        // Get motion vector render target
        std::shared_ptr<RHI::RHITexture> GetMotionVectorTexture() const { return m_MotionVectorTexture; }

        //---------------------------------------------------------------------
        // Statistics
        //---------------------------------------------------------------------
        
        const TAAStats& GetStats() const { return m_Stats; }
        uint32_t GetFrameIndex() const { return m_FrameIndex; }

        //---------------------------------------------------------------------
        // Jitter Generation
        //---------------------------------------------------------------------
        
        // Generate jitter sequence value at given index
        static Math::Vec2 GenerateJitter(JitterPattern pattern, uint32_t index, uint32_t width, uint32_t height);
        static Math::Vec2 Halton23(uint32_t index);
        static Math::Vec2 R2Sequence(uint32_t index);
        static Math::Vec2 Grid4x4(uint32_t index);

        //---------------------------------------------------------------------
        // Utility
        //---------------------------------------------------------------------
        
        // Apply jitter to projection matrix
        static Math::Mat4 ApplyJitterToProjection(const Math::Mat4& projection, 
                                                   const Math::Vec2& jitterPixels,
                                                   uint32_t width, uint32_t height);

        // Reset history (call after camera cut or teleport)
        void InvalidateHistory();

    private:
        void CreateTextures();
        void CreateBuffers();
        void UpdateParams();
        void SwapHistoryBuffers();
        Math::Vec2 CalculateJitter();

        float HaltonSequence(uint32_t index, uint32_t base);

    private:
        std::shared_ptr<RHI::RHIDevice> m_Device;
        TAAConfig m_Config;
        TAAStats m_Stats;
        bool m_Initialized = false;

        // Dimensions
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;

        // Frame tracking
        uint32_t m_FrameIndex = 0;
        uint32_t m_HistoryIndex = 0;  // Ping-pong index

        // Jitter state
        Math::Vec2 m_CurrentJitter;
        Math::Vec2 m_PreviousJitter;
        bool m_HistoryValid = false;

        // Matrices for motion vectors
        Math::Mat4 m_CurrentView;
        Math::Mat4 m_CurrentProjection;
        Math::Mat4 m_PreviousView;
        Math::Mat4 m_PreviousProjection;

        // GPU Textures
        std::shared_ptr<RHI::RHITexture> m_HistoryColor[2];  // Ping-pong history
        std::shared_ptr<RHI::RHITexture> m_ResolvedColor;
        std::shared_ptr<RHI::RHITexture> m_MotionVectorTexture;

        // GPU Buffers
        std::shared_ptr<RHI::RHIBuffer> m_TAAParamsBuffer;
        std::shared_ptr<RHI::RHIBuffer> m_MotionVectorUBOBuffer;

        // TAA parameters
        TAAParams m_Params;
    };

} // namespace Renderer
} // namespace Core
