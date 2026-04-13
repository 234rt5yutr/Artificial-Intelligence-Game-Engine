#pragma once

#include "Core/Asset/Addressables/AddressablesCatalogTypes.h"
#include "Core/Math/Math.h"

#include <entt/entt.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Core::Renderer {

template <typename T>
using Result = Core::Asset::Addressables::Result<T>;

enum class GITechnique : uint8_t {
    None = 0,
    ScreenSpace = 1,
    VoxelConeTracing = 2,
    RayTraced = 3,
    IrradianceProbes = 4
};

enum class GIQuality : uint8_t {
    Off = 0,
    Low = 1,
    Medium = 2,
    High = 3,
    Ultra = 4
};

struct GIConfig {
    GITechnique technique = GITechnique::ScreenSpace;
    GIQuality quality = GIQuality::Medium;
    uint32_t maxBounces = 1;
    uint32_t samplesPerPixel = 1;
    float indirectIntensity = 1.0f;
    float maxTraceDistance = 80.0f;
    bool enableTemporalFilter = true;
    float temporalBlendFactor = 0.1f;
};

struct DynamicGIRequest {
    GIConfig config{};
    bool hardwareRayTracingSupported = false;
    bool preferQuality = false;
    uint64_t frameIndex = 0;
    float frameBudgetMs = 16.67f;
    float currentFrameTimeMs = 16.67f;
};

struct DynamicGIResult {
    GITechnique techniqueUsed = GITechnique::None;
    GIQuality qualityUsed = GIQuality::Off;
    uint32_t bounceCount = 0;
    float estimatedCostMs = 0.0f;
    bool fallbackUsed = false;
    std::string fallbackReason;
};

using BakeProgressCallback = std::function<void(float progress, const std::string& status)>;

struct IrradianceVolume {
    Math::Vec3 minBounds = Math::Vec3(0.0f);
    Math::Vec3 maxBounds = Math::Vec3(0.0f);
    Math::IVec3 resolution = Math::IVec3(1);
};

Result<DynamicGIResult> ComputeDynamicGlobalIllumination(const DynamicGIRequest& request);

class GlobalIllumination {
public:
    GlobalIllumination() = default;

    void SetConfig(const GIConfig& config);
    const GIConfig& GetConfig() const;

    void SetTechnique(GITechnique technique);
    GITechnique GetTechnique() const;

    void SetQuality(GIQuality quality);
    GIQuality GetQuality() const;

    uint32_t AddIrradianceVolume(
        const Math::Vec3& minBounds,
        const Math::Vec3& maxBounds,
        const Math::IVec3& resolution);

    void StartAsyncBake(entt::registry& registry, BakeProgressCallback callback = nullptr);
    bool IsAsyncBakeInProgress() const;

private:
    GIConfig m_Config{};
    std::vector<IrradianceVolume> m_Volumes;
    bool m_AsyncBakeInProgress = false;
    float m_AsyncBakeProgress = 0.0f;
};

} // namespace Core::Renderer

namespace AIEngine::Rendering {
using Core::Renderer::BakeProgressCallback;
using Core::Renderer::ComputeDynamicGlobalIllumination;
using Core::Renderer::DynamicGIRequest;
using Core::Renderer::DynamicGIResult;
using Core::Renderer::GIConfig;
using Core::Renderer::GITechnique;
using Core::Renderer::GIQuality;
using Core::Renderer::GlobalIllumination;
using Core::Renderer::IrradianceVolume;
}
