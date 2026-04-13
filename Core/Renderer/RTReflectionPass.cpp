#include "Core/Renderer/RTReflectionPass.h"

#include <algorithm>

namespace Core::Renderer {
namespace {

[[nodiscard]] RTReflectionQuality ClampReflectionQuality(const RTReflectionQuality quality) {
    switch (quality) {
    case RTReflectionQuality::Off:
    case RTReflectionQuality::Low:
    case RTReflectionQuality::Medium:
    case RTReflectionQuality::High:
    case RTReflectionQuality::Ultra:
        return quality;
    default:
        return RTReflectionQuality::Medium;
    }
}

[[nodiscard]] float ReflectionCostMs(const RTReflectionQuality quality) {
    switch (quality) {
    case RTReflectionQuality::Off:
        return 0.0f;
    case RTReflectionQuality::Low:
        return 0.8f;
    case RTReflectionQuality::Medium:
        return 1.6f;
    case RTReflectionQuality::High:
        return 2.4f;
    case RTReflectionQuality::Ultra:
        return 3.4f;
    default:
        return 1.6f;
    }
}

} // namespace

Result<ReflectionTraceResult> TraceHardwareReflections(const ReflectionTraceRequest& request) {
    ReflectionTraceResult result{};

    result.qualityUsed = ClampReflectionQuality(request.config.quality);
    result.bouncesUsed = std::max(1U, request.config.maxBounces);
    result.fallbackToScreenSpace = false;

    if (!request.hardwareRayTracingSupported || result.qualityUsed == RTReflectionQuality::Off) {
        result.qualityUsed = RTReflectionQuality::Off;
        result.bouncesUsed = 0U;
        result.fallbackToScreenSpace = true;
        result.diagnostics = "REFLECTION_RT_UNAVAILABLE";
        return Result<ReflectionTraceResult>::Success(result);
    }

    if (request.currentFrameTimeMs > request.frameBudgetMs) {
        result.qualityUsed = RTReflectionQuality::Low;
        result.bouncesUsed = 1U;
        result.diagnostics = "REFLECTION_BUDGET_DEGRADED";
    }

    const float halfResScale = request.halfResolutionTrace ? 0.5f : 1.0f;
    result.estimatedCostMs = ReflectionCostMs(result.qualityUsed) * static_cast<float>(result.bouncesUsed) * halfResScale;
    result.raysTraced = static_cast<uint32_t>(50000.0f * static_cast<float>(result.bouncesUsed) * halfResScale);

    return Result<ReflectionTraceResult>::Success(result);
}

void RTReflectionPass::SetConfig(const RTReflectionConfig& config) {
    m_Config = config;
    m_Config.quality = ClampReflectionQuality(m_Config.quality);
    m_Config.maxBounces = std::max(1U, m_Config.maxBounces);
}

const RTReflectionConfig& RTReflectionPass::GetConfig() const {
    return m_Config;
}

void RTReflectionPass::SetQuality(const RTReflectionQuality quality) {
    m_Config.quality = ClampReflectionQuality(quality);
}

RTReflectionQuality RTReflectionPass::GetQuality() const {
    return m_Config.quality;
}

void RTReflectionPass::SetMaterialOverride(const uint32_t materialId, const MaterialReflectionOverride& overrideValue) {
    m_MaterialOverrides[materialId] = overrideValue;
}

void RTReflectionPass::ClearMaterialOverride(const uint32_t materialId) {
    m_MaterialOverrides.erase(materialId);
}

void RTReflectionPass::ClearAllMaterialOverrides() {
    m_MaterialOverrides.clear();
}

uint32_t RTReflectionPass::GetSamplesForQuality(const RTReflectionQuality quality) {
    switch (quality) {
    case RTReflectionQuality::Off:
        return 0U;
    case RTReflectionQuality::Low:
        return 1U;
    case RTReflectionQuality::Medium:
        return 1U;
    case RTReflectionQuality::High:
        return 2U;
    case RTReflectionQuality::Ultra:
        return 4U;
    default:
        return 1U;
    }
}

uint32_t RTReflectionPass::GetBouncesForQuality(const RTReflectionQuality quality) {
    switch (quality) {
    case RTReflectionQuality::Off:
        return 0U;
    case RTReflectionQuality::Low:
    case RTReflectionQuality::Medium:
        return 1U;
    case RTReflectionQuality::High:
        return 2U;
    case RTReflectionQuality::Ultra:
        return 3U;
    default:
        return 1U;
    }
}

float RTReflectionPass::GetResolutionScaleForQuality(const RTReflectionQuality quality) {
    switch (quality) {
    case RTReflectionQuality::Off:
        return 0.0f;
    case RTReflectionQuality::Low:
        return 0.5f;
    case RTReflectionQuality::Medium:
        return 0.75f;
    case RTReflectionQuality::High:
    case RTReflectionQuality::Ultra:
        return 1.0f;
    default:
        return 0.75f;
    }
}

} // namespace Core::Renderer
