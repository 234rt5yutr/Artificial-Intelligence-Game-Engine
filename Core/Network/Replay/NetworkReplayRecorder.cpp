#include "Core/Network/Replay/NetworkReplayRecorder.h"

#include "Core/Log.h"
#include "Core/Network/Diagnostics/NetworkDiagnosticsState.h"
#include "Core/Network/NetworkContractState.h"
#include "Core/Network/NetworkHash.h"
#include "Core/Network/NetworkPackets.h"
#include "Core/Security/PathValidator.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>

namespace Core {
namespace Network {

    namespace {

        constexpr uint32_t REPLAY_SCHEMA_VERSION = 1;

        uint64_t GetNowMilliseconds() {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        }

    } // namespace

    NetworkReplayRecorder& NetworkReplayRecorder::Get() {
        static NetworkReplayRecorder instance;
        return instance;
    }

    ReplayRecordResult NetworkReplayRecorder::RecordNetworkReplay(const ReplayRecordRequest& request) {
        std::scoped_lock lock(m_Mutex);

        switch (request.Action) {
            case ReplayControlAction::Start:
                return StartRecordingLocked(request);
            case ReplayControlAction::Stop:
                return StopRecordingLocked(request);
            case ReplayControlAction::Flush:
                return FlushRecordingLocked(request);
            case ReplayControlAction::Cancel:
                return CancelRecordingLocked(request);
            default: {
                ReplayRecordResult result;
                result.Success = false;
                result.ErrorCode = NET_REPLAY_ARCHIVE_CORRUPT;
                result.Message = "Unsupported replay recording action.";
                return result;
            }
        }
    }

    void NetworkReplayRecorder::RecordPacketSample(
        const std::string& sessionId,
        ReplayPacketDirection direction,
        uint32_t frameTick,
        uint8_t channel,
        uint32_t packetSizeBytes,
        uint64_t payloadHash,
        uint32_t packetType) {
        std::scoped_lock lock(m_Mutex);

        auto recordingIt = m_ActiveRecordingsBySession.find(sessionId);
        if (recordingIt == m_ActiveRecordingsBySession.end()) {
            return;
        }

        ActiveRecording& recording = recordingIt->second;
        if (direction == ReplayPacketDirection::Inbound && !recording.IncludeInboundPackets) {
            return;
        }
        if (direction == ReplayPacketDirection::Outbound && !recording.IncludeOutboundPackets) {
            return;
        }

        ReplayPacketSample sample;
        sample.FrameTick = frameTick;
        sample.TimestampMicroseconds = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        sample.Direction = direction;
        sample.Channel = channel;
        sample.PacketSizeBytes = packetSizeBytes;
        sample.PayloadHash = payloadHash;
        sample.PacketType = packetType;
        if (recording.PacketSamples.empty() && recording.Descriptor.StartTick == 0) {
            recording.Descriptor.StartTick = frameTick;
        }
        recording.PacketSamples.push_back(sample);
        recording.Descriptor.EndTick = std::max(recording.Descriptor.EndTick, frameTick);
    }

    void NetworkReplayRecorder::RecordAuthoritativeMarker(
        const std::string& sessionId,
        uint32_t frameTick,
        const std::string& markerType) {
        std::scoped_lock lock(m_Mutex);

        auto recordingIt = m_ActiveRecordingsBySession.find(sessionId);
        if (recordingIt == m_ActiveRecordingsBySession.end()) {
            return;
        }

        ActiveRecording& recording = recordingIt->second;
        if (!recording.IncludeAuthoritativeMarkers) {
            return;
        }

        ReplayTimelineMarker marker;
        marker.FrameTick = frameTick;
        marker.StreamOffset = recording.PacketSamples.size();
        marker.MarkerType = markerType;
        if (recording.Markers.empty() && recording.Descriptor.StartTick == 0) {
            recording.Descriptor.StartTick = frameTick;
        }
        recording.Markers.push_back(marker);
        recording.Descriptor.EndTick = std::max(recording.Descriptor.EndTick, frameTick);
    }

    bool NetworkReplayRecorder::HasCheckpointMarker(const std::string& sessionId) const {
        std::scoped_lock lock(m_Mutex);

        auto activeIt = m_ActiveRecordingsBySession.find(sessionId);
        if (activeIt != m_ActiveRecordingsBySession.end()) {
            return !activeIt->second.Markers.empty();
        }

        auto sessionArchiveIt = m_ArchiveIdsBySession.find(sessionId);
        if (sessionArchiveIt == m_ArchiveIdsBySession.end()) {
            return false;
        }

        for (const std::string& replayId : sessionArchiveIt->second) {
            auto archiveIt = m_ArchiveByReplayId.find(replayId);
            if (archiveIt != m_ArchiveByReplayId.end() && archiveIt->second.MarkerCount > 0) {
                return true;
            }
        }

        return false;
    }

    std::optional<ReplayArchiveDescriptor> NetworkReplayRecorder::GetArchiveDescriptor(const std::string& replayId) const {
        std::scoped_lock lock(m_Mutex);
        auto it = m_ArchiveByReplayId.find(replayId);
        if (it == m_ArchiveByReplayId.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<std::filesystem::path> NetworkReplayRecorder::GetArchivePath(const std::string& replayId) const {
        const std::optional<ReplayArchiveDescriptor> descriptor = GetArchiveDescriptor(replayId);
        if (!descriptor.has_value()) {
            return std::nullopt;
        }
        return descriptor->ArchivePath;
    }

    std::vector<ReplayArchiveDescriptor> NetworkReplayRecorder::GetSessionArchives(const std::string& sessionId) const {
        std::scoped_lock lock(m_Mutex);

        std::vector<ReplayArchiveDescriptor> archives;
        auto sessionArchiveIt = m_ArchiveIdsBySession.find(sessionId);
        if (sessionArchiveIt == m_ArchiveIdsBySession.end()) {
            return archives;
        }

        for (const std::string& replayId : sessionArchiveIt->second) {
            auto descriptorIt = m_ArchiveByReplayId.find(replayId);
            if (descriptorIt != m_ArchiveByReplayId.end()) {
                archives.push_back(descriptorIt->second);
            }
        }
        std::sort(archives.begin(), archives.end(), [](const ReplayArchiveDescriptor& left, const ReplayArchiveDescriptor& right) {
            return left.CreatedAtUnixMs < right.CreatedAtUnixMs;
        });
        return archives;
    }

    ReplayRecordResult NetworkReplayRecorder::StartRecordingLocked(const ReplayRecordRequest& request) {
        ReplayRecordResult result;
        if (request.SessionId.empty()) {
            result.Success = false;
            result.ErrorCode = NET_REPLAY_ARCHIVE_CORRUPT;
            result.Message = "sessionId is required to start replay recording.";
            return result;
        }

        if (m_ActiveRecordingsBySession.contains(request.SessionId)) {
            result.Success = false;
            result.ErrorCode = NET_REPLAY_ARCHIVE_CORRUPT;
            result.Message = "Replay recording already active for session.";
            return result;
        }

        const uint64_t nowMs = GetNowUnixMilliseconds();
        const std::string replayId = GenerateReplayId(request.SessionId, nowMs);

        ActiveRecording recording;
        recording.Descriptor.ReplayId = replayId;
        recording.Descriptor.SessionId = request.SessionId;
        recording.Descriptor.ProtocolVersion = NETWORK_PROTOCOL_VERSION;
        recording.Descriptor.ContractHash = GetNetworkContractHash();
        recording.Descriptor.BuildCompatibilityHash = "build-default";
        recording.Descriptor.Tags = request.Tags;
        recording.Descriptor.StartTick = 0;
        recording.Descriptor.EndTick = 0;
        recording.Descriptor.CreatedAtUnixMs = nowMs;
        recording.Descriptor.ArchivePath = ResolveArchivePath(request, replayId);
        recording.IncludeInboundPackets = request.IncludeInboundPackets;
        recording.IncludeOutboundPackets = request.IncludeOutboundPackets;
        recording.IncludeAuthoritativeMarkers = request.IncludeAuthoritativeMarkers;
        recording.MaxDurationSeconds = request.MaxDurationSeconds;

        if (recording.IncludeAuthoritativeMarkers) {
            recording.Markers.push_back(ReplayTimelineMarker{ 0, 0, "recording-start" });
        }

        m_ActiveRecordingsBySession[request.SessionId] = std::move(recording);
        NetworkDiagnosticsState::Get().RecordReplayRecordingStarted();
        NetworkDiagnosticsState::Get().RecordEvent("ReplayRecordingStarted: " + replayId);

        result.Success = true;
        result.ReplayId = replayId;
        result.Message = "Replay recording started.";
        return result;
    }

    ReplayRecordResult NetworkReplayRecorder::StopRecordingLocked(const ReplayRecordRequest& request) {
        ReplayRecordResult result;
        auto recordingIt = m_ActiveRecordingsBySession.find(request.SessionId);
        if (recordingIt == m_ActiveRecordingsBySession.end()) {
            result.Success = false;
            result.ErrorCode = NET_REPLAY_ARCHIVE_CORRUPT;
            result.Message = "No active replay recording for session.";
            return result;
        }

        ActiveRecording recording = std::move(recordingIt->second);
        m_ActiveRecordingsBySession.erase(recordingIt);

        std::string errorMessage;
        const bool writeSuccess = FinalizeRecordingToDiskLocked(recording, errorMessage);
        if (!writeSuccess) {
            NetworkDiagnosticsState::Get().RecordReplayRecordingCompleted(false);
            result.Success = false;
            result.ErrorCode = NET_REPLAY_ARCHIVE_CORRUPT;
            result.Message = errorMessage;
            return result;
        }

        m_ArchiveByReplayId[recording.Descriptor.ReplayId] = recording.Descriptor;
        m_ArchiveIdsBySession[recording.Descriptor.SessionId].push_back(recording.Descriptor.ReplayId);

        NetworkDiagnosticsState::Get().RecordReplayRecordingCompleted(true);
        NetworkDiagnosticsState::Get().RecordEvent("ReplayRecordingStopped: " + recording.Descriptor.ReplayId);

        result.Success = true;
        result.ReplayId = recording.Descriptor.ReplayId;
        result.ArchivePath = recording.Descriptor.ArchivePath;
        result.PacketCount = recording.Descriptor.PacketCount;
        result.MarkerCount = recording.Descriptor.MarkerCount;
        result.Message = "Replay recording finalized.";
        return result;
    }

    ReplayRecordResult NetworkReplayRecorder::FlushRecordingLocked(const ReplayRecordRequest& request) {
        ReplayRecordResult result;
        auto recordingIt = m_ActiveRecordingsBySession.find(request.SessionId);
        if (recordingIt == m_ActiveRecordingsBySession.end()) {
            result.Success = false;
            result.ErrorCode = NET_REPLAY_ARCHIVE_CORRUPT;
            result.Message = "No active replay recording for session.";
            return result;
        }

        std::string errorMessage;
        if (!FinalizeRecordingToDiskLocked(recordingIt->second, errorMessage)) {
            result.Success = false;
            result.ErrorCode = NET_REPLAY_ARCHIVE_CORRUPT;
            result.Message = errorMessage;
            return result;
        }

        result.Success = true;
        result.ReplayId = recordingIt->second.Descriptor.ReplayId;
        result.ArchivePath = recordingIt->second.Descriptor.ArchivePath;
        result.PacketCount = recordingIt->second.Descriptor.PacketCount;
        result.MarkerCount = recordingIt->second.Descriptor.MarkerCount;
        result.Message = "Replay recording flushed to disk.";
        return result;
    }

    ReplayRecordResult NetworkReplayRecorder::CancelRecordingLocked(const ReplayRecordRequest& request) {
        ReplayRecordResult result;
        if (m_ActiveRecordingsBySession.erase(request.SessionId) == 0) {
            result.Success = false;
            result.ErrorCode = NET_REPLAY_ARCHIVE_CORRUPT;
            result.Message = "No active replay recording for session.";
            return result;
        }

        NetworkDiagnosticsState::Get().RecordEvent("ReplayRecordingCancelled: " + request.SessionId);
        result.Success = true;
        result.Message = "Replay recording cancelled.";
        return result;
    }

    bool NetworkReplayRecorder::FinalizeRecordingToDiskLocked(ActiveRecording& recording, std::string& errorMessage) {
        recording.Descriptor.PacketCount = static_cast<uint64_t>(recording.PacketSamples.size());
        recording.Descriptor.MarkerCount = static_cast<uint64_t>(recording.Markers.size());
        recording.Descriptor.Checksum = ComputeRecordingChecksum(recording);

        std::filesystem::path outputPath = recording.Descriptor.ArchivePath;
        if (outputPath.empty()) {
            errorMessage = "Replay archive output path is empty.";
            return false;
        }

        const std::optional<std::filesystem::path> validatedPath =
            Security::PathValidator::ValidateReplayArtifactPath(outputPath);
        if (!validatedPath.has_value()) {
            errorMessage = "Replay output path failed validation.";
            return false;
        }
        outputPath = validatedPath.value();

        const std::filesystem::path outputDirectory = outputPath.parent_path();
        if (!outputDirectory.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(outputDirectory, ec);
            if (ec) {
                errorMessage = "Failed to create replay output directory.";
                return false;
            }
        }

        nlohmann::json descriptorJson = {
            {"replayId", recording.Descriptor.ReplayId},
            {"sessionId", recording.Descriptor.SessionId},
            {"protocolVersion", recording.Descriptor.ProtocolVersion},
            {"buildCompatibilityHash", recording.Descriptor.BuildCompatibilityHash},
            {"tags", recording.Descriptor.Tags},
            {"contractHash", recording.Descriptor.ContractHash},
            {"startTick", recording.Descriptor.StartTick},
            {"endTick", recording.Descriptor.EndTick},
            {"checksum", recording.Descriptor.Checksum},
            {"createdAtUnixMs", recording.Descriptor.CreatedAtUnixMs},
            {"packetCount", recording.Descriptor.PacketCount},
            {"markerCount", recording.Descriptor.MarkerCount}
        };

        nlohmann::json packetArray = nlohmann::json::array();
        for (const ReplayPacketSample& sample : recording.PacketSamples) {
            packetArray.push_back({
                {"frameTick", sample.FrameTick},
                {"timestampMicroseconds", sample.TimestampMicroseconds},
                {"direction", sample.Direction == ReplayPacketDirection::Inbound ? "inbound" : "outbound"},
                {"channel", sample.Channel},
                {"packetSizeBytes", sample.PacketSizeBytes},
                {"payloadHash", sample.PayloadHash},
                {"packetType", sample.PacketType}
            });
        }

        nlohmann::json markerArray = nlohmann::json::array();
        for (const ReplayTimelineMarker& marker : recording.Markers) {
            markerArray.push_back({
                {"frameTick", marker.FrameTick},
                {"streamOffset", marker.StreamOffset},
                {"markerType", marker.MarkerType}
            });
        }

        nlohmann::json replayJson = {
            {"schemaVersion", REPLAY_SCHEMA_VERSION},
            {"descriptor", descriptorJson},
            {"packets", packetArray},
            {"markers", markerArray}
        };

        std::ofstream outputStream(outputPath, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!outputStream.is_open()) {
            errorMessage = "Failed to open replay archive output file.";
            return false;
        }
        outputStream << replayJson.dump(2);
        if (!outputStream.good()) {
            errorMessage = "Failed while writing replay archive.";
            return false;
        }

        recording.Descriptor.ArchivePath = outputPath;
        return true;
    }

    uint64_t NetworkReplayRecorder::ComputeRecordingChecksum(const ActiveRecording& recording) const {
        uint64_t checksum = 0;
        checksum = HashCombineFNV1a(checksum, HashStringFNV1a(recording.Descriptor.ReplayId, false));
        checksum = HashCombineFNV1a(checksum, HashStringFNV1a(recording.Descriptor.SessionId, false));
        checksum = HashCombineFNV1a(checksum, recording.Descriptor.ProtocolVersion);
        checksum = HashCombineFNV1a(checksum, recording.Descriptor.ContractHash);
        checksum = HashCombineFNV1a(checksum, recording.Descriptor.StartTick);
        checksum = HashCombineFNV1a(checksum, recording.Descriptor.EndTick);
        for (const std::string& tag : recording.Descriptor.Tags) {
            checksum = HashCombineFNV1a(checksum, HashStringFNV1a(tag, true));
        }

        for (const ReplayPacketSample& sample : recording.PacketSamples) {
            checksum = HashCombineFNV1a(checksum, sample.FrameTick);
            checksum = HashCombineFNV1a(checksum, sample.TimestampMicroseconds);
            checksum = HashCombineFNV1a(checksum, static_cast<uint8_t>(sample.Direction));
            checksum = HashCombineFNV1a(checksum, sample.Channel);
            checksum = HashCombineFNV1a(checksum, sample.PacketSizeBytes);
            checksum = HashCombineFNV1a(checksum, sample.PayloadHash);
            checksum = HashCombineFNV1a(checksum, sample.PacketType);
        }

        for (const ReplayTimelineMarker& marker : recording.Markers) {
            checksum = HashCombineFNV1a(checksum, marker.FrameTick);
            checksum = HashCombineFNV1a(checksum, marker.StreamOffset);
            checksum = HashCombineFNV1a(checksum, HashStringFNV1a(marker.MarkerType, true));
        }

        return checksum;
    }

    std::filesystem::path NetworkReplayRecorder::ResolveArchivePath(
        const ReplayRecordRequest& request,
        const std::string& replayId) const {
        if (!request.OutputPath.empty()) {
            return request.OutputPath;
        }
        return std::filesystem::path("build/replays") / (replayId + ".json");
    }

    std::string NetworkReplayRecorder::GenerateReplayId(const std::string& sessionId, uint64_t nowMs) {
        const uint64_t hash = HashCombineFNV1a(HashStringFNV1a(sessionId, true), nowMs);
        return "replay-" + std::to_string(nowMs) + "-" + std::to_string(hash & 0xFFFFFULL);
    }

    uint64_t NetworkReplayRecorder::GetNowUnixMilliseconds() const {
        return GetNowMilliseconds();
    }

} // namespace Network
} // namespace Core

