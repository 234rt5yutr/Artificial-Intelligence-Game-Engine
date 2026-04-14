#pragma once

#include "Core/Diagnostics/ProfilerCaptureTypes.h"

#include <mutex>
#include <optional>

namespace Core::Diagnostics {

class CPUProfilerCaptureService {
public:
    Result<ProfilerCaptureSession> StartCPUProfilerCapture(const ProfilerCaptureRequest& request);
    std::optional<ProfilerCaptureSession> GetActiveSession();

private:
    void RefreshActiveSessionStateLocked(uint64_t nowEpochMs);

private:
    mutable std::mutex m_Mutex{};
    std::optional<ProfilerCaptureSession> m_ActiveSession{};
    uint64_t m_NextSessionId = 1;
};

Result<ProfilerCaptureSession> StartCPUProfilerCapture(const ProfilerCaptureRequest& request);
CPUProfilerCaptureService& GetCPUProfilerCaptureService();

} // namespace Core::Diagnostics
