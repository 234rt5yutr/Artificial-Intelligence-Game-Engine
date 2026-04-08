// Core/Renderer/RayTracingManager.h

#pragma once
#include "../RHI/RHIRayTracing.h"
#include <memory>
#include <functional>
#include <string>
#include <unordered_map>

namespace AIEngine::Rendering {

/// Ray tracing feature flags
enum class RTFeature : uint32_t {
    None = 0,
    Shadows = 1 << 0,
    Reflections = 1 << 1,
    GlobalIllumination = 1 << 2,
    AmbientOcclusion = 1 << 3,
    All = Shadows | Reflections | GlobalIllumination | AmbientOcclusion
};

inline RTFeature operator|(RTFeature a, RTFeature b) {
    return static_cast<RTFeature>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline RTFeature operator&(RTFeature a, RTFeature b) {
    return static_cast<RTFeature>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool HasFeature(RTFeature flags, RTFeature feature) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(feature)) != 0;
}

/// Render path selection
enum class RenderPath {
    Rasterization,      ///< Traditional rasterization (fallback)
    Hybrid,             ///< Rasterization + selective RT features
    FullRayTracing      ///< Maximum RT usage (future)
};

/// Quality scaling mode
enum class QualityScalingMode {
    Fixed,              ///< Use fixed quality settings
    Adaptive,           ///< Scale based on frame time
    Performance,        ///< Prioritize performance
    Quality             ///< Prioritize quality
};

/// Ray tracing quality settings
struct RTQualitySettings {
    // Shadow settings
    uint32_t shadowSamplesPerPixel = 1;
    float shadowSoftness = 0.1f;
    bool shadowDenoising = true;
    
    // Reflection settings
    uint32_t reflectionSamplesPerPixel = 1;
    uint32_t reflectionBounces = 1;
    float reflectionResolutionScale = 1.0f;
    bool reflectionDenoising = true;
    
    // GI settings
    uint32_t giSamplesPerPixel = 1;
    uint32_t giBounces = 1;
    float giIntensity = 1.0f;
    bool giDenoising = true;
    
    // General
    float resolutionScale = 1.0f;
    bool temporalAccumulation = true;
};

/// Preset quality levels
enum class RTQualityPreset {
    Off,
    Low,
    Medium,
    High,
    Ultra,
    Custom
};

/// Ray tracing manager - handles hybrid rendering and feature toggling
class RayTracingManager {
public:
    using FeatureChangedCallback = std::function<void(RTFeature, bool)>;
    using QualityChangedCallback = std::function<void(RTQualityPreset)>;
    
    RayTracingManager();
    ~RayTracingManager();
    
    /// Initialize with RHI device
    bool Initialize(RHI::RHIDeviceRT* device);
    
    /// Shutdown and cleanup
    void Shutdown();
    
    /// Check if ray tracing is available
    bool IsRayTracingAvailable() const { return m_IsAvailable; }
    
    /// Get current render path
    RenderPath GetRenderPath() const { return m_RenderPath; }
    
    /// Set render path
    void SetRenderPath(RenderPath path);
    
    /// Check if a specific feature is enabled
    bool IsFeatureEnabled(RTFeature feature) const;
    
    /// Enable or disable a specific feature
    void SetFeatureEnabled(RTFeature feature, bool enabled);
    
    /// Toggle a feature
    void ToggleFeature(RTFeature feature);
    
    /// Get all enabled features
    RTFeature GetEnabledFeatures() const { return m_EnabledFeatures; }
    
    /// Set quality preset
    void SetQualityPreset(RTQualityPreset preset);
    
    /// Get current quality preset
    RTQualityPreset GetQualityPreset() const { return m_QualityPreset; }
    
    /// Get quality settings
    const RTQualitySettings& GetQualitySettings() const { return m_QualitySettings; }
    
    /// Set custom quality settings
    void SetQualitySettings(const RTQualitySettings& settings);
    
    /// Set quality scaling mode
    void SetQualityScalingMode(QualityScalingMode mode) { m_ScalingMode = mode; }
    
    /// Get quality scaling mode
    QualityScalingMode GetQualityScalingMode() const { return m_ScalingMode; }
    
    /// Update adaptive quality based on frame time
    void UpdateAdaptiveQuality(float frameTimeMs, float targetFrameTimeMs = 16.67f);
    
    /// Register callback for feature changes
    void RegisterFeatureChangedCallback(const std::string& id, FeatureChangedCallback callback);
    
    /// Unregister feature changed callback
    void UnregisterFeatureChangedCallback(const std::string& id);
    
    /// Register callback for quality changes
    void RegisterQualityChangedCallback(const std::string& id, QualityChangedCallback callback);
    
    /// Unregister quality changed callback
    void UnregisterQualityChangedCallback(const std::string& id);
    
    /// Get hardware capabilities
    const RHI::RTCapabilities& GetCapabilities() const { return m_Capabilities; }
    
    /// Check if hardware supports a specific feature
    bool SupportsFeature(RTFeature feature) const;
    
    /// Get fallback recommendation for unsupported features
    std::string GetFallbackRecommendation(RTFeature feature) const;
    
    /// Get statistics
    struct Stats {
        uint64_t totalRaysTraced = 0;
        float avgRayTracingTimeMs = 0.0f;
        uint32_t currentQualityLevel = 0;
        bool isUsingFallback = false;
    };
    Stats GetStats() const { return m_Stats; }
    
    /// Reset statistics
    void ResetStats() { m_Stats = {}; }
    
    /// Get preset settings
    static RTQualitySettings GetPresetSettings(RTQualityPreset preset);
    
private:
    void ApplyQualityPreset(RTQualityPreset preset);
    void NotifyFeatureChanged(RTFeature feature, bool enabled);
    void NotifyQualityChanged(RTQualityPreset preset);
    void ValidateFeatures();
    
    RHI::RHIDeviceRT* m_Device = nullptr;
    RHI::RTCapabilities m_Capabilities;
    
    bool m_IsAvailable = false;
    RenderPath m_RenderPath = RenderPath::Rasterization;
    RTFeature m_EnabledFeatures = RTFeature::None;
    RTFeature m_SupportedFeatures = RTFeature::None;
    
    RTQualityPreset m_QualityPreset = RTQualityPreset::Medium;
    RTQualitySettings m_QualitySettings;
    QualityScalingMode m_ScalingMode = QualityScalingMode::Fixed;
    
    std::unordered_map<std::string, FeatureChangedCallback> m_FeatureCallbacks;
    std::unordered_map<std::string, QualityChangedCallback> m_QualityCallbacks;
    
    Stats m_Stats;
    
    // Adaptive quality state
    float m_FrameTimeHistory[32] = {};
    uint32_t m_FrameTimeHistoryIndex = 0;
    float m_AdaptiveScale = 1.0f;
};

/// Global ray tracing manager instance
RayTracingManager& GetRayTracingManager();

} // namespace AIEngine::Rendering
