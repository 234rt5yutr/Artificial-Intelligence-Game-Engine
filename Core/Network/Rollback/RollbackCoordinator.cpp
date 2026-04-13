#include "Core/Network/Rollback/RollbackCoordinator.h"

#include "Core/Network/Diagnostics/NetworkDiagnosticsState.h"

#include <algorithm>
#include <chrono>

namespace Core {
namespace Network {

    namespace {

        uint64_t GetNowMilliseconds() {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        }

    } // namespace

    RollbackCoordinator& RollbackCoordinator::Get() {
        static RollbackCoordinator instance;
        return instance;
    }

    void RollbackCoordinator::ConfigureSnapshotWindow(size_t maxSnapshotsPerSession, uint32_t retentionFrames) {
        std::scoped_lock lock(m_Mutex);
        m_MaxSnapshotsPerSession = std::max<size_t>(16, maxSnapshotsPerSession);
        m_RetentionFrames = std::max<uint32_t>(32, retentionFrames);
    }

    void RollbackCoordinator::RecordAuthoritativeSnapshot(const RollbackSnapshotRecord& snapshot) {
        if (snapshot.SessionId.empty()) {
            return;
        }

        RollbackSnapshotRecord normalized = snapshot;
        if (normalized.SnapshotId.empty()) {
            normalized.SnapshotId =
                normalized.SessionId + "-snapshot-" + std::to_string(normalized.FrameTick);
        }
        if (normalized.CapturedAtUnixMs == 0) {
            normalized.CapturedAtUnixMs = GetNowMilliseconds();
        }

        std::scoped_lock lock(m_Mutex);
        auto& ring = m_SnapshotsBySession[normalized.SessionId];

        auto existingIt = std::find_if(ring.begin(), ring.end(), [&normalized](const RollbackSnapshotRecord& record) {
            return record.FrameTick == normalized.FrameTick;
        });
        if (existingIt != ring.end()) {
            *existingIt = normalized;
        } else {
            ring.push_back(normalized);
            std::sort(ring.begin(), ring.end(), [](const RollbackSnapshotRecord& left, const RollbackSnapshotRecord& right) {
                return left.FrameTick < right.FrameTick;
            });
        }

        if (!ring.empty()) {
            const uint32_t latestTick = ring.back().FrameTick;
            const uint32_t minTick = (latestTick > m_RetentionFrames) ? (latestTick - m_RetentionFrames) : 0;
            while (!ring.empty() && ring.front().FrameTick < minTick) {
                ring.pop_front();
            }
        }

        while (ring.size() > m_MaxSnapshotsPerSession) {
            ring.pop_front();
        }

        NetworkDiagnosticsState::Get().SetRollbackSnapshotRingUsage(static_cast<uint32_t>(ring.size()));
    }

    RollbackSimulationResult RollbackCoordinator::RollbackSimulationFrame(const RollbackSimulationRequest& request) {
        RollbackSimulationResult result;
        result.TargetFrameTick = request.TargetFrameTick;

        if (request.SessionId.empty()) {
            result.Success = false;
            result.ErrorCode = NET_ROLLBACK_SNAPSHOT_UNAVAILABLE;
            result.Message = "sessionId is required for rollback.";
            result.FullSyncFallbackTriggered = request.TriggerFullSyncOnFailure;
            return result;
        }

        std::scoped_lock lock(m_Mutex);
        auto sessionSnapshotsIt = m_SnapshotsBySession.find(request.SessionId);
        if (sessionSnapshotsIt == m_SnapshotsBySession.end() || sessionSnapshotsIt->second.empty()) {
            result.Success = false;
            result.ErrorCode = NET_ROLLBACK_SNAPSHOT_UNAVAILABLE;
            result.Message = "No rollback snapshots available for session.";
            result.FullSyncFallbackTriggered = request.TriggerFullSyncOnFailure;
            result.SnapshotRingUsage = 0;
            NetworkDiagnosticsState::Get().SetRollbackSnapshotRingUsage(0);
            NetworkDiagnosticsState::Get().RecordEvent("RollbackFullSyncFallback: " + request.SessionId + " (no snapshots)");
            return result;
        }

        const std::deque<RollbackSnapshotRecord>& snapshots = sessionSnapshotsIt->second;
        result.SnapshotRingUsage = static_cast<uint32_t>(snapshots.size());
        NetworkDiagnosticsState::Get().SetRollbackSnapshotRingUsage(result.SnapshotRingUsage);

        const uint32_t latestTick = snapshots.back().FrameTick;
        const uint32_t frameDistance = (latestTick >= request.TargetFrameTick)
            ? (latestTick - request.TargetFrameTick)
            : (request.TargetFrameTick - latestTick);
        if (frameDistance > request.MaxRollbackFrames) {
            result.Success = false;
            result.ErrorCode = NET_ROLLBACK_SNAPSHOT_UNAVAILABLE;
            result.Message = "Requested rollback exceeds max rollback window.";
            result.FullSyncFallbackTriggered = request.TriggerFullSyncOnFailure;
            NetworkDiagnosticsState::Get().RecordEvent("RollbackRejectedOutsideWindow: " + request.SessionId);
            return result;
        }

        bool usedFallbackSnapshot = false;
        const std::optional<RollbackSnapshotRecord> snapshotOpt =
            ResolveSnapshotForRollback(snapshots, request.TargetFrameTick, request.AllowNearestSnapshotFallback, usedFallbackSnapshot);
        if (!snapshotOpt.has_value()) {
            result.Success = false;
            result.ErrorCode = NET_ROLLBACK_SNAPSHOT_UNAVAILABLE;
            result.Message = "No snapshot matched the requested rollback frame.";
            result.FullSyncFallbackTriggered = request.TriggerFullSyncOnFailure;
            NetworkDiagnosticsState::Get().RecordEvent("RollbackFullSyncFallback: " + request.SessionId + " (snapshot miss)");
            return result;
        }

        const RollbackSnapshotRecord& snapshot = snapshotOpt.value();
        const uint32_t rewoundFrames =
            (request.TargetFrameTick >= snapshot.FrameTick) ? (request.TargetFrameTick - snapshot.FrameTick) : 0;
        if (rewoundFrames > request.MaxRollbackFrames) {
            result.Success = false;
            result.ErrorCode = NET_ROLLBACK_SNAPSHOT_UNAVAILABLE;
            result.Message = "Rollback snapshot is outside allowed rewind range.";
            result.FullSyncFallbackTriggered = request.TriggerFullSyncOnFailure;
            NetworkDiagnosticsState::Get().RecordEvent("RollbackRejectedOutsideRange: " + request.SessionId);
            return result;
        }

        result.Success = true;
        result.RestoredFrameTick = snapshot.FrameTick;
        result.RewoundFrameCount = rewoundFrames;
        result.RestoredSnapshotHash = snapshot.SnapshotHash;
        result.UsedFallbackSnapshot = usedFallbackSnapshot;
        result.FullSyncFallbackTriggered = false;
        result.Message = usedFallbackSnapshot
            ? "Rollback restored using nearest fallback snapshot."
            : "Rollback restored exact snapshot.";

        NetworkDiagnosticsState::Get().SetLastRollbackTick(snapshot.FrameTick);
        NetworkDiagnosticsState::Get().RecordRollbackApplied(usedFallbackSnapshot);
        NetworkDiagnosticsState::Get().RecordEvent(
            "RollbackApplied: " + request.SessionId +
            " reason=" + std::string(ToString(request.CorrectionReason)) +
            " tick=" + std::to_string(snapshot.FrameTick));
        return result;
    }

    std::optional<RollbackSnapshotRecord> RollbackCoordinator::GetLatestSnapshot(const std::string& sessionId) const {
        std::scoped_lock lock(m_Mutex);
        auto it = m_SnapshotsBySession.find(sessionId);
        if (it == m_SnapshotsBySession.end() || it->second.empty()) {
            return std::nullopt;
        }
        return it->second.back();
    }

    std::vector<RollbackSnapshotRecord> RollbackCoordinator::GetSnapshotsForSession(const std::string& sessionId) const {
        std::scoped_lock lock(m_Mutex);
        std::vector<RollbackSnapshotRecord> snapshots;
        auto it = m_SnapshotsBySession.find(sessionId);
        if (it == m_SnapshotsBySession.end()) {
            return snapshots;
        }
        snapshots.assign(it->second.begin(), it->second.end());
        return snapshots;
    }

    std::optional<RollbackSnapshotRecord> RollbackCoordinator::ResolveSnapshotForRollback(
        const std::deque<RollbackSnapshotRecord>& snapshots,
        uint32_t targetFrameTick,
        bool allowNearestFallback,
        bool& outUsedFallback) const {
        outUsedFallback = false;

        auto exactIt = std::find_if(snapshots.begin(), snapshots.end(), [targetFrameTick](const RollbackSnapshotRecord& snapshot) {
            return snapshot.FrameTick == targetFrameTick;
        });
        if (exactIt != snapshots.end()) {
            return *exactIt;
        }

        if (!allowNearestFallback) {
            return std::nullopt;
        }

        auto upperIt = std::upper_bound(snapshots.begin(), snapshots.end(), targetFrameTick, [](uint32_t tick, const RollbackSnapshotRecord& snapshot) {
            return tick < snapshot.FrameTick;
        });

        if (upperIt == snapshots.begin()) {
            return std::nullopt;
        }

        outUsedFallback = true;
        return *(upperIt - 1);
    }

} // namespace Network
} // namespace Core

