#include "Core/Renderer/RTShadowPass.h"

#include <algorithm>

namespace Core::Renderer {
namespace {

[[nodiscard]] RTShadowQuality ClampShadowQuality(const RTShadowQuality quality) {
    switch (quality) {
    case RTShadowQuality::Off:
    case RTShadowQuality::Low:
    case RTShadowQuality::Medium:
    case RTShadowQuality::High:
    case RTShadowQuality::Ultra:
        return quality;
    default:
        return RTShadowQuality::Medium;
    }
}

[[nodiscard]] float ShadowCostMs(const RTShadowQuality quality) {
    switch (quality) {
    case RTShadowQuality::Off:
        return 0.0f;
    case RTShadowQuality::Low:
        return 0.9f;
    case RTShadowQuality::Medium:
        return 1.6f;
    case RTShadowQuality::High:
        return 2.5f;
    case RTShadowQuality::Ultra:
        return 3.8f;
    default:
        return 1.6f;
    }
}

} // namespace

Result<ShadowTraceResult> TraceHardwareShadows(const ShadowTraceRequest& request) {
    ShadowTraceResult result{};
    result.qualityUsed = ClampShadowQuality(request.config.globalQuality);

    if (!request.hardwareRayTracingSupported || result.qualityUsed == RTShadowQuality::Off) {
        result.qualityUsed = RTShadowQuality::Off;
        result.usedFallbackShadowMaps = true;
        result.diagnostics = "SHADOW_RT_UNAVAILABLE";
        return Result<ShadowTraceResult>::Success(result);
    }

    result.usedFallbackShadowMaps = false;
    if (request.currentFrameTimeMs > request.frameBudgetMs) {
        result.qualityUsed = RTShadowQuality::Low;
        result.usedFallbackShadowMaps = true;
        result.diagnostics = "SHADOW_BUDGET_DEGRADED";
    }

    result.lightsRendered = std::min(request.lightCount, request.config.maxLightsWithRTShadows);
    result.estimatedCostMs = ShadowCostMs(result.qualityUsed) * static_cast<float>(result.lightsRendered);
    result.raysTraced = result.lightsRendered * RTShadowPass::GetSamplesForQuality(result.qualityUsed) * 1024U;

    return Result<ShadowTraceResult>::Success(result);
}

void RTShadowPass::SetConfig(const RTShadowConfig& config) {
    m_Config = config;
    m_Config.globalQuality = ClampShadowQuality(m_Config.globalQuality);
    m_Config.maxLightsWithRTShadows = std::max(1U, m_Config.maxLightsWithRTShadows);
    m_Config.resolutionScale = std::clamp(m_Config.resolutionScale, 0.25f, 1.0f);
}

const RTShadowConfig& RTShadowPass::GetConfig() const {
    return m_Config;
}

void RTShadowPass::SetQuality(const RTShadowQuality quality) {
    m_Config.globalQuality = ClampShadowQuality(quality);
}

RTShadowQuality RTShadowPass::GetQuality() const {
    return m_Config.globalQuality;
}

void RTShadowPass::SetLightSettings(const uint32_t lightIndex, const RTShadowLightSettings& settings) {
    if (lightIndex >= m_LightSettings.size()) {
        m_LightSettings.resize(lightIndex + 1U);
    }
    m_LightSettings[lightIndex] = settings;
}

const RTShadowLightSettings& RTShadowPass::GetLightSettings(const uint32_t lightIndex) const {
    static const RTShadowLightSettings defaultSettings{};
    if (lightIndex >= m_LightSettings.size()) {
        return defaultSettings;
    }
    return m_LightSettings[lightIndex];
}

uint32_t RTShadowPass::GetSamplesForQuality(const RTShadowQuality quality) {
    switch (quality) {
    case RTShadowQuality::Off:
        return 0U;
    case RTShadowQuality::Low:
    case RTShadowQuality::Medium:
        return 1U;
    case RTShadowQuality::High:
        return 4U;
    case RTShadowQuality::Ultra:
        return 8U;
    default:
        return 1U;
    }
}

} // namespace Core::Renderer
