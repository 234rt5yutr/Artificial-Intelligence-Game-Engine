#include "Core/Network/Replay/NetworkReplayPlayer.h"

#include "Core/Network/Diagnostics/NetworkDiagnosticsState.h"
#include "Core/Network/NetworkHash.h"
#include "Core/Network/Replay/NetworkReplayRecorder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>

namespace Core {
namespace Network {

    NetworkReplayPlayer& NetworkReplayPlayer::Get() {
        static NetworkReplayPlayer instance;
        return instance;
    }

    ReplayPlaybackResult NetworkReplayPlayer::PlayNetworkReplay(const ReplayPlaybackRequest& request) {
        ReplayPlaybackResult result;
        result.ReplayId = request.ReplayId;
        result.Paused = request.Paused;
        result.Loop = request.Loop;

        const std::optional<ReplayArchiveDescriptor> descriptorOpt =
            NetworkReplayRecorder::Get().GetArchiveDescriptor(request.ReplayId);
        if (!descriptorOpt.has_value()) {
            result.Success = false;
            result.ErrorCode = NET_REPLAY_ARCHIVE_CORRUPT;
            result.Message = "Replay archive metadata not found.";
            NetworkDiagnosticsState::Get().RecordReplayPlaybackFailed();
            return result;
        }

        const ReplayArchiveDescriptor& descriptor = descriptorOpt.value();
        std::ifstream inputStream(descriptor.ArchivePath, std::ios::in | std::ios::binary);
        if (!inputStream.is_open()) {
            result.Success = false;
            result.ErrorCode = NET_REPLAY_ARCHIVE_CORRUPT;
            result.Message = "Failed to open replay archive file.";
            NetworkDiagnosticsState::Get().RecordReplayPlaybackFailed();
            return result;
        }

        const uint64_t decodeStartUs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        nlohmann::json replayJson;
        try {
            inputStream >> replayJson;
        } catch (const nlohmann::json::parse_error&) {
            result.Success = false;
            result.ErrorCode = NET_REPLAY_ARCHIVE_CORRUPT;
            result.Message = "Replay archive contains invalid JSON.";
            NetworkDiagnosticsState::Get().RecordReplayPlaybackFailed();
            return result;
        }

        const uint64_t loadedChecksum = replayJson["descriptor"].value("checksum", 0ULL);
        const uint64_t computedChecksum = ComputeReplayChecksumFromJson(replayJson);
        if (loadedChecksum == 0 || computedChecksum != loadedChecksum) {
            result.Success = false;
            result.ErrorCode = NET_REPLAY_ARCHIVE_CORRUPT;
            result.Message = "Replay archive checksum mismatch.";
            NetworkDiagnosticsState::Get().RecordReplayPlaybackFailed();
            return result;
        }

        std::vector<ReplayTimelineMarker> markers;
        if (replayJson.contains("markers") && replayJson["markers"].is_array()) {
            for (const auto& markerJson : replayJson["markers"]) {
                ReplayTimelineMarker marker;
                marker.FrameTick = markerJson.value("frameTick", 0U);
                marker.StreamOffset = markerJson.value("streamOffset", 0ULL);
                marker.MarkerType = markerJson.value("markerType", std::string("authoritative"));
                markers.push_back(marker);
            }
        }
        std::sort(markers.begin(), markers.end(), [](const ReplayTimelineMarker& left, const ReplayTimelineMarker& right) {
            return left.FrameTick < right.FrameTick;
        });

        const uint64_t decodeEndUs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        const uint64_t decodeDurationUs = (decodeEndUs >= decodeStartUs) ? (decodeEndUs - decodeStartUs) : 0;

        uint32_t requestedTick = request.SeekTick.value_or(request.StartTick.value_or(descriptor.StartTick));
        bool markerFallbackUsed = false;
        float seekDrift = 0.0f;
        const uint32_t resolvedTick = ResolveStartTick(markers, requestedTick, seekDrift, markerFallbackUsed);
        uint32_t playbackTick = resolvedTick;

        if (request.FrameStep.has_value() && request.FrameStep.value() != 0) {
            const int64_t shiftedTick = static_cast<int64_t>(resolvedTick) + static_cast<int64_t>(request.FrameStep.value());
            const int64_t boundedTick = std::clamp<int64_t>(
                shiftedTick,
                static_cast<int64_t>(descriptor.StartTick),
                static_cast<int64_t>(descriptor.EndTick));
            playbackTick = static_cast<uint32_t>(boundedTick);
            seekDrift += static_cast<float>(requestedTick) - static_cast<float>(playbackTick);
        }

        ReplayPlaybackState playbackState;
        playbackState.Descriptor = descriptor;
        playbackState.Markers = markers;
        playbackState.CurrentTick = playbackTick;
        playbackState.PlaybackSpeed = std::max(0.1f, request.PlaybackSpeed);
        playbackState.Paused = request.Paused || (request.FrameStep.has_value() && request.FrameStep.value() != 0);
        playbackState.Loop = request.Loop;
        playbackState.SeekDriftTicks = seekDrift;
        playbackState.Active = true;

        {
            std::scoped_lock lock(m_Mutex);
            m_PlaybackStates[request.ReplayId] = playbackState;
            m_LatestReplayId = request.ReplayId;
        }

        NetworkDiagnosticsState::Get().RecordReplayPlaybackStarted();
        NetworkDiagnosticsState::Get().SetReplayPlaybackState(
            playbackTick,
            seekDrift,
            playbackState.Paused ? "paused" : "playing",
            decodeDurationUs);
        if (markerFallbackUsed) {
            NetworkDiagnosticsState::Get().RecordEvent("ReplayMarkerFallback: " + request.ReplayId);
        } else {
            NetworkDiagnosticsState::Get().RecordEvent("ReplayPlaybackStarted: " + request.ReplayId);
        }

        result.Success = true;
        result.CurrentTick = playbackTick;
        result.SeekDriftTicks = seekDrift;
        result.Paused = playbackState.Paused;
        result.Loop = playbackState.Loop;
        result.Message = markerFallbackUsed
            ? "Replay playback started with nearest marker fallback."
            : "Replay playback started.";
        if (request.FrameStep.has_value() && request.FrameStep.value() != 0) {
            result.Message = "Replay frame-step applied.";
        }
        if (markerFallbackUsed) {
            result.ErrorCode = NET_REPLAY_MARKER_MISSING;
        }
        return result;
    }

    void NetworkReplayPlayer::TickPlayback(float deltaSeconds) {
        std::scoped_lock lock(m_Mutex);

        for (auto& [replayId, state] : m_PlaybackStates) {
            if (!state.Active || state.Paused) {
                continue;
            }

            const float effectiveDelta = std::max(0.0f, deltaSeconds);
            const float tickAdvance = effectiveDelta * 60.0f * std::max(0.1f, state.PlaybackSpeed);
            const uint32_t advance = std::max(1U, static_cast<uint32_t>(std::round(tickAdvance)));
            state.CurrentTick += advance;

            if (state.CurrentTick > state.Descriptor.EndTick) {
                if (state.Loop) {
                    state.CurrentTick = state.Descriptor.StartTick;
                } else {
                    state.CurrentTick = state.Descriptor.EndTick;
                    state.Active = false;
                }
            }

            const std::string playbackMode = !state.Active ? "stopped" : (state.Paused ? "paused" : "playing");
            NetworkDiagnosticsState::Get().SetReplayPlaybackState(
                state.CurrentTick,
                state.SeekDriftTicks,
                playbackMode,
                0);
            (void)replayId;
        }
    }

    std::optional<ReplayPlaybackState> NetworkReplayPlayer::GetPlaybackState(const std::string& replayId) const {
        std::scoped_lock lock(m_Mutex);
        auto it = m_PlaybackStates.find(replayId);
        if (it == m_PlaybackStates.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<ReplayPlaybackState> NetworkReplayPlayer::GetLatestPlaybackState() const {
        std::scoped_lock lock(m_Mutex);
        if (m_LatestReplayId.empty()) {
            return std::nullopt;
        }
        auto it = m_PlaybackStates.find(m_LatestReplayId);
        if (it == m_PlaybackStates.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    uint64_t NetworkReplayPlayer::ComputeReplayChecksumFromJson(const nlohmann::json& replayJson) const {
        uint64_t checksum = 0;

        const nlohmann::json descriptorJson = replayJson.value("descriptor", nlohmann::json::object());
        checksum = HashCombineFNV1a(checksum, HashStringFNV1a(descriptorJson.value("replayId", std::string{}), false));
        checksum = HashCombineFNV1a(checksum, HashStringFNV1a(descriptorJson.value("sessionId", std::string{}), false));
        checksum = HashCombineFNV1a(checksum, descriptorJson.value("protocolVersion", 0U));
        checksum = HashCombineFNV1a(checksum, descriptorJson.value("contractHash", 0ULL));
        checksum = HashCombineFNV1a(checksum, descriptorJson.value("startTick", 0U));
        checksum = HashCombineFNV1a(checksum, descriptorJson.value("endTick", 0U));
        const nlohmann::json tagArray = descriptorJson.value("tags", nlohmann::json::array());
        for (const auto& tagJson : tagArray) {
            if (tagJson.is_string()) {
                checksum = HashCombineFNV1a(checksum, HashStringFNV1a(tagJson.get<std::string>(), true));
            }
        }

        const nlohmann::json packetArray = replayJson.value("packets", nlohmann::json::array());
        for (const auto& packetJson : packetArray) {
            checksum = HashCombineFNV1a(checksum, packetJson.value("frameTick", 0U));
            checksum = HashCombineFNV1a(checksum, packetJson.value("timestampMicroseconds", 0ULL));
            checksum = HashCombineFNV1a(checksum, HashStringFNV1a(packetJson.value("direction", std::string{}), true));
            checksum = HashCombineFNV1a(checksum, packetJson.value("channel", 0U));
            checksum = HashCombineFNV1a(checksum, packetJson.value("packetSizeBytes", 0U));
            checksum = HashCombineFNV1a(checksum, packetJson.value("payloadHash", 0ULL));
            checksum = HashCombineFNV1a(checksum, packetJson.value("packetType", 0U));
        }

        const nlohmann::json markerArray = replayJson.value("markers", nlohmann::json::array());
        for (const auto& markerJson : markerArray) {
            checksum = HashCombineFNV1a(checksum, markerJson.value("frameTick", 0U));
            checksum = HashCombineFNV1a(checksum, markerJson.value("streamOffset", 0ULL));
            checksum = HashCombineFNV1a(checksum, HashStringFNV1a(markerJson.value("markerType", std::string{}), true));
        }

        return checksum;
    }

    uint32_t NetworkReplayPlayer::ResolveStartTick(
        const std::vector<ReplayTimelineMarker>& markers,
        uint32_t requestedTick,
        float& outSeekDriftTicks,
        bool& outMarkerFallbackUsed) const {
        outSeekDriftTicks = 0.0f;
        outMarkerFallbackUsed = false;

        if (markers.empty()) {
            return requestedTick;
        }

        auto exactIt = std::find_if(markers.begin(), markers.end(), [requestedTick](const ReplayTimelineMarker& marker) {
            return marker.FrameTick == requestedTick;
        });
        if (exactIt != markers.end()) {
            return exactIt->FrameTick;
        }

        auto upperIt = std::upper_bound(markers.begin(), markers.end(), requestedTick, [](uint32_t tick, const ReplayTimelineMarker& marker) {
            return tick < marker.FrameTick;
        });

        uint32_t fallbackTick = markers.front().FrameTick;
        if (upperIt != markers.begin()) {
            fallbackTick = (upperIt - 1)->FrameTick;
        }

        outSeekDriftTicks = static_cast<float>(requestedTick) - static_cast<float>(fallbackTick);
        outMarkerFallbackUsed = true;
        return fallbackTick;
    }

} // namespace Network
} // namespace Core

