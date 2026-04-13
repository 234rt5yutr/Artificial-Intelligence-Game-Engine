#include "Core/Renderer/Upscaling/TemporalUpscalerManager.h"

#include <algorithm>
#include <utility>

namespace Core::Renderer {
namespace {

constexpr float kFsr2QualityScale = 1.0f;
constexpr float kFsr2BalancedScale = 0.77f;
constexpr float kFsr2PerformanceScale = 0.67f;
constexpr float kFsr2UltraPerformanceScale = 0.5f;

[[nodiscard]] bool IsVendorBackend(const TemporalUpscalerBackend backend) {
    return backend == TemporalUpscalerBackend::FSR2 ||
           backend == TemporalUpscalerBackend::DLSS ||
           backend == TemporalUpscalerBackend::XeSS;
}

[[nodiscard]] float QualityToScale(const TemporalUpscalerQualityPreset quality) {
    switch (quality) {
    case TemporalUpscalerQualityPreset::Quality:
        return kFsr2QualityScale;
    case TemporalUpscalerQualityPreset::Balanced:
        return kFsr2BalancedScale;
    case TemporalUpscalerQualityPreset::Performance:
        return kFsr2PerformanceScale;
    case TemporalUpscalerQualityPreset::UltraPerformance:
        return kFsr2UltraPerformanceScale;
    case TemporalUpscalerQualityPreset::UltraQuality:
        return 1.2f;
    default:
        return kFsr2QualityScale;
    }
}

} // namespace

Result<void> TemporalUpscalerManager::SetTemporalUpscalerFSR2(const TemporalUpscalerFSR2Config& config) {
    const TemporalUpscalerQualityPreset quality = config.Quality;
    Result<void> transitionResult = ApplyTransition(
        TemporalUpscalerBackend::FSR2,
        quality,
        config.BackendAvailable,
        config.RuntimeFeatureEnabled,
        config.Enabled,
        "SetTemporalUpscalerFSR2");

    if (!transitionResult.Ok) {
        return transitionResult;
    }

    m_State.Sharpness = std::clamp(config.Sharpness, 0.0f, 1.0f);
    (void)QualityToScale(quality);
    return Result<void>::Success();
}

Result<void> TemporalUpscalerManager::SetTemporalUpscalerDLSS(const TemporalUpscalerDLSSConfig& config) {
    Result<void> transitionResult = ApplyTransition(
        TemporalUpscalerBackend::DLSS,
        config.Quality,
        config.BackendAvailable,
        config.RuntimeFeatureEnabled,
        config.Enabled,
        "SetTemporalUpscalerDLSS");

    if (!transitionResult.Ok) {
        return transitionResult;
    }

    m_State.AutoExposure = config.AutoExposure;
    return Result<void>::Success();
}

Result<void> TemporalUpscalerManager::SetTemporalUpscalerXeSS(const TemporalUpscalerXeSSConfig& config) {
    Result<void> transitionResult = ApplyTransition(
        TemporalUpscalerBackend::XeSS,
        config.Quality,
        config.BackendAvailable,
        config.RuntimeFeatureEnabled,
        config.Enabled,
        "SetTemporalUpscalerXeSS");

    if (!transitionResult.Ok) {
        return transitionResult;
    }

    m_State.JitterResponsiveMask = config.JitterResponsiveMask;
    return Result<void>::Success();
}

void TemporalUpscalerManager::SetCurrentFrameIndex(const uint64_t frameIndex) {
    m_CurrentFrameIndex = frameIndex;
}

void TemporalUpscalerManager::SetFrameGenerationActive(const bool active) {
    m_FrameGenerationActive = active;
}

bool TemporalUpscalerManager::ConsumePendingHistoryReset(uint64_t* serialOut) {
    if (!m_State.HistoryResetPending) {
        return false;
    }
    m_State.HistoryResetPending = false;
    if (serialOut != nullptr) {
        *serialOut = m_State.HistoryResetSerial;
    }
    return true;
}

const TemporalUpscalerRuntimeState& TemporalUpscalerManager::GetState() const {
    return m_State;
}

std::deque<UpscalerPolicyTransitionEvent> TemporalUpscalerManager::GetRecentTransitionEvents() const {
    return m_RecentEvents;
}

Result<void> TemporalUpscalerManager::ApplyTransition(const TemporalUpscalerBackend targetBackend,
                                                      const TemporalUpscalerQualityPreset qualityPreset,
                                                      const bool backendAvailable,
                                                      const bool featureEnabled,
                                                      const bool requestEnabled,
                                                      const std::string& reason) {
    const TemporalUpscalerBackend previousBackend = m_State.ActiveBackend;

    if (!requestEnabled) {
        if (previousBackend != TemporalUpscalerBackend::TAA) {
            m_State.ActiveBackend = TemporalUpscalerBackend::TAA;
            m_State.QualityPreset = TemporalUpscalerQualityPreset::Quality;
            m_State.HistoryResetPending = true;
            ++m_State.HistoryResetSerial;
            PushTransitionEvent(previousBackend, TemporalUpscalerBackend::TAA, true, false, reason, "UPSCALER_DISABLED");
        }
        return Result<void>::Success();
    }

    if (!featureEnabled || !backendAvailable) {
        const std::string fallbackReason = "UPSCALER_BACKEND_UNAVAILABLE";
        PushTransitionEvent(previousBackend, previousBackend, false, false, reason, fallbackReason);
        return Result<void>::Failure(fallbackReason);
    }

    m_State.ActiveBackend = targetBackend;
    m_State.QualityPreset = qualityPreset;
    m_State.HistoryResetPending = previousBackend != targetBackend;
    if (m_State.HistoryResetPending) {
        ++m_State.HistoryResetSerial;
    }

    bool frameGenerationDisabled = false;
    if (previousBackend != targetBackend && m_FrameGenerationActive && IsVendorBackend(previousBackend)) {
        m_FrameGenerationActive = false;
        frameGenerationDisabled = true;
    }

    PushTransitionEvent(
        previousBackend,
        targetBackend,
        m_State.HistoryResetPending,
        frameGenerationDisabled,
        reason,
        std::string());
    return Result<void>::Success();
}

void TemporalUpscalerManager::PushTransitionEvent(const TemporalUpscalerBackend previousBackend,
                                                  const TemporalUpscalerBackend newBackend,
                                                  const bool historyResetRequired,
                                                  const bool frameGenerationDisabled,
                                                  const std::string& reason,
                                                  const std::string& fallbackReason) {
    UpscalerPolicyTransitionEvent event{};
    event.EventId = m_EventIdCounter++;
    event.FrameIndex = m_CurrentFrameIndex;
    event.PreviousBackend = previousBackend;
    event.NewBackend = newBackend;
    event.HistoryResetRequired = historyResetRequired;
    event.FrameGenerationDisabled = frameGenerationDisabled;
    event.Reason = reason;
    event.FallbackReason = fallbackReason;

    m_RecentEvents.push_back(std::move(event));
    while (m_RecentEvents.size() > m_MaxRecentEvents) {
        m_RecentEvents.pop_front();
    }
}

TemporalUpscalerManager& GetTemporalUpscalerManager() {
    static TemporalUpscalerManager manager;
    return manager;
}

const char* ToString(const TemporalUpscalerBackend backend) {
    switch (backend) {
    case TemporalUpscalerBackend::TAA:
        return "TAA";
    case TemporalUpscalerBackend::FSR2:
        return "FSR2";
    case TemporalUpscalerBackend::DLSS:
        return "DLSS";
    case TemporalUpscalerBackend::XeSS:
        return "XeSS";
    default:
        return "Unknown";
    }
}

const char* ToString(const TemporalUpscalerQualityPreset quality) {
    switch (quality) {
    case TemporalUpscalerQualityPreset::Quality:
        return "Quality";
    case TemporalUpscalerQualityPreset::Balanced:
        return "Balanced";
    case TemporalUpscalerQualityPreset::Performance:
        return "Performance";
    case TemporalUpscalerQualityPreset::UltraPerformance:
        return "UltraPerformance";
    case TemporalUpscalerQualityPreset::UltraQuality:
        return "UltraQuality";
    default:
        return "Unknown";
    }
}

} // namespace Core::Renderer

