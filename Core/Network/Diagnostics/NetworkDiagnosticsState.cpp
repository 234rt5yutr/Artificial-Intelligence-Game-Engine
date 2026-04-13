#include "Core/Network/Diagnostics/NetworkDiagnosticsState.h"

#include <chrono>
#include <cstddef>
#include <sstream>

namespace Core {
namespace Network {

    namespace {

        constexpr size_t MAX_RECENT_EVENTS = 64;

        uint64_t GetNowMilliseconds() {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        }

    } // namespace

    NetworkDiagnosticsState& NetworkDiagnosticsState::Get() {
        static NetworkDiagnosticsState instance;
        return instance;
    }

    NetworkDiagnosticsSnapshot NetworkDiagnosticsState::GetSnapshot() const {
        std::scoped_lock lock(m_Mutex);
        return m_Snapshot;
    }

    void NetworkDiagnosticsState::Reset() {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot = NetworkDiagnosticsSnapshot{};
    }

    void NetworkDiagnosticsState::SetSessionState(
        const std::string& activeSessionId,
        const std::string& runtimeProfile,
        bool dedicatedRunning) {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot.ActiveSessionId = activeSessionId;
        m_Snapshot.RuntimeProfile = runtimeProfile;
        m_Snapshot.DedicatedRunning = dedicatedRunning;
    }

    void NetworkDiagnosticsState::SetLastServerTick(uint32_t tick) {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot.LastServerTick = tick;
    }

    void NetworkDiagnosticsState::SetReplayCurrentTick(uint32_t tick) {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot.ReplayCurrentTick = tick;
    }

    void NetworkDiagnosticsState::SetReplayPlaybackState(
        uint32_t currentTick,
        float seekDriftTicks,
        const std::string& playbackMode,
        uint64_t decodeMicroseconds) {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot.ReplayCurrentTick = currentTick;
        m_Snapshot.ReplaySeekDriftTicks = seekDriftTicks;
        m_Snapshot.ReplayPlaybackMode = playbackMode;
        m_Snapshot.ReplayLastDecodeMicroseconds = decodeMicroseconds;
    }

    void NetworkDiagnosticsState::SetLastRollbackTick(uint32_t tick) {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot.LastRollbackTick = tick;
    }

    void NetworkDiagnosticsState::SetRollbackSnapshotRingUsage(uint32_t snapshotCount) {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot.RollbackSnapshotRingUsage = snapshotCount;
    }

    void NetworkDiagnosticsState::SetPendingResimFrames(uint32_t frameCount) {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot.PendingResimFrames = frameCount;
    }

    void NetworkDiagnosticsState::SetLastResimulatedFrames(uint32_t frameCount) {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot.LastResimulatedFrames = frameCount;
    }

    void NetworkDiagnosticsState::SetMigrationState(const std::string& migrationState) {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot.MigrationState = migrationState;
    }

    void NetworkDiagnosticsState::SetMigrationEpoch(uint64_t epoch) {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot.MigrationEpoch = epoch;
    }

    void NetworkDiagnosticsState::SetContractHash(uint64_t contractHash) {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot.ContractHash = contractHash;
    }

    void NetworkDiagnosticsState::SetCompatibilityDowngradeActive(bool active) {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot.CompatibilityDowngradeActive = active;
    }

    void NetworkDiagnosticsState::SetRegisteredReplicationPolicies(uint32_t policyCount) {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot.RegisteredReplicationPolicies = policyCount;
    }

    void NetworkDiagnosticsState::SetRegisteredRPCContracts(uint32_t rpcCount) {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot.RegisteredRPCContracts = rpcCount;
    }

    void NetworkDiagnosticsState::IncrementContractHashMismatch() {
        std::scoped_lock lock(m_Mutex);
        ++m_Snapshot.ContractHashMismatchCount;
    }

    void NetworkDiagnosticsState::RecordInviteJoinAttempt(bool success) {
        std::scoped_lock lock(m_Mutex);
        ++m_Snapshot.InviteJoinAttempts;
        if (!success) {
            ++m_Snapshot.InviteJoinFailures;
        }
    }

    void NetworkDiagnosticsState::RecordLANDiscoveryQuery(bool timedOut) {
        std::scoped_lock lock(m_Mutex);
        ++m_Snapshot.LANDiscoveryQueries;
        if (timedOut) {
            ++m_Snapshot.LANDiscoveryTimeouts;
        }
    }

    void NetworkDiagnosticsState::RecordReplayRecordingStarted() {
        std::scoped_lock lock(m_Mutex);
        ++m_Snapshot.ReplayRecordingsStarted;
    }

    void NetworkDiagnosticsState::RecordReplayRecordingCompleted(bool success) {
        std::scoped_lock lock(m_Mutex);
        if (success) {
            ++m_Snapshot.ReplayRecordingsCompleted;
        } else {
            ++m_Snapshot.ReplayRecordingFailures;
        }
    }

    void NetworkDiagnosticsState::RecordReplayPlaybackStarted() {
        std::scoped_lock lock(m_Mutex);
        ++m_Snapshot.ReplayPlaybacksStarted;
    }

    void NetworkDiagnosticsState::RecordReplayPlaybackFailed() {
        std::scoped_lock lock(m_Mutex);
        ++m_Snapshot.ReplayPlaybackFailures;
    }

    void NetworkDiagnosticsState::RecordRollbackApplied(bool usedFallback) {
        std::scoped_lock lock(m_Mutex);
        ++m_Snapshot.RollbacksApplied;
        if (usedFallback) {
            ++m_Snapshot.RollbackFallbacks;
        }
    }

    void NetworkDiagnosticsState::RecordResimulation(uint32_t hardCorrectionCount) {
        std::scoped_lock lock(m_Mutex);
        ++m_Snapshot.ResimulationsExecuted;
        m_Snapshot.ResimulationHardCorrections += hardCorrectionCount;
    }

    void NetworkDiagnosticsState::RecordHostMigrationStarted() {
        std::scoped_lock lock(m_Mutex);
        ++m_Snapshot.HostMigrationsStarted;
    }

    void NetworkDiagnosticsState::RecordHostMigrationCompleted(bool success) {
        std::scoped_lock lock(m_Mutex);
        if (success) {
            ++m_Snapshot.HostMigrationsCompleted;
        } else {
            ++m_Snapshot.HostMigrationsFailed;
        }
    }

    void NetworkDiagnosticsState::RecordEvent(const std::string& eventText) {
        std::scoped_lock lock(m_Mutex);

        std::ostringstream stream;
        stream << "[" << GetNowMilliseconds() << "] " << eventText;
        m_Snapshot.RecentNetworkEvents.push_back(stream.str());

        if (m_Snapshot.RecentNetworkEvents.size() > MAX_RECENT_EVENTS) {
            const size_t overflowCount = m_Snapshot.RecentNetworkEvents.size() - MAX_RECENT_EVENTS;
            m_Snapshot.RecentNetworkEvents.erase(
                m_Snapshot.RecentNetworkEvents.begin(),
                m_Snapshot.RecentNetworkEvents.begin() + static_cast<std::ptrdiff_t>(overflowCount));
        }
    }

} // namespace Network
} // namespace Core

