#pragma once

#include "Core/Network/Replay/ReplayTypes.h"

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Core {
namespace Network {

    class NetworkReplayRecorder {
    public:
        static NetworkReplayRecorder& Get();

        ReplayRecordResult RecordNetworkReplay(const ReplayRecordRequest& request);

        void RecordPacketSample(
            const std::string& sessionId,
            ReplayPacketDirection direction,
            uint32_t frameTick,
            uint8_t channel,
            uint32_t packetSizeBytes,
            uint64_t payloadHash,
            uint32_t packetType);

        void RecordAuthoritativeMarker(
            const std::string& sessionId,
            uint32_t frameTick,
            const std::string& markerType = "authoritative");

        bool HasCheckpointMarker(const std::string& sessionId) const;

        std::optional<ReplayArchiveDescriptor> GetArchiveDescriptor(const std::string& replayId) const;
        std::optional<std::filesystem::path> GetArchivePath(const std::string& replayId) const;
        std::vector<ReplayArchiveDescriptor> GetSessionArchives(const std::string& sessionId) const;

    private:
        struct ActiveRecording {
            ReplayArchiveDescriptor Descriptor;
            bool IncludeInboundPackets = true;
            bool IncludeOutboundPackets = true;
            bool IncludeAuthoritativeMarkers = true;
            std::optional<uint32_t> MaxDurationSeconds;
            std::vector<ReplayPacketSample> PacketSamples;
            std::vector<ReplayTimelineMarker> Markers;
        };

        ReplayRecordResult StartRecordingLocked(const ReplayRecordRequest& request);
        ReplayRecordResult StopRecordingLocked(const ReplayRecordRequest& request);
        ReplayRecordResult FlushRecordingLocked(const ReplayRecordRequest& request);
        ReplayRecordResult CancelRecordingLocked(const ReplayRecordRequest& request);

        bool FinalizeRecordingToDiskLocked(ActiveRecording& recording, std::string& errorMessage);
        uint64_t ComputeRecordingChecksum(const ActiveRecording& recording) const;
        std::filesystem::path ResolveArchivePath(const ReplayRecordRequest& request, const std::string& replayId) const;
        std::string GenerateReplayId(const std::string& sessionId, uint64_t nowMs);
        uint64_t GetNowUnixMilliseconds() const;

        mutable std::mutex m_Mutex;
        std::unordered_map<std::string, ActiveRecording> m_ActiveRecordingsBySession;
        std::unordered_map<std::string, ReplayArchiveDescriptor> m_ArchiveByReplayId;
        std::unordered_map<std::string, std::vector<std::string>> m_ArchiveIdsBySession;
    };

} // namespace Network
} // namespace Core

