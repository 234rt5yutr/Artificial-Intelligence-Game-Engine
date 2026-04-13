#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace Core {
namespace Network {

    struct NetworkDiagnosticsSnapshot {
        std::string ActiveSessionId;
        std::string RuntimeProfile = "client";
        bool DedicatedRunning = false;
        uint32_t LastServerTick = 0;
        uint32_t ReplayCurrentTick = 0;
        float ReplaySeekDriftTicks = 0.0f;
        std::string ReplayPlaybackMode = "stopped";
        uint64_t ReplayLastDecodeMicroseconds = 0;
        uint32_t LastRollbackTick = 0;
        uint32_t RollbackSnapshotRingUsage = 0;
        uint32_t PendingResimFrames = 0;
        uint32_t LastResimulatedFrames = 0;
        std::string MigrationState = "idle";
        uint64_t MigrationEpoch = 0;
        std::vector<std::string> RecentNetworkEvents;
        uint64_t ContractHash = 0;
        bool CompatibilityDowngradeActive = false;
        uint32_t RegisteredReplicationPolicies = 0;
        uint32_t RegisteredRPCContracts = 0;
        uint32_t ContractHashMismatchCount = 0;
        uint64_t InviteJoinAttempts = 0;
        uint64_t InviteJoinFailures = 0;
        uint64_t LANDiscoveryQueries = 0;
        uint64_t LANDiscoveryTimeouts = 0;
        uint64_t ReplayRecordingsStarted = 0;
        uint64_t ReplayRecordingsCompleted = 0;
        uint64_t ReplayRecordingFailures = 0;
        uint64_t ReplayPlaybacksStarted = 0;
        uint64_t ReplayPlaybackFailures = 0;
        uint64_t RollbacksApplied = 0;
        uint64_t RollbackFallbacks = 0;
        uint64_t ResimulationsExecuted = 0;
        uint64_t ResimulationHardCorrections = 0;
        uint64_t HostMigrationsStarted = 0;
        uint64_t HostMigrationsCompleted = 0;
        uint64_t HostMigrationsFailed = 0;
    };

    class NetworkDiagnosticsState {
    public:
        static NetworkDiagnosticsState& Get();

        NetworkDiagnosticsSnapshot GetSnapshot() const;

        void Reset();

        void SetSessionState(const std::string& activeSessionId, const std::string& runtimeProfile, bool dedicatedRunning);
        void SetLastServerTick(uint32_t tick);
        void SetReplayCurrentTick(uint32_t tick);
        void SetReplayPlaybackState(uint32_t currentTick, float seekDriftTicks, const std::string& playbackMode, uint64_t decodeMicroseconds);
        void SetLastRollbackTick(uint32_t tick);
        void SetRollbackSnapshotRingUsage(uint32_t snapshotCount);
        void SetPendingResimFrames(uint32_t frameCount);
        void SetLastResimulatedFrames(uint32_t frameCount);
        void SetMigrationState(const std::string& migrationState);
        void SetMigrationEpoch(uint64_t epoch);
        void SetContractHash(uint64_t contractHash);
        void SetCompatibilityDowngradeActive(bool active);
        void SetRegisteredReplicationPolicies(uint32_t policyCount);
        void SetRegisteredRPCContracts(uint32_t rpcCount);

        void IncrementContractHashMismatch();
        void RecordInviteJoinAttempt(bool success);
        void RecordLANDiscoveryQuery(bool timedOut);
        void RecordReplayRecordingStarted();
        void RecordReplayRecordingCompleted(bool success);
        void RecordReplayPlaybackStarted();
        void RecordReplayPlaybackFailed();
        void RecordRollbackApplied(bool usedFallback);
        void RecordResimulation(uint32_t hardCorrectionCount);
        void RecordHostMigrationStarted();
        void RecordHostMigrationCompleted(bool success);
        void RecordEvent(const std::string& eventText);

    private:
        mutable std::mutex m_Mutex;
        NetworkDiagnosticsSnapshot m_Snapshot;
    };

} // namespace Network
} // namespace Core

