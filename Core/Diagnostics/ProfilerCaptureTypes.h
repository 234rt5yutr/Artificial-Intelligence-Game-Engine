#pragma once

#include "Core/Asset/Addressables/AddressablesCatalogTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Core::Diagnostics {

template <typename T>
using Result = Core::Asset::Addressables::Result<T>;

enum class ProfilerMarkerChannel : uint8_t {
    Render = 0,
    Physics = 1,
    Network = 2,
    Ui = 3,
    Script = 4,
    Custom = 5
};

inline constexpr std::size_t PROFILER_MARKER_CHANNEL_COUNT = 6;

struct ProfilerCaptureRequest {
    std::string ProfileName;
    std::vector<ProfilerMarkerChannel> Channels;
    uint32_t DurationMs = 0;
    bool IncludeCpu = true;
    bool IncludeGpu = false;
};

struct ProfilerCaptureSession {
    std::string SessionId;
    std::string ProfileName;
    std::vector<ProfilerMarkerChannel> Channels;
    uint64_t StartedAtEpochMs = 0;
    uint64_t EndedAtEpochMs = 0;
    uint32_t DurationMs = 0;
    bool IncludeCpu = true;
    bool IncludeGpu = false;
    bool Completed = false;
    std::string CaptureType = "cpu";
};

constexpr std::array<ProfilerMarkerChannel, PROFILER_MARKER_CHANNEL_COUNT> GetAllProfilerMarkerChannels() {
    return {
        ProfilerMarkerChannel::Render,
        ProfilerMarkerChannel::Physics,
        ProfilerMarkerChannel::Network,
        ProfilerMarkerChannel::Ui,
        ProfilerMarkerChannel::Script,
        ProfilerMarkerChannel::Custom
    };
}

constexpr bool IsProfilerMarkerChannelSupported(ProfilerMarkerChannel channel) {
    switch (channel) {
    case ProfilerMarkerChannel::Render:
    case ProfilerMarkerChannel::Physics:
    case ProfilerMarkerChannel::Network:
    case ProfilerMarkerChannel::Ui:
    case ProfilerMarkerChannel::Script:
    case ProfilerMarkerChannel::Custom:
        return true;
    default:
        return false;
    }
}

const char* ToString(ProfilerMarkerChannel channel);
Result<ProfilerMarkerChannel> ProfilerMarkerChannelFromString(std::string_view channelName);

} // namespace Core::Diagnostics
