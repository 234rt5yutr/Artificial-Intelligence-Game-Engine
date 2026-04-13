#pragma once

#include "Core/Renderer/GlobalIllumination.h"
#include "Core/Renderer/RTReflectionPass.h"
#include "Core/Renderer/RTShadowPass.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Core::Renderer {

enum class RTFeature : uint32_t {
    None = 0,
    Shadows = 1U << 0U,
    Reflections = 1U << 1U,
    GlobalIllumination = 1U << 2U,
    AmbientOcclusion = 1U << 3U,
    All = Shadows | Reflections | GlobalIllumination | AmbientOcclusion
};

inline RTFeature operator|(const RTFeature lhs, const RTFeature rhs) {
    return static_cast<RTFeature>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

inline RTFeature operator&(const RTFeature lhs, const RTFeature rhs) {
    return static_cast<RTFeature>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}

[[nodiscard]] inline bool HasFeature(const RTFeature flags, const RTFeature feature) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(feature)) != 0U;
}

enum class RenderPath : uint8_t {
    Rasterization = 0,
    Hybrid = 1,
    FullRayTracing = 2
};

enum class QualityScalingMode : uint8_t {
    Fixed = 0,
    Adaptive = 1,
    Performance = 2,
    Quality = 3
};

enum class RTQualityPreset : uint8_t {
    Off = 0,
    Low = 1,
    Medium = 2,
    High = 3,
    Ultra = 4,
    Custom = 5
};

struct RTQualitySettings {
    uint32_t shadowSamplesPerPixel = 1;
    float shadowSoftness = 0.1f;
    bool shadowDenoising = true;
    uint32_t reflectionSamplesPerPixel = 1;
    uint32_t reflectionBounces = 1;
    float reflectionResolutionScale = 1.0f;
    bool reflectionDenoising = true;
    uint32_t giSamplesPerPixel = 1;
    uint32_t giBounces = 1;
    float giIntensity = 1.0f;
    bool giDenoising = true;
    float resolutionScale = 1.0f;
    bool temporalAccumulation = true;
};

struct RTCapabilityInfo {
    bool rayTracingPipelineSupported = false;
    bool rayQuerySupported = false;
    bool inlineRayShadowsSupported = false;
    uint32_t maxRecursionDepth = 0;
};

enum class HybridLightingStage : uint8_t {
    RTShadows = 0,
    Reflections = 1,
    GIComplexity = 2
};

struct HybridLightingQualityState {
    RTShadowQuality shadowQuality = RTShadowQuality::Medium;
    RTReflectionQuality reflectionQuality = RTReflectionQuality::Medium;
    GIQuality giQuality = GIQuality::Medium;
    uint32_t giComplexity = 1;
};

struct HybridLightingTransitionEvent {
    HybridLightingStage stage = HybridLightingStage::RTShadows;
    std::string fromState;
    std::string toState;
    float frameTimeMs = 0.0f;
    float frameBudgetMs = 0.0f;
};

class HybridLightingPolicyGovernor {
public:
    void SetTargetState(const HybridLightingQualityState& state);
    const HybridLightingQualityState& GetTargetState() const;
    const HybridLightingQualityState& GetActiveState() const;

    void UpdateFrameBudget(float frameTimeMs, float frameBudgetMs);
    std::vector<HybridLightingTransitionEvent> ConsumeTransitionEvents();

private:
    static float EstimateCostMs(const HybridLightingQualityState& state);
    static bool TryDowngrade(HybridLightingQualityState& state, HybridLightingTransitionEvent& eventOut);
    static std::string ToDebugString(RTShadowQuality value);
    static std::string ToDebugString(RTReflectionQuality value);
    static std::string ToDebugString(GIQuality value, uint32_t complexity);

    HybridLightingQualityState m_TargetState{};
    HybridLightingQualityState m_ActiveState{};
    std::vector<HybridLightingTransitionEvent> m_TransitionEvents;
};

class RayTracingManager {
public:
    using FeatureChangedCallback = std::function<void(RTFeature, bool)>;
    using QualityChangedCallback = std::function<void(RTQualityPreset)>;

    bool Initialize(const RTCapabilityInfo& capabilities);
    void Shutdown();

    bool IsRayTracingAvailable() const;
    RenderPath GetRenderPath() const;
    void SetRenderPath(RenderPath path);

    bool IsFeatureEnabled(RTFeature feature) const;
    void SetFeatureEnabled(RTFeature feature, bool enabled);
    void ToggleFeature(RTFeature feature);
    RTFeature GetEnabledFeatures() const;

    void SetQualityPreset(RTQualityPreset preset);
    RTQualityPreset GetQualityPreset() const;

    const RTQualitySettings& GetQualitySettings() const;
    void SetQualitySettings(const RTQualitySettings& settings);

    void SetQualityScalingMode(QualityScalingMode mode);
    QualityScalingMode GetQualityScalingMode() const;

    void UpdateAdaptiveQuality(float frameTimeMs, float targetFrameTimeMs = 16.67f);

    void RegisterFeatureChangedCallback(const std::string& id, FeatureChangedCallback callback);
    void UnregisterFeatureChangedCallback(const std::string& id);
    void RegisterQualityChangedCallback(const std::string& id, QualityChangedCallback callback);
    void UnregisterQualityChangedCallback(const std::string& id);

    const RTCapabilityInfo& GetCapabilities() const;
    bool SupportsFeature(RTFeature feature) const;
    std::string GetFallbackRecommendation(RTFeature feature) const;

    struct Stats {
        uint64_t totalRaysTraced = 0;
        float avgRayTracingTimeMs = 0.0f;
        uint32_t currentQualityLevel = 0;
        bool isUsingFallback = false;
    };

    Stats GetStats() const;
    void ResetStats();

    static RTQualitySettings GetPresetSettings(RTQualityPreset preset);

    HybridLightingPolicyGovernor& GetHybridLightingGovernor();

private:
    void ApplyQualityPreset(RTQualityPreset preset);
    void NotifyFeatureChanged(RTFeature feature, bool enabled) const;
    void NotifyQualityChanged(RTQualityPreset preset) const;
    void ValidateFeatures();

    bool m_IsAvailable = false;
    RenderPath m_RenderPath = RenderPath::Rasterization;
    RTFeature m_EnabledFeatures = RTFeature::None;
    RTFeature m_SupportedFeatures = RTFeature::None;
    RTCapabilityInfo m_Capabilities{};
    RTQualityPreset m_QualityPreset = RTQualityPreset::Medium;
    RTQualitySettings m_QualitySettings{};
    QualityScalingMode m_ScalingMode = QualityScalingMode::Fixed;
    std::unordered_map<std::string, FeatureChangedCallback> m_FeatureCallbacks;
    std::unordered_map<std::string, QualityChangedCallback> m_QualityCallbacks;
    Stats m_Stats{};
    float m_FrameTimeHistory[32]{};
    uint32_t m_FrameTimeHistoryIndex = 0;
    float m_AdaptiveScale = 1.0f;
    HybridLightingPolicyGovernor m_HybridGovernor{};
};

RayTracingManager& GetRayTracingManager();

} // namespace Core::Renderer

namespace AIEngine::Rendering {
using Core::Renderer::GetRayTracingManager;
using Core::Renderer::HasFeature;
using Core::Renderer::HybridLightingPolicyGovernor;
using Core::Renderer::HybridLightingQualityState;
using Core::Renderer::HybridLightingStage;
using Core::Renderer::HybridLightingTransitionEvent;
using Core::Renderer::QualityScalingMode;
using Core::Renderer::RTCapabilityInfo;
using Core::Renderer::RTFeature;
using Core::Renderer::RTQualityPreset;
using Core::Renderer::RTQualitySettings;
using Core::Renderer::RayTracingManager;
using Core::Renderer::RenderPath;
}
