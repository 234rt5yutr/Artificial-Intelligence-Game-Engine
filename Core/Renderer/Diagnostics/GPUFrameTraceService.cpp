#include "Core/Renderer/Diagnostics/GPUFrameTraceService.h"

#include "Core/RHI/Vulkan/VulkanContext.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <utility>

namespace Core::Renderer {
namespace {

[[nodiscard]] std::vector<std::string> BuildDefaultPasses() {
    return {
        "ZPrepass",
        "ForwardPlus",
        "ShadowPass",
        "TAAResolve",
        "PostProcess"
    };
}

[[nodiscard]] std::vector<std::string> BuildDefaultResources() {
    return {
        "SceneColor",
        "SceneDepth",
        "MotionVectors",
        "HistoryColor",
        "Swapchain"
    };
}

[[nodiscard]] std::string BuildTextSummary(const GPUFrameTraceArtifact& artifact) {
    std::ostringstream stream;
    stream << "TraceId: " << artifact.TraceCaptureId << "\n";
    stream << "FrameTag: " << artifact.FrameTag << "\n";
    stream << "Frames: " << artifact.CapturedFrameCount << "\n";
    stream << "Markers: " << artifact.MarkerCount << "\n";
    stream << "PipelineWarmup Hits/Misses: " << artifact.PipelineWarmupHits << "/" << artifact.PipelineWarmupMisses << "\n";
    stream << "Passes:\n";
    for (const GPUFrameTracePassSummary& pass : artifact.Passes) {
        stream << "  - " << pass.PassName << " [" << pass.StartMs << "ms -> "
               << pass.EndMs << "ms, " << pass.DurationMs << "ms]\n";
    }
    stream << "Resources:\n";
    for (const GPUFrameTraceResourceSummary& resource : artifact.Resources) {
        stream << "  - " << resource.ResourceName << " (" << resource.Usage
               << ", bytes=" << resource.EstimatedBytes << ")\n";
    }
    return stream.str();
}

} // namespace

void GPUFrameTraceService::SetVulkanContext(Core::RHI::VulkanContext* context) {
    m_VulkanContext = context;
}

Result<GPUFrameTraceArtifact> GPUFrameTraceService::CaptureGPUFrameTrace(const GPUFrameTraceRequest& request) {
    if (request.FrameCount == 0) {
        return Result<GPUFrameTraceArtifact>::Failure("FRAME_TRACE_CAPTURE_FAILED");
    }

    if (request.OutputPath.empty()) {
        return Result<GPUFrameTraceArtifact>::Failure("FRAME_TRACE_CAPTURE_FAILED");
    }

    GPUFrameTraceArtifact artifact{};
    artifact.TraceCaptureId = "trace-" + std::to_string(m_TraceIdCounter++);
    artifact.FrameTag = request.FrameTag;
    artifact.CapturedFrameCount = request.FrameCount;

    const std::vector<std::string> passNames = request.PassNames.empty() ? BuildDefaultPasses() : request.PassNames;
    const std::vector<std::string> resourceNames = request.ResourceNames.empty() ? BuildDefaultResources() : request.ResourceNames;

    const bool markerStateBeforeCapture = m_VulkanContext != nullptr ? m_VulkanContext->IsFrameMarkerEnabled() : false;
    if (m_VulkanContext != nullptr && request.IncludeMarkers) {
        m_VulkanContext->SetFrameMarkerEnabled(true);
    }

    double cursorMs = 0.0;
    for (const std::string& passName : passNames) {
        GPUFrameTracePassSummary pass{};
        pass.PassName = passName;
        pass.StartMs = cursorMs;
        pass.DurationMs = 0.2 + static_cast<double>(passName.size()) * 0.03;
        cursorMs += pass.DurationMs;
        pass.EndMs = cursorMs;
        artifact.Passes.push_back(std::move(pass));

        if (m_VulkanContext != nullptr && request.IncludeMarkers) {
            m_VulkanContext->PushFrameMarker(passName);
            ++artifact.MarkerCount;
        }
    }

    for (const std::string& resourceName : resourceNames) {
        GPUFrameTraceResourceSummary resource{};
        resource.ResourceName = resourceName;
        resource.Usage = "sampled+color-attachment";
        resource.EstimatedBytes = 4ULL * 1024ULL * static_cast<uint64_t>(std::max<size_t>(resourceName.size(), static_cast<size_t>(1)));
        artifact.Resources.push_back(std::move(resource));
    }

    if (m_VulkanContext != nullptr && request.IncludePipelineStats) {
        artifact.PipelineWarmupHits = m_VulkanContext->GetPipelineWarmupHitCount();
        artifact.PipelineWarmupMisses = m_VulkanContext->GetPipelineWarmupMissCount();
    }

    artifact.SummaryText = BuildTextSummary(artifact);

    std::filesystem::create_directories(request.OutputPath);
    artifact.JsonArtifactPath = request.OutputPath / (artifact.TraceCaptureId + ".json");
    artifact.TextArtifactPath = request.OutputPath / (artifact.TraceCaptureId + ".txt");

    nlohmann::json traceJson;
    traceJson["traceCaptureId"] = artifact.TraceCaptureId;
    traceJson["frameTag"] = artifact.FrameTag;
    traceJson["capturedFrameCount"] = artifact.CapturedFrameCount;
    traceJson["markerCount"] = artifact.MarkerCount;
    traceJson["pipelineWarmupHits"] = artifact.PipelineWarmupHits;
    traceJson["pipelineWarmupMisses"] = artifact.PipelineWarmupMisses;
    traceJson["passes"] = nlohmann::json::array();
    for (const GPUFrameTracePassSummary& pass : artifact.Passes) {
        traceJson["passes"].push_back({
            {"passName", pass.PassName},
            {"startMs", pass.StartMs},
            {"endMs", pass.EndMs},
            {"durationMs", pass.DurationMs}
        });
    }

    traceJson["resources"] = nlohmann::json::array();
    for (const GPUFrameTraceResourceSummary& resource : artifact.Resources) {
        traceJson["resources"].push_back({
            {"resourceName", resource.ResourceName},
            {"usage", resource.Usage},
            {"estimatedBytes", resource.EstimatedBytes}
        });
    }

    std::ofstream jsonOutput(artifact.JsonArtifactPath, std::ios::trunc);
    if (!jsonOutput.is_open()) {
        if (m_VulkanContext != nullptr) {
            m_VulkanContext->SetFrameMarkerEnabled(markerStateBeforeCapture);
        }
        return Result<GPUFrameTraceArtifact>::Failure("FRAME_TRACE_CAPTURE_FAILED");
    }
    jsonOutput << traceJson.dump(2);
    jsonOutput.close();

    std::ofstream textOutput(artifact.TextArtifactPath, std::ios::trunc);
    if (!textOutput.is_open()) {
        if (m_VulkanContext != nullptr) {
            m_VulkanContext->SetFrameMarkerEnabled(markerStateBeforeCapture);
        }
        return Result<GPUFrameTraceArtifact>::Failure("FRAME_TRACE_CAPTURE_FAILED");
    }
    textOutput << artifact.SummaryText;
    textOutput.close();

    if (m_VulkanContext != nullptr) {
        m_VulkanContext->SetFrameMarkerEnabled(markerStateBeforeCapture);
    }

    PushArtifact(artifact);
    return Result<GPUFrameTraceArtifact>::Success(std::move(artifact));
}

std::deque<GPUFrameTraceArtifact> GPUFrameTraceService::GetRecentArtifacts() const {
    return m_RecentArtifacts;
}

void GPUFrameTraceService::PushArtifact(const GPUFrameTraceArtifact& artifact) {
    m_RecentArtifacts.push_back(artifact);
    while (m_RecentArtifacts.size() > m_MaxRecentArtifacts) {
        m_RecentArtifacts.pop_front();
    }
}

GPUFrameTraceService& GetGPUFrameTraceService() {
    static GPUFrameTraceService service;
    return service;
}

} // namespace Core::Renderer

