#pragma once

#include "Core/Asset/Addressables/AddressablesCatalogTypes.h"
#include "Core/Math/Math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Core::Renderer {

template <typename T>
using Result = Core::Asset::Addressables::Result<T>;

enum class RTShadowQuality : uint8_t {
    Off = 0,
    Low = 1,
    Medium = 2,
    High = 3,
    Ultra = 4
};

struct RTShadowLightSettings {
    bool useRayTracing = true;
    RTShadowQuality quality = RTShadowQuality::Medium;
    float softShadowRadius = 0.1f;
    float maxRayDistance = 1000.0f;
    float shadowBias = 0.001f;
};

struct RTShadowConfig {
    RTShadowQuality globalQuality = RTShadowQuality::Medium;
    bool enableDenoising = true;
    float resolutionScale = 1.0f;
    uint32_t maxLightsWithRTShadows = 4;
};

struct ShadowTraceRequest {
    RTShadowConfig config{};
    uint32_t lightCount = 0;
    bool hardwareRayTracingSupported = false;
    uint64_t frameIndex = 0;
    float frameBudgetMs = 16.67f;
    float currentFrameTimeMs = 16.67f;
};

struct ShadowTraceResult {
    RTShadowQuality qualityUsed = RTShadowQuality::Off;
    uint32_t lightsRendered = 0;
    uint32_t raysTraced = 0;
    float estimatedCostMs = 0.0f;
    bool usedFallbackShadowMaps = true;
    std::string diagnostics;
};

Result<ShadowTraceResult> TraceHardwareShadows(const ShadowTraceRequest& request);

class RTShadowPass {
public:
    RTShadowPass() = default;

    void SetConfig(const RTShadowConfig& config);
    const RTShadowConfig& GetConfig() const;

    void SetQuality(RTShadowQuality quality);
    RTShadowQuality GetQuality() const;

    void SetLightSettings(uint32_t lightIndex, const RTShadowLightSettings& settings);
    const RTShadowLightSettings& GetLightSettings(uint32_t lightIndex) const;

    static uint32_t GetSamplesForQuality(RTShadowQuality quality);

private:
    RTShadowConfig m_Config{};
    std::vector<RTShadowLightSettings> m_LightSettings;
};

} // namespace Core::Renderer

namespace AIEngine::Rendering {
using Core::Renderer::RTShadowConfig;
using Core::Renderer::RTShadowLightSettings;
using Core::Renderer::RTShadowPass;
using Core::Renderer::RTShadowQuality;
using Core::Renderer::ShadowTraceRequest;
using Core::Renderer::ShadowTraceResult;
using Core::Renderer::TraceHardwareShadows;
}
