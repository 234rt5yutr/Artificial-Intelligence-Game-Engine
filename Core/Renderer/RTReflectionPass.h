#pragma once

#include "Core/Asset/Addressables/AddressablesCatalogTypes.h"
#include "Core/Math/Math.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Core::Renderer {

template <typename T>
using Result = Core::Asset::Addressables::Result<T>;

enum class RTReflectionQuality : uint8_t {
    Off = 0,
    Low = 1,
    Medium = 2,
    High = 3,
    Ultra = 4
};

struct RTReflectionConfig {
    RTReflectionQuality quality = RTReflectionQuality::Medium;
    uint32_t maxBounces = 1;
    float roughnessThreshold = 0.5f;
    float maxRayDistance = 500.0f;
    float resolutionScale = 1.0f;
    uint32_t samplesPerPixel = 1;
    bool enableDenoising = true;
};

struct MaterialReflectionOverride {
    bool hasOverride = false;
    RTReflectionQuality quality = RTReflectionQuality::Medium;
    uint32_t maxBounces = 1;
    float roughnessOverride = -1.0f;
};

struct ReflectionTraceRequest {
    RTReflectionConfig config{};
    bool hardwareRayTracingSupported = false;
    bool halfResolutionTrace = false;
    uint64_t frameIndex = 0;
    float frameBudgetMs = 16.67f;
    float currentFrameTimeMs = 16.67f;
};

struct ReflectionTraceResult {
    RTReflectionQuality qualityUsed = RTReflectionQuality::Off;
    uint32_t bouncesUsed = 0;
    uint32_t raysTraced = 0;
    float estimatedCostMs = 0.0f;
    bool fallbackToScreenSpace = true;
    std::string diagnostics;
};

Result<ReflectionTraceResult> TraceHardwareReflections(const ReflectionTraceRequest& request);

class RTReflectionPass {
public:
    RTReflectionPass() = default;

    void SetConfig(const RTReflectionConfig& config);
    const RTReflectionConfig& GetConfig() const;

    void SetQuality(RTReflectionQuality quality);
    RTReflectionQuality GetQuality() const;

    void SetMaterialOverride(uint32_t materialId, const MaterialReflectionOverride& overrideValue);
    void ClearMaterialOverride(uint32_t materialId);
    void ClearAllMaterialOverrides();

    static uint32_t GetSamplesForQuality(RTReflectionQuality quality);
    static uint32_t GetBouncesForQuality(RTReflectionQuality quality);
    static float GetResolutionScaleForQuality(RTReflectionQuality quality);

private:
    RTReflectionConfig m_Config{};
    std::unordered_map<uint32_t, MaterialReflectionOverride> m_MaterialOverrides;
};

} // namespace Core::Renderer

namespace AIEngine::Rendering {
using Core::Renderer::MaterialReflectionOverride;
using Core::Renderer::RTReflectionConfig;
using Core::Renderer::RTReflectionPass;
using Core::Renderer::RTReflectionQuality;
using Core::Renderer::ReflectionTraceRequest;
using Core::Renderer::ReflectionTraceResult;
using Core::Renderer::TraceHardwareReflections;
}
