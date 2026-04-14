#include "Core/Diagnostics/GPUProfilerCapture.h"

#include <cassert>
#include <string>

namespace {

Core::Diagnostics::ProfilerCaptureRequest BuildValidGpuRequest() {
    Core::Diagnostics::ProfilerCaptureRequest request{};
    request.ProfileName = "gpu-pass-capture";
    request.Channels = {Core::Diagnostics::ProfilerMarkerChannel::Render};
    request.DurationMs = 1000;
    request.IncludeCpu = false;
    request.IncludeGpu = true;
    return request;
}

} // namespace

int main() {
    using namespace Core::Diagnostics;

#ifndef TRACY_ENABLE
    ProfilerCaptureRequest request = BuildValidGpuRequest();
    Result<ProfilerCaptureSession> result = StartGPUProfilerCapture(request);
    assert(!result.Ok);
    assert(result.Error == "PROFILER_NOT_ENABLED");
    return 0;
#else
    GPUProfilerCaptureService service;

    {
        ProfilerCaptureRequest invalidRequest = BuildValidGpuRequest();
        invalidRequest.ProfileName.clear();
        Result<ProfilerCaptureSession> invalidResult = service.StartGPUProfilerCapture(invalidRequest);
        assert(!invalidResult.Ok);
        assert(invalidResult.Error == "PROFILER_INVALID_REQUEST");
    }

    {
        ProfilerCaptureRequest invalidRequest = BuildValidGpuRequest();
        invalidRequest.IncludeCpu = true;
        Result<ProfilerCaptureSession> invalidResult = service.StartGPUProfilerCapture(invalidRequest);
        assert(!invalidResult.Ok);
        assert(invalidResult.Error == "PROFILER_INVALID_REQUEST");
    }

    {
        const ProfilerCaptureRequest request = BuildValidGpuRequest();
        const Result<ProfilerCaptureSession> firstStart = service.StartGPUProfilerCapture(request);
        assert(firstStart.Ok);
        assert(firstStart.Value.CaptureType == "gpu");
        assert(!firstStart.Value.GpuPassTimings.empty());
        assert(!firstStart.Value.GpuQueueBreakdown.empty());

        const Result<ProfilerCaptureSession> secondStart = service.StartGPUProfilerCapture(request);
        assert(!secondStart.Ok);
        assert(secondStart.Error == "PROFILER_SESSION_ACTIVE");

        const std::optional<ProfilerCaptureSession> activeSession = service.GetActiveSession();
        assert(activeSession.has_value());
        assert(activeSession->IncludeGpu);
    }

    return 0;
#endif
}
