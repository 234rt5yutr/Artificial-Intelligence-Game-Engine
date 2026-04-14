#include "Core/Diagnostics/TraceExporter.h"

#include "Core/Diagnostics/CPUProfilerCapture.h"
#include "Core/Diagnostics/GPUProfilerCapture.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace Core::Diagnostics {
namespace {

[[nodiscard]] uint64_t GetNowEpochMilliseconds() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
                                     .count());
}

[[nodiscard]] std::string ToString(TraceExportFormat format) {
    switch (format) {
    case TraceExportFormat::Json:
        return "json";
    case TraceExportFormat::ChromeTrace:
        return "chrome_trace";
    default:
        return "json";
    }
}

[[nodiscard]] std::optional<ProfilerCaptureSession> ResolveSessionById(const std::string& sessionId) {
    const std::optional<ProfilerCaptureSession> cpuSession = GetCPUProfilerCaptureService().GetActiveSession();
    if (cpuSession.has_value() && cpuSession->SessionId == sessionId) {
        return cpuSession;
    }

    const std::optional<ProfilerCaptureSession> gpuSession = GetGPUProfilerCaptureService().GetActiveSession();
    if (gpuSession.has_value() && gpuSession->SessionId == sessionId) {
        return gpuSession;
    }

    return std::nullopt;
}

[[nodiscard]] nlohmann::json BuildChannelsJson(const std::vector<ProfilerMarkerChannel>& channels) {
    nlohmann::json channelsJson = nlohmann::json::array();
    for (const ProfilerMarkerChannel channel : channels) {
        channelsJson.push_back(ToString(channel));
    }
    return channelsJson;
}

[[nodiscard]] nlohmann::json BuildGpuPassTimingsJson(const std::vector<GPUProfilerPassTiming>& timings) {
    nlohmann::json timingsJson = nlohmann::json::array();
    for (const GPUProfilerPassTiming& timing : timings) {
        timingsJson.push_back({
            {"passName", timing.PassName},
            {"queueLabel", timing.QueueLabel},
            {"queueFamilyIndex", timing.QueueFamilyIndex},
            {"startMs", timing.StartMs},
            {"endMs", timing.EndMs},
            {"durationMs", timing.DurationMs}
        });
    }
    return timingsJson;
}

[[nodiscard]] nlohmann::json BuildGpuQueueBreakdownJson(const std::vector<GPUProfilerQueueBreakdown>& queueBreakdown) {
    nlohmann::json queueBreakdownJson = nlohmann::json::array();
    for (const GPUProfilerQueueBreakdown& queue : queueBreakdown) {
        queueBreakdownJson.push_back({
            {"queueLabel", queue.QueueLabel},
            {"queueFamilyIndex", queue.QueueFamilyIndex},
            {"totalDurationMs", queue.TotalDurationMs},
            {"passCount", queue.PassCount}
        });
    }
    return queueBreakdownJson;
}

[[nodiscard]] nlohmann::json BuildSessionMetadataJson(const ProfilerCaptureSession& session) {
    nlohmann::json metadata;
    metadata["schemaVersion"] = 1;
    metadata["sessionId"] = session.SessionId;
    metadata["profileName"] = session.ProfileName;
    metadata["captureType"] = session.CaptureType;
    metadata["startedAtEpochMs"] = session.StartedAtEpochMs;
    metadata["endedAtEpochMs"] = session.EndedAtEpochMs;
    metadata["durationMs"] = session.DurationMs;
    metadata["completed"] = session.Completed;
    metadata["includeCpu"] = session.IncludeCpu;
    metadata["includeGpu"] = session.IncludeGpu;
    metadata["channels"] = BuildChannelsJson(session.Channels);
    return metadata;
}

[[nodiscard]] nlohmann::json BuildStageJsonTrace(const ProfilerCaptureSession& session) {
    nlohmann::json document;
    document["metadata"] = BuildSessionMetadataJson(session);
    document["gpuPassTimings"] = BuildGpuPassTimingsJson(session.GpuPassTimings);
    document["gpuQueueBreakdown"] = BuildGpuQueueBreakdownJson(session.GpuQueueBreakdown);
    return document;
}

[[nodiscard]] nlohmann::json BuildChromeTrace(const ProfilerCaptureSession& session) {
    nlohmann::json document;
    document["metadata"] = BuildSessionMetadataJson(session);
    document["traceEvents"] = nlohmann::json::array();

    for (const GPUProfilerPassTiming& timing : session.GpuPassTimings) {
        document["traceEvents"].push_back({
            {"name", timing.PassName},
            {"cat", "gpu"},
            {"ph", "X"},
            {"ts", timing.StartMs * 1000.0},
            {"dur", timing.DurationMs * 1000.0},
            {"pid", 1},
            {"tid", static_cast<uint64_t>(timing.QueueFamilyIndex)},
            {"args",
             {
                 {"queueLabel", timing.QueueLabel},
                 {"queueFamilyIndex", timing.QueueFamilyIndex}
             }}
        });
    }

    document["gpuPassTimings"] = BuildGpuPassTimingsJson(session.GpuPassTimings);
    document["gpuQueueBreakdown"] = BuildGpuQueueBreakdownJson(session.GpuQueueBreakdown);
    return document;
}

[[nodiscard]] std::string ComputeChecksum(const std::string& payload) {
    constexpr uint64_t FnvOffset = 14695981039346656037ULL;
    constexpr uint64_t FnvPrime = 1099511628211ULL;

    uint64_t hash = FnvOffset;
    for (const unsigned char byte : payload) {
        hash ^= static_cast<uint64_t>(byte);
        hash *= FnvPrime;
    }

    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << hash;
    return stream.str();
}

[[nodiscard]] bool WriteFile(const std::filesystem::path& filePath, const std::string& content) {
    std::ofstream output(filePath, std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }

    output << content;
    output.flush();
    return output.good();
}

} // namespace

Result<TraceExportResult> ExportProfilerTrace(const TraceExportRequest& request) {
    if (request.SessionId.empty() || request.OutputPath.empty()) {
        return Result<TraceExportResult>::Failure("PROFILER_INVALID_REQUEST");
    }

    const std::optional<ProfilerCaptureSession> session = ResolveSessionById(request.SessionId);
    if (!session.has_value()) {
        return Result<TraceExportResult>::Failure("PROFILER_SESSION_NOT_FOUND");
    }

    std::error_code errorCode;
    const bool outputPathExists = std::filesystem::exists(request.OutputPath, errorCode);
    if (errorCode) {
        return Result<TraceExportResult>::Failure("TRACE_EXPORT_FAILED");
    }

    if (outputPathExists && !std::filesystem::is_directory(request.OutputPath, errorCode)) {
        return Result<TraceExportResult>::Failure("PROFILER_INVALID_REQUEST");
    }
    if (errorCode) {
        return Result<TraceExportResult>::Failure("TRACE_EXPORT_FAILED");
    }

    if (!outputPathExists) {
        std::filesystem::create_directories(request.OutputPath, errorCode);
        if (errorCode) {
            return Result<TraceExportResult>::Failure("TRACE_EXPORT_FAILED");
        }
    }

    nlohmann::json traceDocument;
    if (request.Format == TraceExportFormat::ChromeTrace) {
        traceDocument = BuildChromeTrace(*session);
    } else {
        traceDocument = BuildStageJsonTrace(*session);
    }

    const std::string tracePayload = traceDocument.dump(2);
    const std::string checksum = ComputeChecksum(tracePayload);
    const uint64_t exportedAtEpochMs = GetNowEpochMilliseconds();

    const std::string formatLabel = ToString(request.Format);
    const std::filesystem::path traceArtifactPath =
        request.OutputPath / (session->SessionId + "_" + formatLabel + ".json");
    const std::filesystem::path manifestArtifactPath =
        request.OutputPath / (session->SessionId + "_manifest.json");

    if (!WriteFile(traceArtifactPath, tracePayload)) {
        return Result<TraceExportResult>::Failure("TRACE_EXPORT_FAILED");
    }

    nlohmann::json manifest;
    manifest["schemaVersion"] = 1;
    manifest["sessionId"] = session->SessionId;
    manifest["profileName"] = session->ProfileName;
    manifest["captureType"] = session->CaptureType;
    manifest["format"] = formatLabel;
    manifest["checksum"] = checksum;
    manifest["exportedAtEpochMs"] = exportedAtEpochMs;
    manifest["traceArtifactPath"] = traceArtifactPath.string();
    manifest["traceByteSize"] = static_cast<uint64_t>(tracePayload.size());

    const std::string manifestPayload = manifest.dump(2);
    if (!WriteFile(manifestArtifactPath, manifestPayload)) {
        return Result<TraceExportResult>::Failure("TRACE_EXPORT_FAILED");
    }

    TraceExportResult result{};
    result.SessionId = session->SessionId;
    result.Format = request.Format;
    result.TraceArtifactPath = traceArtifactPath;
    result.ManifestArtifactPath = manifestArtifactPath;
    result.Checksum = checksum;
    result.ExportedAtEpochMs = exportedAtEpochMs;
    result.TraceByteSize = static_cast<uint64_t>(tracePayload.size());

    return Result<TraceExportResult>::Success(std::move(result));
}

} // namespace Core::Diagnostics
