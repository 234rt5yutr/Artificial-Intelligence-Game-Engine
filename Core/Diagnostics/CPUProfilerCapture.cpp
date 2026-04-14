#include "Core/Diagnostics/CPUProfilerCapture.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>

namespace Core::Diagnostics {
namespace {

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

} // namespace

const char* ToString(ProfilerMarkerChannel channel) {
    switch (channel) {
    case ProfilerMarkerChannel::Render:
        return "render";
    case ProfilerMarkerChannel::Physics:
        return "physics";
    case ProfilerMarkerChannel::Network:
        return "network";
    case ProfilerMarkerChannel::Ui:
        return "ui";
    case ProfilerMarkerChannel::Script:
        return "script";
    case ProfilerMarkerChannel::Custom:
        return "custom";
    default:
        return "custom";
    }
}

Result<ProfilerMarkerChannel> ProfilerMarkerChannelFromString(const std::string_view channelName) {
    if (channelName == "render") {
        return Result<ProfilerMarkerChannel>::Success(ProfilerMarkerChannel::Render);
    }
    if (channelName == "physics") {
        return Result<ProfilerMarkerChannel>::Success(ProfilerMarkerChannel::Physics);
    }
    if (channelName == "network") {
        return Result<ProfilerMarkerChannel>::Success(ProfilerMarkerChannel::Network);
    }
    if (channelName == "ui") {
        return Result<ProfilerMarkerChannel>::Success(ProfilerMarkerChannel::Ui);
    }
    if (channelName == "script") {
        return Result<ProfilerMarkerChannel>::Success(ProfilerMarkerChannel::Script);
    }
    if (channelName == "custom") {
        return Result<ProfilerMarkerChannel>::Success(ProfilerMarkerChannel::Custom);
    }

    return Result<ProfilerMarkerChannel>::Failure("PROFILER_INVALID_REQUEST");
}

Result<ProfilerCaptureSession> CPUProfilerCaptureService::StartCPUProfilerCapture(const ProfilerCaptureRequest& request) {
#ifndef TRACY_ENABLE
    static_cast<void>(request);
    return Result<ProfilerCaptureSession>::Failure("PROFILER_NOT_ENABLED");
#else
    if (request.ProfileName.empty()) {
        return Result<ProfilerCaptureSession>::Failure("PROFILER_INVALID_REQUEST");
    }

    if (!request.IncludeCpu || request.IncludeGpu) {
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
    session.SessionId = "cpu-session-" + std::to_string(m_NextSessionId++);
    session.ProfileName = request.ProfileName;
    session.Channels = std::move(normalizedChannels);
    session.StartedAtEpochMs = nowEpochMs;
    session.DurationMs = request.DurationMs;
    session.IncludeCpu = request.IncludeCpu;
    session.IncludeGpu = request.IncludeGpu;
    session.Completed = false;
    session.CaptureType = "cpu";

    m_ActiveSession = session;
    return Result<ProfilerCaptureSession>::Success(std::move(session));
#endif
}

std::optional<ProfilerCaptureSession> CPUProfilerCaptureService::GetActiveSession() {
    const uint64_t nowEpochMs = GetNowEpochMilliseconds();

    std::scoped_lock lock(m_Mutex);
    RefreshActiveSessionStateLocked(nowEpochMs);
    return m_ActiveSession;
}

void CPUProfilerCaptureService::RefreshActiveSessionStateLocked(uint64_t nowEpochMs) {
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

Result<ProfilerCaptureSession> StartCPUProfilerCapture(const ProfilerCaptureRequest& request) {
    return GetCPUProfilerCaptureService().StartCPUProfilerCapture(request);
}

CPUProfilerCaptureService& GetCPUProfilerCaptureService() {
    static CPUProfilerCaptureService service;
    return service;
}

} // namespace Core::Diagnostics
