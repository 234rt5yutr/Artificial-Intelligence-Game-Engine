#include "Core/Renderer/DynamicResolution.h"
#include "Core/Log.h"
#include "Core/Profile.h"
#include <algorithm>
#include <cmath>
#include <numeric>

// Use engine core log macros
#define LOG_INFO    ENGINE_CORE_INFO
#define LOG_WARN    ENGINE_CORE_WARN
#define LOG_DEBUG   ENGINE_CORE_TRACE
#define LOG_ERROR   ENGINE_CORE_ERROR

namespace Core {
namespace Renderer {

    //=========================================================================
    // Constructor / Destructor
    //=========================================================================

    DynamicResolutionSystem::DynamicResolutionSystem()
        : m_LastFrameTime(std::chrono::steady_clock::now())
        , m_LastScaleChangeTime(std::chrono::steady_clock::now())
    {
    }

    DynamicResolutionSystem::~DynamicResolutionSystem()
    {
        Shutdown();
    }

    //=========================================================================
    // Initialization
    //=========================================================================

    void DynamicResolutionSystem::Initialize(std::shared_ptr<RHI::RHIDevice> device,
                                              uint32_t nativeWidth, uint32_t nativeHeight)
    {
        if (m_Initialized) {
            LOG_WARN("DynamicResolutionSystem already initialized");
            return;
        }

        m_Device = device;
        m_NativeWidth = nativeWidth;
        m_NativeHeight = nativeHeight;
        m_RenderWidth = nativeWidth;
        m_RenderHeight = nativeHeight;

        m_Stats.NativeWidth = nativeWidth;
        m_Stats.NativeHeight = nativeHeight;
        m_Stats.RenderWidth = nativeWidth;
        m_Stats.RenderHeight = nativeHeight;
        m_Stats.TargetFrameTimeMs = m_Config.TargetFrameTimeMs;

        m_Initialized = true;
        LOG_INFO("DynamicResolutionSystem initialized ({}x{}, target: {:.2f}ms)", 
                nativeWidth, nativeHeight, m_Config.TargetFrameTimeMs);
    }

    void DynamicResolutionSystem::Shutdown()
    {
        if (!m_Initialized) {
            return;
        }

        m_FrameHistory.clear();
        m_Device.reset();
        m_Initialized = false;

        LOG_INFO("DynamicResolutionSystem shutdown");
    }

    void DynamicResolutionSystem::SetNativeResolution(uint32_t width, uint32_t height)
    {
        if (width == m_NativeWidth && height == m_NativeHeight) {
            return;
        }

        m_NativeWidth = width;
        m_NativeHeight = height;
        m_Stats.NativeWidth = width;
        m_Stats.NativeHeight = height;

        // Recalculate render resolution with current scale
        CalculateRenderResolution();

        LOG_INFO("Native resolution changed to {}x{}", width, height);
    }

    //=========================================================================
    // Configuration
    //=========================================================================

    void DynamicResolutionSystem::SetConfig(const DRSConfig& config)
    {
        m_Config = config;
        m_Stats.TargetFrameTimeMs = config.TargetFrameTimeMs;

        // Clamp fixed scale to bounds
        if (config.Mode == DRSMode::FixedScale) {
            m_CurrentScale = std::clamp(config.FixedScale, config.MinScale, config.MaxScale);
            m_TargetScale = m_CurrentScale;
            CalculateRenderResolution();
        }
    }

    void DynamicResolutionSystem::SetMode(DRSMode mode)
    {
        m_Config.Mode = mode;

        if (mode == DRSMode::Disabled) {
            ResetToNative();
        } else if (mode == DRSMode::FixedScale) {
            m_CurrentScale = std::clamp(m_Config.FixedScale, m_Config.MinScale, m_Config.MaxScale);
            m_TargetScale = m_CurrentScale;
            CalculateRenderResolution();
        }
    }

    void DynamicResolutionSystem::SetTargetFrameTime(float targetMs)
    {
        m_Config.TargetFrameTimeMs = targetMs;
        m_Config.TargetFPS = 1000.0f / targetMs;
        m_Stats.TargetFrameTimeMs = targetMs;
    }

    void DynamicResolutionSystem::SetTargetFPS(float fps)
    {
        m_Config.TargetFPS = fps;
        m_Config.TargetFrameTimeMs = 1000.0f / fps;
        m_Stats.TargetFrameTimeMs = m_Config.TargetFrameTimeMs;
    }

    void DynamicResolutionSystem::SetScaleBounds(float minScale, float maxScale)
    {
        m_Config.MinScale = std::clamp(minScale, 0.25f, 1.0f);
        m_Config.MaxScale = std::clamp(maxScale, m_Config.MinScale, 2.0f);

        // Clamp current scale to new bounds
        m_CurrentScale = std::clamp(m_CurrentScale, m_Config.MinScale, m_Config.MaxScale);
        m_TargetScale = std::clamp(m_TargetScale, m_Config.MinScale, m_Config.MaxScale);
        CalculateRenderResolution();
    }

    void DynamicResolutionSystem::SetFixedScale(float scale)
    {
        m_Config.FixedScale = std::clamp(scale, m_Config.MinScale, m_Config.MaxScale);
        
        if (m_Config.Mode == DRSMode::FixedScale) {
            m_CurrentScale = m_Config.FixedScale;
            m_TargetScale = m_Config.FixedScale;
            CalculateRenderResolution();
        }
    }

    //=========================================================================
    // Per-Frame Operations
    //=========================================================================

    void DynamicResolutionSystem::BeginFrame()
    {
        PROFILE_FUNCTION();

        m_FrameNumber++;
        m_CurrentFrameData = FrameTimingData();
        m_CurrentFrameData.FrameNumber = m_FrameNumber;
        m_CurrentFrameData.Timestamp = std::chrono::steady_clock::now();

        // Handle forced scale
        if (m_ForceScaleNextFrame) {
            m_CurrentScale = m_ForcedScale;
            m_TargetScale = m_ForcedScale;
            m_ForceScaleNextFrame = false;
            CalculateRenderResolution();
        }

        m_CurrentFrameData.RenderScaleUsed = m_CurrentScale;
    }

    void DynamicResolutionSystem::RecordFrameTime(float frameTimeMs)
    {
        m_CurrentFrameData.FrameTimeMs = frameTimeMs;

        // Exponential smoothing
        m_SmoothedFrameTime = m_SmoothedFrameTime * m_Config.SmoothingFactor +
                              frameTimeMs * (1.0f - m_Config.SmoothingFactor);
    }

    void DynamicResolutionSystem::RecordGPUTime(float gpuTimeMs)
    {
        m_CurrentFrameData.GPUTimeMs = gpuTimeMs;

        // Exponential smoothing
        m_SmoothedGPUTime = m_SmoothedGPUTime * m_Config.SmoothingFactor +
                            gpuTimeMs * (1.0f - m_Config.SmoothingFactor);
    }

    void DynamicResolutionSystem::RecordCPUTime(float cpuTimeMs)
    {
        m_CurrentFrameData.CPUTimeMs = cpuTimeMs;
    }

    void DynamicResolutionSystem::EndFrame()
    {
        PROFILE_FUNCTION();

        // Add to history
        m_FrameHistory.push_back(m_CurrentFrameData);
        while (m_FrameHistory.size() > DRS_FRAME_HISTORY_SIZE) {
            m_FrameHistory.pop_front();
        }

        // Update scale if enabled and not locked
        if (m_Config.Mode != DRSMode::Disabled && 
            m_Config.Mode != DRSMode::FixedScale && 
            !m_ScaleLocked) {
            UpdateScale();
        }

        // Update statistics
        UpdateStatistics();

        m_PreviousScale = m_CurrentScale;
        m_LastFrameTime = std::chrono::steady_clock::now();
    }

    //=========================================================================
    // Scale Calculation
    //=========================================================================

    void DynamicResolutionSystem::UpdateScale()
    {
        float targetScale = CalculateTargetScale();
        m_TargetScale = targetScale;

        // Smooth the scale change
        m_CurrentScale = SmoothScale(targetScale);

        // Clamp to bounds
        m_CurrentScale = std::clamp(m_CurrentScale, m_Config.MinScale, m_Config.MaxScale);

        // Update render resolution
        CalculateRenderResolution();
    }

    float DynamicResolutionSystem::CalculateTargetScale()
    {
        float frameTime = GetSmoothedFrameTime();
        float targetTime = m_Config.TargetFrameTimeMs;

        // Calculate how far off target we are
        float deviation = frameTime - targetTime;
        
        float newScale = m_CurrentScale;

        // Check if we should decrease scale (over budget)
        if (deviation > m_Config.DecreaseThresholdMs) {
            m_FramesOverTarget++;
            m_FramesUnderTarget = 0;

            // Only decrease if consistently over target (spike prevention)
            if (!m_Config.PreventSpikes || m_FramesOverTarget >= m_Config.SpikeFrameThreshold) {
                // Scale down proportionally to how much we're over budget
                float decreaseAmount = m_Config.DecreaseRate * (deviation / targetTime);
                newScale = m_CurrentScale - decreaseAmount;

                m_Stats.ScaleDecreases++;
                m_LastScaleChangeTime = std::chrono::steady_clock::now();
            }
        }
        // Check if we should increase scale (under budget with headroom)
        else if (deviation < -m_Config.IncreaseThresholdMs) {
            m_FramesUnderTarget++;
            m_FramesOverTarget = 0;

            // Only increase if we have consistent headroom
            if (m_FramesUnderTarget >= 3 && m_CurrentScale < m_Config.MaxScale) {
                // Scale up cautiously
                float headroom = -deviation / targetTime;
                float increaseAmount = m_Config.IncreaseRate * std::min(headroom, 0.5f);
                newScale = m_CurrentScale + increaseAmount;

                m_Stats.ScaleIncreases++;
                m_LastScaleChangeTime = std::chrono::steady_clock::now();
            }
        }
        // Within hysteresis zone - maintain current scale
        else {
            // Reset counters but don't change scale
            if (std::abs(deviation) < m_Config.Hysteresis * targetTime) {
                m_FramesOverTarget = 0;
                m_FramesUnderTarget = 0;
            }
        }

        return newScale;
    }

    float DynamicResolutionSystem::SmoothScale(float targetScale)
    {
        // Smooth transition between scales
        float diff = targetScale - m_CurrentScale;
        
        // Use different smoothing for increase vs decrease
        float smoothingRate;
        if (diff > 0) {
            // Increasing scale - be more cautious
            smoothingRate = 0.1f;
        } else {
            // Decreasing scale - be more responsive
            smoothingRate = 0.3f;
        }

        return m_CurrentScale + diff * smoothingRate;
    }

    void DynamicResolutionSystem::CalculateRenderResolution()
    {
        switch (m_Config.ScalingMethod) {
            case DRSScalingMethod::Uniform:
                m_RenderWidth = static_cast<uint32_t>(m_NativeWidth * m_CurrentScale);
                m_RenderHeight = static_cast<uint32_t>(m_NativeHeight * m_CurrentScale);
                break;

            case DRSScalingMethod::PreferWidth:
                // Scale width more aggressively
                m_RenderWidth = static_cast<uint32_t>(m_NativeWidth * m_CurrentScale);
                m_RenderHeight = static_cast<uint32_t>(m_NativeHeight * std::sqrt(m_CurrentScale));
                break;

            case DRSScalingMethod::PreferHeight:
                // Scale height more aggressively
                m_RenderWidth = static_cast<uint32_t>(m_NativeWidth * std::sqrt(m_CurrentScale));
                m_RenderHeight = static_cast<uint32_t>(m_NativeHeight * m_CurrentScale);
                break;

            case DRSScalingMethod::Temporal:
                // Same as uniform, but signal to TAA
                m_RenderWidth = static_cast<uint32_t>(m_NativeWidth * m_CurrentScale);
                m_RenderHeight = static_cast<uint32_t>(m_NativeHeight * m_CurrentScale);
                break;
        }

        // Ensure minimum of 1 pixel and even dimensions (some hardware prefers this)
        m_RenderWidth = std::max(m_RenderWidth, 2u);
        m_RenderHeight = std::max(m_RenderHeight, 2u);
        m_RenderWidth = (m_RenderWidth + 1) & ~1u;  // Round up to even
        m_RenderHeight = (m_RenderHeight + 1) & ~1u;

        m_Stats.RenderWidth = m_RenderWidth;
        m_Stats.RenderHeight = m_RenderHeight;
        m_Stats.CurrentScale = m_CurrentScale;
    }

    //=========================================================================
    // Resolution Queries
    //=========================================================================

    Math::Vec2 DynamicResolutionSystem::GetScaleFactors() const
    {
        if (m_NativeWidth == 0 || m_NativeHeight == 0) {
            return Math::Vec2(1.0f);
        }

        return Math::Vec2(
            static_cast<float>(m_RenderWidth) / static_cast<float>(m_NativeWidth),
            static_cast<float>(m_RenderHeight) / static_cast<float>(m_NativeHeight)
        );
    }

    float DynamicResolutionSystem::GetMipBias() const
    {
        if (!m_Config.EnableMipBias || m_CurrentScale >= 1.0f) {
            return 0.0f;
        }

        // Negative mip bias to sharpen textures at lower resolutions
        // log2(scale) gives the number of mip levels difference
        float mipOffset = std::log2(m_CurrentScale);
        return mipOffset * m_Config.MipBiasScale;
    }

    //=========================================================================
    // Statistics
    //=========================================================================

    void DynamicResolutionSystem::UpdateStatistics()
    {
        if (m_FrameHistory.empty()) {
            return;
        }

        // Calculate averages from history
        float sumFrameTime = 0.0f;
        float sumGPUTime = 0.0f;
        float minFrameTime = 1000.0f;
        float maxFrameTime = 0.0f;

        for (const auto& frame : m_FrameHistory) {
            sumFrameTime += frame.FrameTimeMs;
            sumGPUTime += frame.GPUTimeMs;
            minFrameTime = std::min(minFrameTime, frame.FrameTimeMs);
            maxFrameTime = std::max(maxFrameTime, frame.FrameTimeMs);
        }

        size_t count = m_FrameHistory.size();
        m_Stats.AverageFrameTimeMs = sumFrameTime / static_cast<float>(count);
        m_Stats.AverageGPUTimeMs = sumGPUTime / static_cast<float>(count);
        m_Stats.MinFrameTimeMs = minFrameTime;
        m_Stats.MaxFrameTimeMs = maxFrameTime;

        // Calculate variance
        float sumSquaredDiff = 0.0f;
        for (const auto& frame : m_FrameHistory) {
            float diff = frame.FrameTimeMs - m_Stats.AverageFrameTimeMs;
            sumSquaredDiff += diff * diff;
        }
        m_Stats.FrameTimeVariance = sumSquaredDiff / static_cast<float>(count);

        // Headroom calculation
        m_Stats.HeadroomMs = m_Config.TargetFrameTimeMs - m_Stats.AverageFrameTimeMs;

        // GPU bound detection
        if (m_Stats.AverageGPUTimeMs > 0.0f) {
            m_Stats.IsGPUBound = m_Stats.AverageGPUTimeMs > (m_Stats.AverageFrameTimeMs * 0.8f);
        }

        // Time at scale extremes
        auto now = std::chrono::steady_clock::now();
        float deltaSeconds = std::chrono::duration<float>(now - m_LastFrameTime).count();
        
        if (m_CurrentScale <= m_Config.MinScale + 0.01f) {
            m_Stats.TimeAtMinScale += deltaSeconds;
        }
        if (m_CurrentScale >= m_Config.MaxScale - 0.01f) {
            m_Stats.TimeAtMaxScale += deltaSeconds;
        }
    }

    float DynamicResolutionSystem::GetSmoothedFrameTime() const
    {
        return m_SmoothedFrameTime;
    }

    float DynamicResolutionSystem::GetSmoothedGPUTime() const
    {
        return m_SmoothedGPUTime;
    }

    bool DynamicResolutionSystem::IsFrameTimeStable() const
    {
        if (m_FrameHistory.size() < 5) {
            return false;
        }

        // Check variance relative to average
        float coefficientOfVariation = std::sqrt(m_Stats.FrameTimeVariance) / m_Stats.AverageFrameTimeMs;
        return coefficientOfVariation < 0.15f;  // Less than 15% variation
    }

    //=========================================================================
    // Manual Control
    //=========================================================================

    void DynamicResolutionSystem::ForceScale(float scale)
    {
        m_ForcedScale = std::clamp(scale, m_Config.MinScale, m_Config.MaxScale);
        m_ForceScaleNextFrame = true;
    }

    void DynamicResolutionSystem::ResetToNative()
    {
        m_CurrentScale = 1.0f;
        m_TargetScale = 1.0f;
        m_RenderWidth = m_NativeWidth;
        m_RenderHeight = m_NativeHeight;
        m_FramesOverTarget = 0;
        m_FramesUnderTarget = 0;

        m_Stats.CurrentScale = 1.0f;
        m_Stats.RenderWidth = m_NativeWidth;
        m_Stats.RenderHeight = m_NativeHeight;
    }

} // namespace Renderer
} // namespace Core
