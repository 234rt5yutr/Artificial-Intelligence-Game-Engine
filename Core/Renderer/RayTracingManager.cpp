#include "Core/Renderer/RayTracingManager.h"

#include <algorithm>
#include <numeric>

namespace Core::Renderer {
namespace {

[[nodiscard]] RTShadowQuality DowngradeShadowQuality(const RTShadowQuality quality) {
    switch (quality) {
    case RTShadowQuality::Ultra:
        return RTShadowQuality::High;
    case RTShadowQuality::High:
        return RTShadowQuality::Medium;
    case RTShadowQuality::Medium:
        return RTShadowQuality::Low;
    case RTShadowQuality::Low:
        return RTShadowQuality::Off;
    case RTShadowQuality::Off:
    default:
        return RTShadowQuality::Off;
    }
}

[[nodiscard]] RTReflectionQuality DowngradeReflectionQuality(const RTReflectionQuality quality) {
    switch (quality) {
    case RTReflectionQuality::Ultra:
        return RTReflectionQuality::High;
    case RTReflectionQuality::High:
        return RTReflectionQuality::Medium;
    case RTReflectionQuality::Medium:
        return RTReflectionQuality::Low;
    case RTReflectionQuality::Low:
        return RTReflectionQuality::Off;
    case RTReflectionQuality::Off:
    default:
        return RTReflectionQuality::Off;
    }
}

[[nodiscard]] GIQuality DowngradeGIQuality(const GIQuality quality) {
    switch (quality) {
    case GIQuality::Ultra:
        return GIQuality::High;
    case GIQuality::High:
        return GIQuality::Medium;
    case GIQuality::Medium:
        return GIQuality::Low;
    case GIQuality::Low:
        return GIQuality::Off;
    case GIQuality::Off:
    default:
        return GIQuality::Off;
    }
}

[[nodiscard]] float ShadowCost(const RTShadowQuality quality) {
    switch (quality) {
    case RTShadowQuality::Off:
        return 0.0f;
    case RTShadowQuality::Low:
        return 0.6f;
    case RTShadowQuality::Medium:
        return 1.2f;
    case RTShadowQuality::High:
        return 2.0f;
    case RTShadowQuality::Ultra:
        return 3.0f;
    default:
        return 1.2f;
    }
}

[[nodiscard]] float ReflectionCost(const RTReflectionQuality quality) {
    switch (quality) {
    case RTReflectionQuality::Off:
        return 0.0f;
    case RTReflectionQuality::Low:
        return 0.8f;
    case RTReflectionQuality::Medium:
        return 1.5f;
    case RTReflectionQuality::High:
        return 2.3f;
    case RTReflectionQuality::Ultra:
        return 3.2f;
    default:
        return 1.5f;
    }
}

[[nodiscard]] float GICost(const GIQuality quality, const uint32_t complexity) {
    float qualityScale = 0.0f;
    switch (quality) {
    case GIQuality::Off:
        qualityScale = 0.0f;
        break;
    case GIQuality::Low:
        qualityScale = 0.5f;
        break;
    case GIQuality::Medium:
        qualityScale = 1.0f;
        break;
    case GIQuality::High:
        qualityScale = 1.6f;
        break;
    case GIQuality::Ultra:
        qualityScale = 2.3f;
        break;
    default:
        qualityScale = 1.0f;
        break;
    }
    return qualityScale * static_cast<float>(complexity);
}

} // namespace

void HybridLightingPolicyGovernor::SetTargetState(const HybridLightingQualityState& state) {
    m_TargetState = state;
}

const HybridLightingQualityState& HybridLightingPolicyGovernor::GetTargetState() const {
    return m_TargetState;
}

const HybridLightingQualityState& HybridLightingPolicyGovernor::GetActiveState() const {
    return m_ActiveState;
}

void HybridLightingPolicyGovernor::UpdateFrameBudget(const float frameTimeMs, const float frameBudgetMs) {
    m_ActiveState = m_TargetState;
    if (frameBudgetMs <= 0.0f) {
        return;
    }

    while (frameTimeMs > frameBudgetMs && EstimateCostMs(m_ActiveState) > frameBudgetMs) {
        HybridLightingTransitionEvent event{};
        event.frameTimeMs = frameTimeMs;
        event.frameBudgetMs = frameBudgetMs;
        if (!TryDowngrade(m_ActiveState, event)) {
            break;
        }
        m_TransitionEvents.push_back(std::move(event));
    }
}

std::vector<HybridLightingTransitionEvent> HybridLightingPolicyGovernor::ConsumeTransitionEvents() {
    std::vector<HybridLightingTransitionEvent> events = std::move(m_TransitionEvents);
    m_TransitionEvents.clear();
    return events;
}

float HybridLightingPolicyGovernor::EstimateCostMs(const HybridLightingQualityState& state) {
    return ShadowCost(state.shadowQuality) + ReflectionCost(state.reflectionQuality) + GICost(state.giQuality, state.giComplexity);
}

bool HybridLightingPolicyGovernor::TryDowngrade(HybridLightingQualityState& state, HybridLightingTransitionEvent& eventOut) {
    const RTShadowQuality nextShadow = DowngradeShadowQuality(state.shadowQuality);
    if (nextShadow != state.shadowQuality) {
        eventOut.stage = HybridLightingStage::RTShadows;
        eventOut.fromState = ToDebugString(state.shadowQuality);
        state.shadowQuality = nextShadow;
        eventOut.toState = ToDebugString(state.shadowQuality);
        return true;
    }

    const RTReflectionQuality nextReflection = DowngradeReflectionQuality(state.reflectionQuality);
    if (nextReflection != state.reflectionQuality) {
        eventOut.stage = HybridLightingStage::Reflections;
        eventOut.fromState = ToDebugString(state.reflectionQuality);
        state.reflectionQuality = nextReflection;
        eventOut.toState = ToDebugString(state.reflectionQuality);
        return true;
    }

    if (state.giComplexity > 1U) {
        eventOut.stage = HybridLightingStage::GIComplexity;
        eventOut.fromState = ToDebugString(state.giQuality, state.giComplexity);
        --state.giComplexity;
        eventOut.toState = ToDebugString(state.giQuality, state.giComplexity);
        return true;
    }

    const GIQuality nextGI = DowngradeGIQuality(state.giQuality);
    if (nextGI != state.giQuality) {
        eventOut.stage = HybridLightingStage::GIComplexity;
        eventOut.fromState = ToDebugString(state.giQuality, state.giComplexity);
        state.giQuality = nextGI;
        state.giComplexity = std::min(state.giComplexity, 1U);
        eventOut.toState = ToDebugString(state.giQuality, state.giComplexity);
        return true;
    }

    return false;
}

std::string HybridLightingPolicyGovernor::ToDebugString(const RTShadowQuality value) {
    switch (value) {
    case RTShadowQuality::Off:
        return "off";
    case RTShadowQuality::Low:
        return "low";
    case RTShadowQuality::Medium:
        return "medium";
    case RTShadowQuality::High:
        return "high";
    case RTShadowQuality::Ultra:
        return "ultra";
    default:
        return "unknown";
    }
}

std::string HybridLightingPolicyGovernor::ToDebugString(const RTReflectionQuality value) {
    switch (value) {
    case RTReflectionQuality::Off:
        return "off";
    case RTReflectionQuality::Low:
        return "low";
    case RTReflectionQuality::Medium:
        return "medium";
    case RTReflectionQuality::High:
        return "high";
    case RTReflectionQuality::Ultra:
        return "ultra";
    default:
        return "unknown";
    }
}

std::string HybridLightingPolicyGovernor::ToDebugString(const GIQuality value, const uint32_t complexity) {
    std::string quality;
    switch (value) {
    case GIQuality::Off:
        quality = "off";
        break;
    case GIQuality::Low:
        quality = "low";
        break;
    case GIQuality::Medium:
        quality = "medium";
        break;
    case GIQuality::High:
        quality = "high";
        break;
    case GIQuality::Ultra:
        quality = "ultra";
        break;
    default:
        quality = "unknown";
        break;
    }
    return quality + ":complexity=" + std::to_string(complexity);
}

bool RayTracingManager::Initialize(const RTCapabilityInfo& capabilities) {
    m_Capabilities = capabilities;
    m_IsAvailable = capabilities.rayTracingPipelineSupported || capabilities.rayQuerySupported;
    m_SupportedFeatures = m_IsAvailable ? RTFeature::All : RTFeature::None;
    m_EnabledFeatures = m_IsAvailable ? RTFeature::Shadows : RTFeature::None;
    ApplyQualityPreset(m_QualityPreset);
    return true;
}

void RayTracingManager::Shutdown() {
    m_IsAvailable = false;
    m_SupportedFeatures = RTFeature::None;
    m_EnabledFeatures = RTFeature::None;
    m_FeatureCallbacks.clear();
    m_QualityCallbacks.clear();
    m_Stats = {};
}

bool RayTracingManager::IsRayTracingAvailable() const {
    return m_IsAvailable;
}

RenderPath RayTracingManager::GetRenderPath() const {
    return m_RenderPath;
}

void RayTracingManager::SetRenderPath(const RenderPath path) {
    m_RenderPath = path;
}

bool RayTracingManager::IsFeatureEnabled(const RTFeature feature) const {
    return HasFeature(m_EnabledFeatures, feature);
}

void RayTracingManager::SetFeatureEnabled(const RTFeature feature, const bool enabled) {
    if (enabled) {
        m_EnabledFeatures = m_EnabledFeatures | feature;
    } else {
        m_EnabledFeatures = static_cast<RTFeature>(static_cast<uint32_t>(m_EnabledFeatures) &
            ~static_cast<uint32_t>(feature));
    }
    ValidateFeatures();
    NotifyFeatureChanged(feature, enabled);
}

void RayTracingManager::ToggleFeature(const RTFeature feature) {
    SetFeatureEnabled(feature, !IsFeatureEnabled(feature));
}

RTFeature RayTracingManager::GetEnabledFeatures() const {
    return m_EnabledFeatures;
}

void RayTracingManager::SetQualityPreset(const RTQualityPreset preset) {
    m_QualityPreset = preset;
    ApplyQualityPreset(preset);
    NotifyQualityChanged(preset);
}

RTQualityPreset RayTracingManager::GetQualityPreset() const {
    return m_QualityPreset;
}

const RTQualitySettings& RayTracingManager::GetQualitySettings() const {
    return m_QualitySettings;
}

void RayTracingManager::SetQualitySettings(const RTQualitySettings& settings) {
    m_QualitySettings = settings;
    m_QualityPreset = RTQualityPreset::Custom;
}

void RayTracingManager::SetQualityScalingMode(const QualityScalingMode mode) {
    m_ScalingMode = mode;
}

QualityScalingMode RayTracingManager::GetQualityScalingMode() const {
    return m_ScalingMode;
}

void RayTracingManager::UpdateAdaptiveQuality(const float frameTimeMs, const float targetFrameTimeMs) {
    m_FrameTimeHistory[m_FrameTimeHistoryIndex % 32U] = frameTimeMs;
    ++m_FrameTimeHistoryIndex;

    const float averageFrameTime = std::accumulate(std::begin(m_FrameTimeHistory), std::end(m_FrameTimeHistory), 0.0f) / 32.0f;
    if (m_ScalingMode == QualityScalingMode::Adaptive && averageFrameTime > targetFrameTimeMs) {
        m_AdaptiveScale = std::max(0.5f, m_AdaptiveScale - 0.05f);
    } else if (m_ScalingMode == QualityScalingMode::Adaptive && averageFrameTime <= targetFrameTimeMs) {
        m_AdaptiveScale = std::min(1.0f, m_AdaptiveScale + 0.02f);
    }

    HybridLightingQualityState state{};
    state.shadowQuality = m_AdaptiveScale < 0.75f ? RTShadowQuality::Low : RTShadowQuality::Medium;
    state.reflectionQuality = m_AdaptiveScale < 0.75f ? RTReflectionQuality::Low : RTReflectionQuality::Medium;
    state.giQuality = m_AdaptiveScale < 0.75f ? GIQuality::Low : GIQuality::Medium;
    state.giComplexity = m_AdaptiveScale < 0.6f ? 1U : 2U;
    m_HybridGovernor.SetTargetState(state);
    m_HybridGovernor.UpdateFrameBudget(frameTimeMs, targetFrameTimeMs);
}

void RayTracingManager::RegisterFeatureChangedCallback(const std::string& id, FeatureChangedCallback callback) {
    m_FeatureCallbacks[id] = std::move(callback);
}

void RayTracingManager::UnregisterFeatureChangedCallback(const std::string& id) {
    m_FeatureCallbacks.erase(id);
}

void RayTracingManager::RegisterQualityChangedCallback(const std::string& id, QualityChangedCallback callback) {
    m_QualityCallbacks[id] = std::move(callback);
}

void RayTracingManager::UnregisterQualityChangedCallback(const std::string& id) {
    m_QualityCallbacks.erase(id);
}

const RTCapabilityInfo& RayTracingManager::GetCapabilities() const {
    return m_Capabilities;
}

bool RayTracingManager::SupportsFeature(const RTFeature feature) const {
    return HasFeature(m_SupportedFeatures, feature);
}

std::string RayTracingManager::GetFallbackRecommendation(const RTFeature feature) const {
    switch (feature) {
    case RTFeature::Shadows:
        return "Use cascaded shadow maps";
    case RTFeature::Reflections:
        return "Use SSR + probe blend";
    case RTFeature::GlobalIllumination:
        return "Use SSGI or irradiance probes";
    case RTFeature::AmbientOcclusion:
        return "Use SSAO";
    case RTFeature::None:
    case RTFeature::All:
    default:
        return "Use raster fallback";
    }
}

RayTracingManager::Stats RayTracingManager::GetStats() const {
    return m_Stats;
}

void RayTracingManager::ResetStats() {
    m_Stats = {};
}

RTQualitySettings RayTracingManager::GetPresetSettings(const RTQualityPreset preset) {
    RTQualitySettings settings{};
    switch (preset) {
    case RTQualityPreset::Off:
        settings.shadowSamplesPerPixel = 0U;
        settings.reflectionSamplesPerPixel = 0U;
        settings.giSamplesPerPixel = 0U;
        break;
    case RTQualityPreset::Low:
        settings.shadowSamplesPerPixel = 1U;
        settings.reflectionSamplesPerPixel = 1U;
        settings.giSamplesPerPixel = 1U;
        settings.giBounces = 1U;
        break;
    case RTQualityPreset::Medium:
        settings.shadowSamplesPerPixel = 1U;
        settings.reflectionSamplesPerPixel = 1U;
        settings.giSamplesPerPixel = 1U;
        settings.giBounces = 1U;
        break;
    case RTQualityPreset::High:
        settings.shadowSamplesPerPixel = 2U;
        settings.reflectionSamplesPerPixel = 2U;
        settings.giSamplesPerPixel = 2U;
        settings.giBounces = 2U;
        break;
    case RTQualityPreset::Ultra:
        settings.shadowSamplesPerPixel = 4U;
        settings.reflectionSamplesPerPixel = 4U;
        settings.giSamplesPerPixel = 3U;
        settings.giBounces = 3U;
        break;
    case RTQualityPreset::Custom:
    default:
        break;
    }
    return settings;
}

HybridLightingPolicyGovernor& RayTracingManager::GetHybridLightingGovernor() {
    return m_HybridGovernor;
}

void RayTracingManager::ApplyQualityPreset(const RTQualityPreset preset) {
    if (preset != RTQualityPreset::Custom) {
        m_QualitySettings = GetPresetSettings(preset);
    }
    m_Stats.currentQualityLevel = static_cast<uint32_t>(preset);
}

void RayTracingManager::NotifyFeatureChanged(const RTFeature feature, const bool enabled) const {
    for (const auto& [_, callback] : m_FeatureCallbacks) {
        if (callback) {
            callback(feature, enabled);
        }
    }
}

void RayTracingManager::NotifyQualityChanged(const RTQualityPreset preset) const {
    for (const auto& [_, callback] : m_QualityCallbacks) {
        if (callback) {
            callback(preset);
        }
    }
}

void RayTracingManager::ValidateFeatures() {
    m_EnabledFeatures = static_cast<RTFeature>(
        static_cast<uint32_t>(m_EnabledFeatures) & static_cast<uint32_t>(m_SupportedFeatures));
}

RayTracingManager& GetRayTracingManager() {
    static RayTracingManager manager{};
    return manager;
}

} // namespace Core::Renderer
