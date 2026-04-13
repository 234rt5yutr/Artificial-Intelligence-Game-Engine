#pragma once

#include "Core/Asset/Addressables/AddressablesCatalogTypes.h"

#include <cstdint>
#include <deque>
#include <string>

namespace Core::Renderer {

template <typename T>
using Result = Core::Asset::Addressables::Result<T>;

enum class TemporalUpscalerBackend : uint8_t {
    TAA = 0,
    FSR2 = 1,
    DLSS = 2,
    XeSS = 3
};

enum class TemporalUpscalerQualityPreset : uint8_t {
    Quality = 0,
    Balanced = 1,
    Performance = 2,
    UltraPerformance = 3,
    UltraQuality = 4
};

struct TemporalUpscalerFSR2Config {
    bool Enabled = true;
    TemporalUpscalerQualityPreset Quality = TemporalUpscalerQualityPreset::Balanced;
    float Sharpness = 0.2f;
    bool RuntimeFeatureEnabled = false;
    bool BackendAvailable = false;
};

struct TemporalUpscalerDLSSConfig {
    bool Enabled = true;
    TemporalUpscalerQualityPreset Quality = TemporalUpscalerQualityPreset::Balanced;
    bool AutoExposure = true;
    bool RuntimeFeatureEnabled = false;
    bool BackendAvailable = false;
};

struct TemporalUpscalerXeSSConfig {
    bool Enabled = true;
    TemporalUpscalerQualityPreset Quality = TemporalUpscalerQualityPreset::Quality;
    bool JitterResponsiveMask = true;
    bool RuntimeFeatureEnabled = false;
    bool BackendAvailable = false;
};

struct UpscalerPolicyTransitionEvent {
    uint64_t EventId = 0;
    uint64_t FrameIndex = 0;
    TemporalUpscalerBackend PreviousBackend = TemporalUpscalerBackend::TAA;
    TemporalUpscalerBackend NewBackend = TemporalUpscalerBackend::TAA;
    bool HistoryResetRequired = false;
    bool FrameGenerationDisabled = false;
    std::string Reason;
    std::string FallbackReason;
};

struct TemporalUpscalerRuntimeState {
    TemporalUpscalerBackend ActiveBackend = TemporalUpscalerBackend::TAA;
    TemporalUpscalerQualityPreset QualityPreset = TemporalUpscalerQualityPreset::Quality;
    float Sharpness = 0.0f;
    bool AutoExposure = true;
    bool JitterResponsiveMask = true;
    bool HistoryResetPending = false;
    uint64_t HistoryResetSerial = 0;
};

class TemporalUpscalerManager {
public:
    Result<void> SetTemporalUpscalerFSR2(const TemporalUpscalerFSR2Config& config);
    Result<void> SetTemporalUpscalerDLSS(const TemporalUpscalerDLSSConfig& config);
    Result<void> SetTemporalUpscalerXeSS(const TemporalUpscalerXeSSConfig& config);

    void SetCurrentFrameIndex(uint64_t frameIndex);
    void SetFrameGenerationActive(bool active);
    bool ConsumePendingHistoryReset(uint64_t* serialOut);

    const TemporalUpscalerRuntimeState& GetState() const;
    std::deque<UpscalerPolicyTransitionEvent> GetRecentTransitionEvents() const;

private:
    Result<void> ApplyTransition(TemporalUpscalerBackend targetBackend,
                                 TemporalUpscalerQualityPreset qualityPreset,
                                 bool backendAvailable,
                                 bool featureEnabled,
                                 bool requestEnabled,
                                 const std::string& reason);

    void PushTransitionEvent(TemporalUpscalerBackend previousBackend,
                             TemporalUpscalerBackend newBackend,
                             bool historyResetRequired,
                             bool frameGenerationDisabled,
                             const std::string& reason,
                             const std::string& fallbackReason);

private:
    TemporalUpscalerRuntimeState m_State{};
    std::deque<UpscalerPolicyTransitionEvent> m_RecentEvents{};
    uint64_t m_EventIdCounter = 1;
    uint64_t m_CurrentFrameIndex = 0;
    bool m_FrameGenerationActive = false;
    size_t m_MaxRecentEvents = 64;
};

TemporalUpscalerManager& GetTemporalUpscalerManager();

const char* ToString(TemporalUpscalerBackend backend);
const char* ToString(TemporalUpscalerQualityPreset quality);

} // namespace Core::Renderer

