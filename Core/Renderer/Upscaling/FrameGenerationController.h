#pragma once

#include "Core/Asset/Addressables/AddressablesCatalogTypes.h"
#include "Core/Renderer/Upscaling/TemporalUpscalerManager.h"

#include <cstdint>
#include <deque>
#include <string>

namespace Core::Renderer {

template <typename T>
using Result = Core::Asset::Addressables::Result<T>;

struct FrameGenerationConfig {
    bool Enabled = true;
    float MaxAddedLatencyMs = 10.0f;
    bool FallbackOnInputSpike = true;
    float CurrentEndToEndLatencyMs = 0.0f;
    float EstimatedFrameGenerationLatencyMs = 0.0f;
    bool RuntimeFeatureEnabled = false;
    bool BackendSupportsFrameGeneration = false;
    bool InputLatencySpikeDetected = false;
    TemporalUpscalerBackend ActiveUpscaler = TemporalUpscalerBackend::TAA;
};

struct FrameGenerationResult {
    bool Active = false;
    bool FallbackUsed = false;
    std::string FallbackReason;
    float AddedLatencyMs = 0.0f;
    float EffectiveLatencyMs = 0.0f;
    TemporalUpscalerBackend UpscalerBackend = TemporalUpscalerBackend::TAA;
    uint64_t EventId = 0;
    uint64_t FrameIndex = 0;
};

class FrameGenerationController {
public:
    Result<FrameGenerationResult> EnableFrameGeneration(const FrameGenerationConfig& config);

    void SetCurrentFrameIndex(uint64_t frameIndex);
    bool IsFrameGenerationActive() const;
    FrameGenerationResult GetLastResult() const;
    std::deque<FrameGenerationResult> GetRecentEvents() const;

private:
    void PushResultEvent(const FrameGenerationResult& result);

private:
    bool m_FrameGenerationActive = false;
    FrameGenerationResult m_LastResult{};
    std::deque<FrameGenerationResult> m_RecentEvents{};
    uint64_t m_CurrentFrameIndex = 0;
    uint64_t m_EventIdCounter = 1;
    size_t m_MaxRecentEvents = 64;
};

FrameGenerationController& GetFrameGenerationController();

} // namespace Core::Renderer

