#pragma once

#include "Core/Network/Replay/ReplayTypes.h"

#include <nlohmann/json.hpp>

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Core {
namespace Network {

    struct ReplayPlaybackState {
        ReplayArchiveDescriptor Descriptor;
        std::vector<ReplayTimelineMarker> Markers;
        uint32_t CurrentTick = 0;
        float PlaybackSpeed = 1.0f;
        bool Paused = false;
        bool Loop = false;
        float SeekDriftTicks = 0.0f;
        bool Active = false;
    };

    class NetworkReplayPlayer {
    public:
        static NetworkReplayPlayer& Get();

        ReplayPlaybackResult PlayNetworkReplay(const ReplayPlaybackRequest& request);
        void TickPlayback(float deltaSeconds);

        std::optional<ReplayPlaybackState> GetPlaybackState(const std::string& replayId) const;
        std::optional<ReplayPlaybackState> GetLatestPlaybackState() const;

    private:
        uint64_t ComputeReplayChecksumFromJson(const nlohmann::json& replayJson) const;
        uint32_t ResolveStartTick(
            const std::vector<ReplayTimelineMarker>& markers,
            uint32_t requestedTick,
            float& outSeekDriftTicks,
            bool& outMarkerFallbackUsed) const;

        mutable std::mutex m_Mutex;
        std::unordered_map<std::string, ReplayPlaybackState> m_PlaybackStates;
        std::string m_LatestReplayId;
    };

} // namespace Network
} // namespace Core

