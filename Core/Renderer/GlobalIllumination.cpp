#include "Core/Renderer/GlobalIllumination.h"

#include <algorithm>

namespace Core::Renderer {
namespace {

constexpr float kGiQualityCostMs[] = {0.0f, 0.75f, 1.4f, 2.2f, 3.1f};

[[nodiscard]] GIQuality ClampGIQuality(const GIQuality quality) {
    switch (quality) {
    case GIQuality::Off:
    case GIQuality::Low:
    case GIQuality::Medium:
    case GIQuality::High:
    case GIQuality::Ultra:
        return quality;
    default:
        return GIQuality::Medium;
    }
}

[[nodiscard]] uint32_t QualityToBounceCount(const GIQuality quality) {
    switch (quality) {
    case GIQuality::Off:
        return 0U;
    case GIQuality::Low:
    case GIQuality::Medium:
        return 1U;
    case GIQuality::High:
        return 2U;
    case GIQuality::Ultra:
        return 3U;
    default:
        return 1U;
    }
}

} // namespace

Result<DynamicGIResult> ComputeDynamicGlobalIllumination(const DynamicGIRequest& request) {
    DynamicGIResult result{};
    result.qualityUsed = ClampGIQuality(request.config.quality);
    result.bounceCount = std::max(1U, request.config.maxBounces);
    result.techniqueUsed = request.config.technique;

    if (result.qualityUsed == GIQuality::Off || request.config.technique == GITechnique::None) {
        result.qualityUsed = GIQuality::Off;
        result.techniqueUsed = GITechnique::None;
        result.bounceCount = 0U;
        result.estimatedCostMs = 0.0f;
        return Result<DynamicGIResult>::Success(result);
    }

    if (!request.hardwareRayTracingSupported && request.config.technique == GITechnique::RayTraced) {
        result.techniqueUsed = GITechnique::ScreenSpace;
        result.fallbackUsed = true;
        result.fallbackReason = "GI_RT_UNAVAILABLE_FALLBACK_TO_SSGI";
    }

    const float qualityCost = kGiQualityCostMs[static_cast<size_t>(result.qualityUsed)];
    result.estimatedCostMs = qualityCost * static_cast<float>(result.bounceCount);

    if (!request.preferQuality && request.currentFrameTimeMs > request.frameBudgetMs) {
        result.qualityUsed = result.qualityUsed == GIQuality::Low ? GIQuality::Off : GIQuality::Low;
        result.bounceCount = QualityToBounceCount(result.qualityUsed);
        result.estimatedCostMs = kGiQualityCostMs[static_cast<size_t>(result.qualityUsed)] * static_cast<float>(result.bounceCount);
        result.fallbackUsed = true;
        if (result.fallbackReason.empty()) {
            result.fallbackReason = "GI_BUDGET_PRESSURE";
        }
    }

    return Result<DynamicGIResult>::Success(result);
}

void GlobalIllumination::SetConfig(const GIConfig& config) {
    m_Config = config;
    m_Config.quality = ClampGIQuality(m_Config.quality);
}

const GIConfig& GlobalIllumination::GetConfig() const {
    return m_Config;
}

void GlobalIllumination::SetTechnique(const GITechnique technique) {
    m_Config.technique = technique;
}

GITechnique GlobalIllumination::GetTechnique() const {
    return m_Config.technique;
}

void GlobalIllumination::SetQuality(const GIQuality quality) {
    m_Config.quality = ClampGIQuality(quality);
}

GIQuality GlobalIllumination::GetQuality() const {
    return m_Config.quality;
}

uint32_t GlobalIllumination::AddIrradianceVolume(
    const Math::Vec3& minBounds,
    const Math::Vec3& maxBounds,
    const Math::IVec3& resolution) {
    m_Volumes.push_back(IrradianceVolume{minBounds, maxBounds, resolution});
    return static_cast<uint32_t>(m_Volumes.size() - 1U);
}

void GlobalIllumination::StartAsyncBake(entt::registry& /*registry*/, BakeProgressCallback callback) {
    m_AsyncBakeInProgress = true;
    m_AsyncBakeProgress = 1.0f;
    m_AsyncBakeInProgress = false;
    if (callback) {
        callback(m_AsyncBakeProgress, "completed");
    }
}

bool GlobalIllumination::IsAsyncBakeInProgress() const {
    return m_AsyncBakeInProgress;
}

} // namespace Core::Renderer
