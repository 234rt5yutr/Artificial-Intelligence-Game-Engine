#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Core {
namespace Network {

    constexpr const char* NET_HOST_MIGRATION_CANDIDATE_INVALID = "NET_HOST_MIGRATION_CANDIDATE_INVALID";
    constexpr const char* NET_HOST_MIGRATION_COMMIT_FAILED = "NET_HOST_MIGRATION_COMMIT_FAILED";

    enum class HostMigrationPhase : uint8_t {
        Idle = 0,
        CandidateSelection,
        TransferState,
        RebindClients,
        Commit,
        Rollback,
        Failed
    };

    struct HostMigrationCandidate {
        std::string ClientId;
        std::string EndpointAddress;
        uint16_t EndpointPort = 0;
        float HealthScore = 0.0f;
        float LatencyMs = 0.0f;
        float PerformanceScore = 0.0f;
        uint32_t LastAckTick = 0;
        bool Compatible = true;
        bool IsCurrentHost = false;
    };

    struct HostMigrationRequest {
        std::string SessionId;
        std::string CurrentHostId;
        std::vector<HostMigrationCandidate> Candidates;
        std::optional<std::string> PreferredCandidateId;
        std::optional<std::filesystem::path> MigrationArtifactPath;
        uint32_t CommitTimeoutMs = 3000;
        uint32_t RequiredAckTick = 0;
        uint32_t MaxCandidateAttempts = 3;
        bool AllowFallbackCandidates = true;
        bool RequireMigrationArtifact = false;
        bool RollbackOnCommitFailure = true;
    };

    struct HostMigrationResult {
        bool Success = false;
        std::string ErrorCode;
        std::string Message;
        std::string EpochId;
        uint64_t SessionEpoch = 0;
        std::string PreviousHostId;
        std::string SelectedHostId;
        std::string SelectedHostEndpoint;
        uint16_t SelectedHostPort = 0;
        bool UsedFallbackCandidate = false;
        bool RolledBackToPreviousHost = false;
        std::vector<std::string> FailedCandidates;
    };

    struct HostMigrationEpochRecord {
        std::string EpochId;
        std::string SessionId;
        uint64_t SessionEpoch = 0;
        std::string PreviousHostId;
        std::string NewHostId;
        HostMigrationPhase Phase = HostMigrationPhase::Idle;
        uint64_t StartedAtUnixMs = 0;
        uint64_t CompletedAtUnixMs = 0;
    };

    inline const char* ToString(HostMigrationPhase phase) {
        switch (phase) {
            case HostMigrationPhase::Idle: return "idle";
            case HostMigrationPhase::CandidateSelection: return "candidate-selection";
            case HostMigrationPhase::TransferState: return "transfer-state";
            case HostMigrationPhase::RebindClients: return "rebind-clients";
            case HostMigrationPhase::Commit: return "commit";
            case HostMigrationPhase::Rollback: return "rollback";
            case HostMigrationPhase::Failed: return "failed";
            default: return "idle";
        }
    }

} // namespace Network
} // namespace Core

