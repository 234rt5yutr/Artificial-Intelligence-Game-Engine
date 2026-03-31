#pragma once

#include "Core/Math/Math.h"
#include "Core/RHI/RHITexture.h"
#include "Core/RHI/RHIDevice.h"
#include <memory>
#include <array>
#include <cstdint>
#include <chrono>
#include <deque>

namespace Core {
namespace Renderer {

    //=========================================================================
    // Constants
    //=========================================================================
    
    constexpr uint32_t DRS_FRAME_HISTORY_SIZE = 32;
    constexpr float DRS_MIN_SCALE = 0.5f;    // 50% minimum resolution
    constexpr float DRS_MAX_SCALE = 1.0f;    // 100% maximum resolution
    constexpr float DRS_DEFAULT_TARGET_MS = 16.67f;  // 60 FPS target

    //=========================================================================
    // Enums
    //=========================================================================

    enum class DRSMode {
        Disabled,           // Always render at native resolution
        FixedScale,         // Fixed scale factor (no adaptation)
        FrameTime,          // Adapt based on frame completion time
        GPUTime,            // Adapt based on GPU render time only
        Hybrid              // Use both CPU and GPU timing
    };

    enum class DRSScalingMethod {
        Uniform,            // Scale X and Y equally
        PreferWidth,        // Scale width more aggressively
        PreferHeight,       // Scale height more aggressively
        Temporal            // Use temporal upscaling hints
    };

    enum class DRSUpscaleMethod {
        Bilinear,           // Simple bilinear filtering
        Bicubic,            // Catmull-Rom bicubic
        Lanczos,            // Lanczos resampling
        TAA,                // Let TAA handle upscaling
        FSR,                // AMD FidelityFX Super Resolution (stub)
        DLSS                // NVIDIA DLSS (stub)
    };

    //=========================================================================
    // Timing Statistics
    //=========================================================================

    struct FrameTimingData {
        float FrameTimeMs;          // Total frame time
        float GPUTimeMs;            // GPU render time
        float CPUTimeMs;            // CPU time
        float RenderScaleUsed;      // Scale used this frame
        uint64_t FrameNumber;       // Frame index
        std::chrono::steady_clock::time_point Timestamp;

        FrameTimingData()
            : FrameTimeMs(0.0f), GPUTimeMs(0.0f), CPUTimeMs(0.0f)
            , RenderScaleUsed(1.0f), FrameNumber(0) {}
    };

    struct DRSStatistics {
        // Current state
        float CurrentScale;
        uint32_t RenderWidth;
        uint32_t RenderHeight;
        uint32_t NativeWidth;
        uint32_t NativeHeight;

        // Timing averages
        float AverageFrameTimeMs;
        float AverageGPUTimeMs;
        float MinFrameTimeMs;
        float MaxFrameTimeMs;
        float FrameTimeVariance;

        // Adaptation stats
        uint32_t ScaleIncreases;
        uint32_t ScaleDecreases;
        float TimeAtMinScale;       // Seconds spent at minimum scale
        float TimeAtMaxScale;       // Seconds spent at maximum scale

        // Target tracking
        float TargetFrameTimeMs;
        float HeadroomMs;           // How much under target we are
        bool IsGPUBound;

        DRSStatistics()
            : CurrentScale(1.0f)
            , RenderWidth(0), RenderHeight(0)
            , NativeWidth(0), NativeHeight(0)
            , AverageFrameTimeMs(0.0f), AverageGPUTimeMs(0.0f)
            , MinFrameTimeMs(1000.0f), MaxFrameTimeMs(0.0f)
            , FrameTimeVariance(0.0f)
            , ScaleIncreases(0), ScaleDecreases(0)
            , TimeAtMinScale(0.0f), TimeAtMaxScale(0.0f)
            , TargetFrameTimeMs(16.67f), HeadroomMs(0.0f)
            , IsGPUBound(false) {}

        void Reset() {
            CurrentScale = 1.0f;
            AverageFrameTimeMs = 0.0f;
            AverageGPUTimeMs = 0.0f;
            MinFrameTimeMs = 1000.0f;
            MaxFrameTimeMs = 0.0f;
            FrameTimeVariance = 0.0f;
            ScaleIncreases = 0;
            ScaleDecreases = 0;
            TimeAtMinScale = 0.0f;
            TimeAtMaxScale = 0.0f;
        }
    };

    //=========================================================================
    // Configuration
    //=========================================================================

    struct DRSConfig {
        DRSMode Mode = DRSMode::FrameTime;
        DRSScalingMethod ScalingMethod = DRSScalingMethod::Uniform;
        DRSUpscaleMethod UpscaleMethod = DRSUpscaleMethod::Bilinear;

        // Target frame time
        float TargetFrameTimeMs = DRS_DEFAULT_TARGET_MS;  // 60 FPS
        float TargetFPS = 60.0f;

        // Scale bounds
        float MinScale = DRS_MIN_SCALE;
        float MaxScale = DRS_MAX_SCALE;
        float FixedScale = 1.0f;    // Used when Mode == FixedScale

        // Adaptation parameters
        float IncreaseRate = 0.02f;     // How fast to increase scale
        float DecreaseRate = 0.05f;     // How fast to decrease scale (faster for responsiveness)
        float Hysteresis = 0.05f;       // Deadzone around target to prevent oscillation
        float SmoothingFactor = 0.8f;   // Exponential smoothing for timing data

        // Thresholds
        float IncreaseThresholdMs = 2.0f;   // Must be this much under target to increase
        float DecreaseThresholdMs = 1.0f;   // Must be this much over target to decrease
        
        // Advanced options
        bool EnableMipBias = true;      // Adjust mip bias based on scale
        float MipBiasScale = 0.5f;      // How much to adjust mip bias
        bool PreventSpikes = true;      // Prevent sudden scale changes on frame spikes
        uint32_t SpikeFrameThreshold = 3;   // Frames over target before decreasing

        // Set target FPS (convenience method)
        void SetTargetFPS(float fps) {
            TargetFPS = fps;
            TargetFrameTimeMs = 1000.0f / fps;
        }
    };

    //=========================================================================
    // Dynamic Resolution Scaling System
    //=========================================================================

    class DynamicResolutionSystem {
    public:
        DynamicResolutionSystem();
        ~DynamicResolutionSystem();

        // Non-copyable
        DynamicResolutionSystem(const DynamicResolutionSystem&) = delete;
        DynamicResolutionSystem& operator=(const DynamicResolutionSystem&) = delete;

        //---------------------------------------------------------------------
        // Initialization
        //---------------------------------------------------------------------
        
        void Initialize(std::shared_ptr<RHI::RHIDevice> device, 
                       uint32_t nativeWidth, uint32_t nativeHeight);
        void Shutdown();

        // Call when window/display resolution changes
        void SetNativeResolution(uint32_t width, uint32_t height);

        //---------------------------------------------------------------------
        // Configuration
        //---------------------------------------------------------------------
        
        void SetConfig(const DRSConfig& config);
        const DRSConfig& GetConfig() const { return m_Config; }

        void SetMode(DRSMode mode);
        void SetTargetFrameTime(float targetMs);
        void SetTargetFPS(float fps);
        void SetScaleBounds(float minScale, float maxScale);
        void SetFixedScale(float scale);

        void Enable() { m_Config.Mode = DRSMode::FrameTime; }
        void Disable() { m_Config.Mode = DRSMode::Disabled; }
        bool IsEnabled() const { return m_Config.Mode != DRSMode::Disabled; }

        //---------------------------------------------------------------------
        // Per-Frame Operations
        //---------------------------------------------------------------------

        // Call at the start of each frame to get render resolution
        void BeginFrame();

        // Record frame timing data
        void RecordFrameTime(float frameTimeMs);
        void RecordGPUTime(float gpuTimeMs);
        void RecordCPUTime(float cpuTimeMs);

        // Call at end of frame to update scaling decision
        void EndFrame();

        //---------------------------------------------------------------------
        // Resolution Queries
        //---------------------------------------------------------------------

        // Get current render resolution (may be lower than native)
        uint32_t GetRenderWidth() const { return m_RenderWidth; }
        uint32_t GetRenderHeight() const { return m_RenderHeight; }
        
        // Get native/display resolution
        uint32_t GetNativeWidth() const { return m_NativeWidth; }
        uint32_t GetNativeHeight() const { return m_NativeHeight; }

        // Get current scale factor (0.0 - 1.0)
        float GetCurrentScale() const { return m_CurrentScale; }
        
        // Get scale for next frame (may differ during transitions)
        float GetTargetScale() const { return m_TargetScale; }

        // Get render-to-native scale factors
        Math::Vec2 GetScaleFactors() const;

        // Get recommended mip bias for textures
        float GetMipBias() const;

        //---------------------------------------------------------------------
        // Statistics
        //---------------------------------------------------------------------
        
        const DRSStatistics& GetStatistics() const { return m_Stats; }
        
        // Get frame history for debugging
        const std::deque<FrameTimingData>& GetFrameHistory() const { return m_FrameHistory; }

        //---------------------------------------------------------------------
        // Manual Control
        //---------------------------------------------------------------------
        
        // Force a specific scale (overrides automatic scaling for one frame)
        void ForceScale(float scale);
        
        // Reset to native resolution
        void ResetToNative();
        
        // Lock current scale (prevent automatic changes)
        void LockScale(bool locked) { m_ScaleLocked = locked; }
        bool IsScaleLocked() const { return m_ScaleLocked; }

    private:
        void UpdateScale();
        void CalculateRenderResolution();
        void UpdateStatistics();
        float CalculateTargetScale();
        float SmoothScale(float targetScale);

        // Timing helpers
        float GetSmoothedFrameTime() const;
        float GetSmoothedGPUTime() const;
        bool IsFrameTimeStable() const;

    private:
        std::shared_ptr<RHI::RHIDevice> m_Device;
        DRSConfig m_Config;
        DRSStatistics m_Stats;
        bool m_Initialized = false;

        // Native (output) resolution
        uint32_t m_NativeWidth = 1920;
        uint32_t m_NativeHeight = 1080;

        // Current render resolution
        uint32_t m_RenderWidth = 1920;
        uint32_t m_RenderHeight = 1080;

        // Scale factors
        float m_CurrentScale = 1.0f;
        float m_TargetScale = 1.0f;
        float m_PreviousScale = 1.0f;

        // Frame timing history
        std::deque<FrameTimingData> m_FrameHistory;
        FrameTimingData m_CurrentFrameData;

        // Smoothed timing values
        float m_SmoothedFrameTime = 16.67f;
        float m_SmoothedGPUTime = 8.0f;

        // State tracking
        uint64_t m_FrameNumber = 0;
        uint32_t m_FramesOverTarget = 0;
        uint32_t m_FramesUnderTarget = 0;
        bool m_ScaleLocked = false;
        bool m_ForceScaleNextFrame = false;
        float m_ForcedScale = 1.0f;

        // Timing
        std::chrono::steady_clock::time_point m_LastFrameTime;
        std::chrono::steady_clock::time_point m_LastScaleChangeTime;
    };

} // namespace Renderer
} // namespace Core
