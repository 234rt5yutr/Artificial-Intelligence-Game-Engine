#include "Core/Renderer/Upscaling/FrameGenerationController.h"

#include <algorithm>
#include <utility>

namespace Core::Renderer {
namespace {

[[nodiscard]] bool SupportsFrameGenerationWithUpscaler(const TemporalUpscalerBackend backend) {
    return backend == TemporalUpscalerBackend::DLSS || backend == TemporalUpscalerBackend::FSR2;
}

} // namespace

Result<FrameGenerationResult> FrameGenerationController::EnableFrameGeneration(const FrameGenerationConfig& config) {
    FrameGenerationResult result{};
    result.UpscalerBackend = config.ActiveUpscaler;
    result.AddedLatencyMs = std::max(config.EstimatedFrameGenerationLatencyMs, 0.0f);
    result.EffectiveLatencyMs = std::max(config.CurrentEndToEndLatencyMs + result.AddedLatencyMs, 0.0f);
    result.FrameIndex = m_CurrentFrameIndex;
    result.EventId = m_EventIdCounter++;

    if (!config.Enabled) {
        m_FrameGenerationActive = false;
        result.Active = false;
        result.FallbackUsed = true;
        result.FallbackReason = "FRAME_GENERATION_DISABLED_BY_REQUEST";
        m_LastResult = result;
        PushResultEvent(result);
        GetTemporalUpscalerManager().SetFrameGenerationActive(false);
        return Result<FrameGenerationResult>::Success(std::move(result));
    }

    if (!config.RuntimeFeatureEnabled || !config.BackendSupportsFrameGeneration) {
        result.Active = false;
        result.FallbackUsed = true;
        result.FallbackReason = "UPSCALER_BACKEND_UNAVAILABLE";
        m_LastResult = result;
        PushResultEvent(result);
        GetTemporalUpscalerManager().SetFrameGenerationActive(false);
        return Result<FrameGenerationResult>::Failure(result.FallbackReason);
    }

    if (!SupportsFrameGenerationWithUpscaler(config.ActiveUpscaler)) {
        result.Active = false;
        result.FallbackUsed = true;
        result.FallbackReason = "FRAME_GENERATION_REQUIRES_VENDOR_UPSCALER";
        m_LastResult = result;
        PushResultEvent(result);
        GetTemporalUpscalerManager().SetFrameGenerationActive(false);
        return Result<FrameGenerationResult>::Failure(result.FallbackReason);
    }

    if (config.FallbackOnInputSpike && config.InputLatencySpikeDetected) {
        m_FrameGenerationActive = false;
        result.Active = false;
        result.FallbackUsed = true;
        result.FallbackReason = "FRAME_GENERATION_INPUT_SPIKE_FALLBACK";
        m_LastResult = result;
        PushResultEvent(result);
        GetTemporalUpscalerManager().SetFrameGenerationActive(false);
        return Result<FrameGenerationResult>::Success(std::move(result));
    }

    if (result.AddedLatencyMs > std::max(config.MaxAddedLatencyMs, 0.0f)) {
        result.Active = false;
        result.FallbackUsed = true;
        result.FallbackReason = "FRAME_GENERATION_LATENCY_GUARDRAIL";
        m_LastResult = result;
        PushResultEvent(result);
        GetTemporalUpscalerManager().SetFrameGenerationActive(false);
        return Result<FrameGenerationResult>::Failure(result.FallbackReason);
    }

    m_FrameGenerationActive = true;
    result.Active = true;
    result.FallbackUsed = false;
    result.FallbackReason.clear();
    m_LastResult = result;
    PushResultEvent(result);
    GetTemporalUpscalerManager().SetFrameGenerationActive(true);
    return Result<FrameGenerationResult>::Success(std::move(result));
}

void FrameGenerationController::SetCurrentFrameIndex(const uint64_t frameIndex) {
    m_CurrentFrameIndex = frameIndex;
}

bool FrameGenerationController::IsFrameGenerationActive() const {
    return m_FrameGenerationActive;
}

FrameGenerationResult FrameGenerationController::GetLastResult() const {
    return m_LastResult;
}

std::deque<FrameGenerationResult> FrameGenerationController::GetRecentEvents() const {
    return m_RecentEvents;
}

void FrameGenerationController::PushResultEvent(const FrameGenerationResult& result) {
    m_RecentEvents.push_back(result);
    while (m_RecentEvents.size() > m_MaxRecentEvents) {
        m_RecentEvents.pop_front();
    }
}

FrameGenerationController& GetFrameGenerationController() {
    static FrameGenerationController controller;
    return controller;
}

} // namespace Core::Renderer

