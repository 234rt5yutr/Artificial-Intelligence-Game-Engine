#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Core {
namespace Network {

    constexpr const char* NET_REPLAY_ARCHIVE_CORRUPT = "NET_REPLAY_ARCHIVE_CORRUPT";
    constexpr const char* NET_REPLAY_MARKER_MISSING = "NET_REPLAY_MARKER_MISSING";

    enum class ReplayControlAction : uint8_t {
        Start = 0,
        Stop,
        Flush,
        Cancel
    };

    enum class ReplayPacketDirection : uint8_t {
        Inbound = 0,
        Outbound
    };

    struct ReplayPacketSample {
        uint32_t FrameTick = 0;
        uint64_t TimestampMicroseconds = 0;
        ReplayPacketDirection Direction = ReplayPacketDirection::Inbound;
        uint8_t Channel = 0;
        uint32_t PacketSizeBytes = 0;
        uint64_t PayloadHash = 0;
        uint32_t PacketType = 0;
    };

    struct ReplayTimelineMarker {
        uint32_t FrameTick = 0;
        uint64_t StreamOffset = 0;
        std::string MarkerType = "authoritative";
    };

    struct ReplayArchiveDescriptor {
        std::string ReplayId;
        std::string SessionId;
        uint32_t ProtocolVersion = 1;
        std::string BuildCompatibilityHash;
        std::vector<std::string> Tags;
        uint64_t ContractHash = 0;
        uint32_t StartTick = 0;
        uint32_t EndTick = 0;
        uint64_t Checksum = 0;
        std::filesystem::path ArchivePath;
        uint64_t CreatedAtUnixMs = 0;
        uint64_t PacketCount = 0;
        uint64_t MarkerCount = 0;
    };

    struct ReplayRecordRequest {
        std::string SessionId;
        std::filesystem::path OutputPath;
        std::vector<std::string> Tags;
        bool IncludeInboundPackets = true;
        bool IncludeOutboundPackets = true;
        bool IncludeAuthoritativeMarkers = true;
        std::optional<uint32_t> MaxDurationSeconds;
        ReplayControlAction Action = ReplayControlAction::Start;
    };

    struct ReplayRecordResult {
        bool Success = false;
        std::string ErrorCode;
        std::string Message;
        std::string ReplayId;
        std::filesystem::path ArchivePath;
        uint64_t PacketCount = 0;
        uint64_t MarkerCount = 0;
    };

    struct ReplayPlaybackRequest {
        std::string ReplayId;
        std::optional<uint32_t> StartTick;
        std::optional<uint32_t> SeekTick;
        std::optional<int32_t> FrameStep;
        float PlaybackSpeed = 1.0f;
        bool Paused = false;
        bool Loop = false;
    };

    struct ReplayPlaybackResult {
        bool Success = false;
        std::string ErrorCode;
        std::string Message;
        std::string ReplayId;
        uint32_t CurrentTick = 0;
        float SeekDriftTicks = 0.0f;
        bool Paused = false;
        bool Loop = false;
    };

    inline const char* ToString(ReplayControlAction action) {
        switch (action) {
            case ReplayControlAction::Start: return "start";
            case ReplayControlAction::Stop: return "stop";
            case ReplayControlAction::Flush: return "flush";
            case ReplayControlAction::Cancel: return "cancel";
            default: return "start";
        }
    }

    inline std::optional<ReplayControlAction> ParseReplayControlAction(const std::string& action) {
        if (action == "start") return ReplayControlAction::Start;
        if (action == "stop") return ReplayControlAction::Stop;
        if (action == "flush") return ReplayControlAction::Flush;
        if (action == "cancel") return ReplayControlAction::Cancel;
        return std::nullopt;
    }

} // namespace Network
} // namespace Core

