#pragma once

#include "Core/Diagnostics/ProfilerCaptureTypes.h"
#include "Core/RHI/Vulkan/VulkanContext.h"

#include <mutex>
#include <optional>

namespace Core::Diagnostics {

class GPUProfilerCaptureService {
public:
    void SetVulkanContext(Core::RHI::VulkanContext* context);
    Result<ProfilerCaptureSession> StartGPUProfilerCapture(const ProfilerCaptureRequest& request);
    std::optional<ProfilerCaptureSession> GetActiveSession();

private:
    void RefreshActiveSessionStateLocked(uint64_t nowEpochMs);

private:
    mutable std::mutex m_Mutex{};
    Core::RHI::VulkanContext* m_VulkanContext = nullptr;
    std::optional<ProfilerCaptureSession> m_ActiveSession{};
    uint64_t m_NextSessionId = 1;
};

Result<ProfilerCaptureSession> StartGPUProfilerCapture(const ProfilerCaptureRequest& request);
GPUProfilerCaptureService& GetGPUProfilerCaptureService();

} // namespace Core::Diagnostics
