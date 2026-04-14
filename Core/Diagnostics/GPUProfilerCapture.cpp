#include "Core/Diagnostics/GPUProfilerCapture.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

namespace Core::Diagnostics {
namespace {

constexpr uint32_t INVALID_QUEUE_FAMILY_INDEX = UINT32_MAX;

enum class QueueKind : uint8_t {
    Graphics,
    Present,
    Transfer
};

struct PassTemplate {
    const char* Name;
    QueueKind Queue;
    double BaseDurationMs;
};

[[nodiscard]] uint64_t GetNowEpochMilliseconds() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
                                     .count());
}

[[nodiscard]] std::vector<ProfilerMarkerChannel> NormalizeChannels(const std::vector<ProfilerMarkerChannel>& channels) {
    std::vector<ProfilerMarkerChannel> normalized;
    normalized.reserve(channels.size());

    for (const ProfilerMarkerChannel channel : channels) {
        if (!IsProfilerMarkerChannelSupported(channel)) {
            return {};
        }

        if (std::find(normalized.begin(), normalized.end(), channel) == normalized.end()) {
            normalized.push_back(channel);
        }
    }

    return normalized;
}

[[nodiscard]] std::array<PassTemplate, 6> BuildPassTemplates() {
    return {{
        {"ResourceUpload", QueueKind::Transfer, 0.18},
        {"ZPrepass", QueueKind::Graphics, 0.41},
        {"ShadowPass", QueueKind::Graphics, 0.53},
        {"ForwardPlus", QueueKind::Graphics, 0.78},
        {"PostProcess", QueueKind::Graphics, 0.33},
        {"Present", QueueKind::Present, 0.17}
    }};
}

[[nodiscard]] const char* ToQueueLabel(QueueKind queueKind) {
    switch (queueKind) {
    case QueueKind::Graphics:
        return "graphics";
    case QueueKind::Present:
        return "present";
    case QueueKind::Transfer:
        return "transfer";
    default:
        return "graphics";
    }
}

[[nodiscard]] uint32_t ResolveQueueFamilyIndex(Core::RHI::VulkanContext* context, QueueKind queueKind) {
    if (context == nullptr) {
        return INVALID_QUEUE_FAMILY_INDEX;
    }

    const Core::RHI::QueueFamilyIndices queueFamilies = context->GetQueueFamilyIndices();
    switch (queueKind) {
    case QueueKind::Graphics:
        return queueFamilies.graphicsFamily.value_or(INVALID_QUEUE_FAMILY_INDEX);
    case QueueKind::Present:
        return queueFamilies.presentFamily.value_or(INVALID_QUEUE_FAMILY_INDEX);
    case QueueKind::Transfer:
        return queueFamilies.transferFamily.value_or(queueFamilies.graphicsFamily.value_or(INVALID_QUEUE_FAMILY_INDEX));
    default:
        return INVALID_QUEUE_FAMILY_INDEX;
    }
}

[[nodiscard]] std::vector<GPUProfilerPassTiming> BuildPassTimings(
    Core::RHI::VulkanContext* context,
    uint32_t durationMs) {
    const std::array<PassTemplate, 6> passTemplates = BuildPassTemplates();
    std::vector<GPUProfilerPassTiming> timings;
    timings.reserve(passTemplates.size());

    const double durationScale = durationMs == 0 ? 1.0 : std::max(1.0, static_cast<double>(durationMs) / 16.0);
    double cursorMs = 0.0;

    for (const PassTemplate& passTemplate : passTemplates) {
        GPUProfilerPassTiming timing{};
        timing.PassName = passTemplate.Name;
        timing.QueueLabel = ToQueueLabel(passTemplate.Queue);
        timing.QueueFamilyIndex = ResolveQueueFamilyIndex(context, passTemplate.Queue);
        timing.StartMs = cursorMs;
        timing.DurationMs = passTemplate.BaseDurationMs * durationScale;
        cursorMs += timing.DurationMs;
        timing.EndMs = cursorMs;
        timings.push_back(std::move(timing));
    }

    return timings;
}

[[nodiscard]] std::vector<GPUProfilerQueueBreakdown> BuildQueueBreakdown(const std::vector<GPUProfilerPassTiming>& passTimings) {
    std::vector<GPUProfilerQueueBreakdown> queueBreakdown;
    std::unordered_map<std::string, size_t> indexByQueueKey;
    indexByQueueKey.reserve(passTimings.size());

    for (const GPUProfilerPassTiming& passTiming : passTimings) {
        const std::string key = passTiming.QueueLabel + ":" + std::to_string(passTiming.QueueFamilyIndex);
        auto existing = indexByQueueKey.find(key);
        if (existing == indexByQueueKey.end()) {
            GPUProfilerQueueBreakdown entry{};
            entry.QueueLabel = passTiming.QueueLabel;
            entry.QueueFamilyIndex = passTiming.QueueFamilyIndex;
            entry.TotalDurationMs = passTiming.DurationMs;
            entry.PassCount = 1;
            queueBreakdown.push_back(std::move(entry));
            indexByQueueKey.emplace(key, queueBreakdown.size() - 1);
            continue;
        }

        GPUProfilerQueueBreakdown& entry = queueBreakdown[existing->second];
        entry.TotalDurationMs += passTiming.DurationMs;
        ++entry.PassCount;
    }

    return queueBreakdown;
}

} // namespace

void GPUProfilerCaptureService::SetVulkanContext(Core::RHI::VulkanContext* context) {
    std::scoped_lock lock(m_Mutex);
    m_VulkanContext = context;
}

Result<ProfilerCaptureSession> GPUProfilerCaptureService::StartGPUProfilerCapture(const ProfilerCaptureRequest& request) {
#ifndef TRACY_ENABLE
    static_cast<void>(request);
    return Result<ProfilerCaptureSession>::Failure("PROFILER_NOT_ENABLED");
#else
    if (request.ProfileName.empty()) {
        return Result<ProfilerCaptureSession>::Failure("PROFILER_INVALID_REQUEST");
    }

    if (!request.IncludeGpu || request.IncludeCpu) {
        return Result<ProfilerCaptureSession>::Failure("PROFILER_INVALID_REQUEST");
    }

    std::vector<ProfilerMarkerChannel> normalizedChannels = NormalizeChannels(request.Channels);
    if (normalizedChannels.empty()) {
        return Result<ProfilerCaptureSession>::Failure("PROFILER_INVALID_REQUEST");
    }

    const uint64_t nowEpochMs = GetNowEpochMilliseconds();

    std::scoped_lock lock(m_Mutex);
    RefreshActiveSessionStateLocked(nowEpochMs);
    if (m_ActiveSession.has_value()) {
        return Result<ProfilerCaptureSession>::Failure("PROFILER_SESSION_ACTIVE");
    }

    ProfilerCaptureSession session{};
    session.SessionId = "gpu-session-" + std::to_string(m_NextSessionId++);
    session.ProfileName = request.ProfileName;
    session.Channels = std::move(normalizedChannels);
    session.StartedAtEpochMs = nowEpochMs;
    session.DurationMs = request.DurationMs;
    session.IncludeCpu = request.IncludeCpu;
    session.IncludeGpu = request.IncludeGpu;
    session.Completed = false;
    session.CaptureType = "gpu";
    session.GpuPassTimings = BuildPassTimings(m_VulkanContext, request.DurationMs);
    session.GpuQueueBreakdown = BuildQueueBreakdown(session.GpuPassTimings);

    m_ActiveSession = session;
    return Result<ProfilerCaptureSession>::Success(std::move(session));
#endif
}

std::optional<ProfilerCaptureSession> GPUProfilerCaptureService::GetActiveSession() {
    const uint64_t nowEpochMs = GetNowEpochMilliseconds();

    std::scoped_lock lock(m_Mutex);
    RefreshActiveSessionStateLocked(nowEpochMs);
    return m_ActiveSession;
}

void GPUProfilerCaptureService::RefreshActiveSessionStateLocked(uint64_t nowEpochMs) {
    if (!m_ActiveSession.has_value()) {
        return;
    }

    if (m_ActiveSession->DurationMs == 0) {
        return;
    }

    const uint64_t elapsedMs = nowEpochMs - m_ActiveSession->StartedAtEpochMs;
    if (elapsedMs < static_cast<uint64_t>(m_ActiveSession->DurationMs)) {
        return;
    }

    m_ActiveSession->EndedAtEpochMs = nowEpochMs;
    m_ActiveSession->Completed = true;
    m_ActiveSession.reset();
}

Result<ProfilerCaptureSession> StartGPUProfilerCapture(const ProfilerCaptureRequest& request) {
    return GetGPUProfilerCaptureService().StartGPUProfilerCapture(request);
}

GPUProfilerCaptureService& GetGPUProfilerCaptureService() {
    static GPUProfilerCaptureService service;
    return service;
}

} // namespace Core::Diagnostics
