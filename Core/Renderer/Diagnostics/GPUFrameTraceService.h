#pragma once

#include "Core/Asset/Addressables/AddressablesCatalogTypes.h"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

namespace Core::RHI {
class VulkanContext;
}

namespace Core::Renderer {

template <typename T>
using Result = Core::Asset::Addressables::Result<T>;

struct GPUFrameTraceRequest {
    uint32_t FrameCount = 1;
    bool IncludeMarkers = true;
    bool IncludePipelineStats = true;
    std::filesystem::path OutputPath;
    std::string FrameTag = "runtime";
    std::vector<std::string> PassNames;
    std::vector<std::string> ResourceNames;
};

struct GPUFrameTracePassSummary {
    std::string PassName;
    double StartMs = 0.0;
    double EndMs = 0.0;
    double DurationMs = 0.0;
};

struct GPUFrameTraceResourceSummary {
    std::string ResourceName;
    std::string Usage;
    uint64_t EstimatedBytes = 0;
};

struct GPUFrameTraceArtifact {
    std::string TraceCaptureId;
    std::string FrameTag;
    std::filesystem::path JsonArtifactPath;
    std::filesystem::path TextArtifactPath;
    uint32_t CapturedFrameCount = 0;
    uint32_t MarkerCount = 0;
    uint64_t PipelineWarmupHits = 0;
    uint64_t PipelineWarmupMisses = 0;
    std::vector<GPUFrameTracePassSummary> Passes;
    std::vector<GPUFrameTraceResourceSummary> Resources;
    std::string SummaryText;
};

class GPUFrameTraceService {
public:
    void SetVulkanContext(Core::RHI::VulkanContext* context);
    Result<GPUFrameTraceArtifact> CaptureGPUFrameTrace(const GPUFrameTraceRequest& request);
    std::deque<GPUFrameTraceArtifact> GetRecentArtifacts() const;

private:
    void PushArtifact(const GPUFrameTraceArtifact& artifact);

private:
    Core::RHI::VulkanContext* m_VulkanContext = nullptr;
    std::deque<GPUFrameTraceArtifact> m_RecentArtifacts{};
    uint64_t m_TraceIdCounter = 1;
    size_t m_MaxRecentArtifacts = 16;
};

GPUFrameTraceService& GetGPUFrameTraceService();

} // namespace Core::Renderer

