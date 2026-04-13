#pragma once

#include "Core/Network/NetworkPackets.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Core {
namespace Network {

    constexpr const char* NET_ROLLBACK_SNAPSHOT_UNAVAILABLE = "NET_ROLLBACK_SNAPSHOT_UNAVAILABLE";
    constexpr const char* NET_RESIM_DIVERGENCE_EXCEEDED = "NET_RESIM_DIVERGENCE_EXCEEDED";

    enum class RollbackCorrectionReason : uint8_t {
        AuthorityCorrection = 0,
        PredictionDivergence,
        LateInputArrival,
        MigrationResync
    };

    struct RollbackSnapshotRecord {
        std::string SnapshotId;
        std::string SessionId;
        uint32_t FrameTick = 0;
        uint64_t SnapshotHash = 0;
        uint32_t SnapshotSizeBytes = 0;
        uint32_t EntityCount = 0;
        uint64_t CapturedAtUnixMs = 0;
    };

    struct RollbackSimulationRequest {
        std::string SessionId;
        uint32_t TargetFrameTick = 0;
        RollbackCorrectionReason CorrectionReason = RollbackCorrectionReason::AuthorityCorrection;
        uint32_t MaxRollbackFrames = 120;
        bool AllowNearestSnapshotFallback = true;
        bool TriggerFullSyncOnFailure = true;
    };

    struct RollbackSimulationResult {
        bool Success = false;
        std::string ErrorCode;
        std::string Message;
        uint32_t TargetFrameTick = 0;
        uint32_t RestoredFrameTick = 0;
        uint32_t RewoundFrameCount = 0;
        uint64_t RestoredSnapshotHash = 0;
        uint32_t SnapshotRingUsage = 0;
        bool UsedFallbackSnapshot = false;
        bool FullSyncFallbackTriggered = false;
    };

    struct ResimulationInputRecord {
        uint32_t FrameTick = 0;
        uint32_t InputSequence = 0;
        InputSample Input{};
    };

    struct ResimulationRequest {
        std::string SessionId;
        uint32_t FromFrameTick = 0;
        uint32_t ToFrameTick = 0;
        uint32_t MaxFramesPerUpdate = 32;
        float DivergenceThreshold = 0.25f;
        bool EnableSmoothing = true;
        bool EnableHardCorrectionOnDivergence = true;
        std::vector<ResimulationInputRecord> BufferedInputs;
        std::optional<uint64_t> AuthoritativeStateHash;
    };

    struct ResimulationResult {
        bool Success = false;
        std::string ErrorCode;
        std::string Message;
        uint32_t FramesResimulated = 0;
        uint32_t HardCorrectionCount = 0;
        float DivergenceMagnitude = 0.0f;
        uint64_t PredictedStateHash = 0;
        uint64_t AuthoritativeStateHash = 0;
    };

    inline const char* ToString(RollbackCorrectionReason reason) {
        switch (reason) {
            case RollbackCorrectionReason::AuthorityCorrection: return "authority-correction";
            case RollbackCorrectionReason::PredictionDivergence: return "prediction-divergence";
            case RollbackCorrectionReason::LateInputArrival: return "late-input-arrival";
            case RollbackCorrectionReason::MigrationResync: return "migration-resync";
            default: return "authority-correction";
        }
    }

    inline std::optional<RollbackCorrectionReason> ParseRollbackCorrectionReason(const std::string& reason) {
        if (reason == "authority-correction") return RollbackCorrectionReason::AuthorityCorrection;
        if (reason == "prediction-divergence") return RollbackCorrectionReason::PredictionDivergence;
        if (reason == "late-input-arrival") return RollbackCorrectionReason::LateInputArrival;
        if (reason == "migration-resync") return RollbackCorrectionReason::MigrationResync;
        return std::nullopt;
    }

} // namespace Network
} // namespace Core

