#include "Core/Diagnostics/CPUProfilerCapture.h"
#include "Core/Diagnostics/GPUProfilerCapture.h"
#include "Core/Diagnostics/TraceExporter.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

Core::Diagnostics::ProfilerCaptureRequest BuildValidCpuRequest() {
    Core::Diagnostics::ProfilerCaptureRequest request{};
    request.ProfileName = "cpu-export-capture";
    request.Channels = {Core::Diagnostics::ProfilerMarkerChannel::Render};
    request.DurationMs = 1200;
    request.IncludeCpu = true;
    request.IncludeGpu = false;
    return request;
}

Core::Diagnostics::ProfilerCaptureRequest BuildValidGpuRequest() {
    Core::Diagnostics::ProfilerCaptureRequest request{};
    request.ProfileName = "gpu-export-capture";
    request.Channels = {Core::Diagnostics::ProfilerMarkerChannel::Render};
    request.DurationMs = 1200;
    request.IncludeCpu = false;
    request.IncludeGpu = true;
    return request;
}

std::filesystem::path BuildExportDirectory(const std::string& suffix) {
    return std::filesystem::path("build") / "diagnostics" / "trace-export-tests" / suffix;
}

nlohmann::json ReadJsonFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    assert(input.is_open());
    return nlohmann::json::parse(input);
}

} // namespace

int main() {
    using namespace Core::Diagnostics;

    {
        TraceExportRequest invalidRequest{};
        invalidRequest.Format = TraceExportFormat::Json;
        invalidRequest.OutputPath = BuildExportDirectory("invalid");

        const Result<TraceExportResult> invalidResult = ExportProfilerTrace(invalidRequest);
        assert(!invalidResult.Ok);
        assert(invalidResult.Error == "PROFILER_INVALID_REQUEST");
    }

    {
        TraceExportRequest missingSessionRequest{};
        missingSessionRequest.SessionId = "missing-session";
        missingSessionRequest.Format = TraceExportFormat::Json;
        missingSessionRequest.OutputPath = BuildExportDirectory("missing");

        const Result<TraceExportResult> missingResult = ExportProfilerTrace(missingSessionRequest);
        assert(!missingResult.Ok);
        assert(missingResult.Error == "PROFILER_SESSION_NOT_FOUND");
    }

#ifndef TRACY_ENABLE
    return 0;
#else
    std::error_code errorCode;

    {
        const std::filesystem::path outputPath = BuildExportDirectory("cpu-json");
        std::filesystem::remove_all(outputPath, errorCode);

        const Result<ProfilerCaptureSession> captureResult = StartCPUProfilerCapture(BuildValidCpuRequest());
        assert(captureResult.Ok);

        TraceExportRequest request{};
        request.SessionId = captureResult.Value.SessionId;
        request.Format = TraceExportFormat::Json;
        request.OutputPath = outputPath;

        const Result<TraceExportResult> exportResult = ExportProfilerTrace(request);
        assert(exportResult.Ok);
        assert(!exportResult.Value.Checksum.empty());
        assert(std::filesystem::exists(exportResult.Value.TraceArtifactPath));
        assert(std::filesystem::exists(exportResult.Value.ManifestArtifactPath));

        const nlohmann::json traceJson = ReadJsonFile(exportResult.Value.TraceArtifactPath);
        assert(traceJson["metadata"]["sessionId"] == captureResult.Value.SessionId);
        assert(traceJson["metadata"]["captureType"] == "cpu");
        assert(traceJson["metadata"]["channels"].is_array());

        const nlohmann::json manifestJson = ReadJsonFile(exportResult.Value.ManifestArtifactPath);
        assert(manifestJson["checksum"] == exportResult.Value.Checksum);
        assert(manifestJson["format"] == "json");
    }

    {
        const std::filesystem::path outputPath = BuildExportDirectory("gpu-chrome");
        std::filesystem::remove_all(outputPath, errorCode);

        const Result<ProfilerCaptureSession> captureResult = StartGPUProfilerCapture(BuildValidGpuRequest());
        assert(captureResult.Ok);

        TraceExportRequest request{};
        request.SessionId = captureResult.Value.SessionId;
        request.Format = TraceExportFormat::ChromeTrace;
        request.OutputPath = outputPath;

        const Result<TraceExportResult> exportResult = ExportProfilerTrace(request);
        assert(exportResult.Ok);
        assert(std::filesystem::exists(exportResult.Value.TraceArtifactPath));
        assert(std::filesystem::exists(exportResult.Value.ManifestArtifactPath));

        const nlohmann::json traceJson = ReadJsonFile(exportResult.Value.TraceArtifactPath);
        assert(traceJson["metadata"]["sessionId"] == captureResult.Value.SessionId);
        assert(traceJson["metadata"]["captureType"] == "gpu");
        assert(traceJson["traceEvents"].is_array());
        assert(!traceJson["gpuPassTimings"].empty());
        assert(!traceJson["gpuQueueBreakdown"].empty());

        const nlohmann::json manifestJson = ReadJsonFile(exportResult.Value.ManifestArtifactPath);
        assert(manifestJson["format"] == "chrome_trace");
    }

    return 0;
#endif
}
