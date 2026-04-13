#pragma once

#include "Core/Network/Rollback/RollbackTypes.h"

#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Core {
namespace Network {

    class RollbackCoordinator {
    public:
        static RollbackCoordinator& Get();

        void ConfigureSnapshotWindow(size_t maxSnapshotsPerSession, uint32_t retentionFrames);
        void RecordAuthoritativeSnapshot(const RollbackSnapshotRecord& snapshot);

        RollbackSimulationResult RollbackSimulationFrame(const RollbackSimulationRequest& request);

        std::optional<RollbackSnapshotRecord> GetLatestSnapshot(const std::string& sessionId) const;
        std::vector<RollbackSnapshotRecord> GetSnapshotsForSession(const std::string& sessionId) const;

    private:
        std::optional<RollbackSnapshotRecord> ResolveSnapshotForRollback(
            const std::deque<RollbackSnapshotRecord>& snapshots,
            uint32_t targetFrameTick,
            bool allowNearestFallback,
            bool& outUsedFallback) const;

        mutable std::mutex m_Mutex;
        std::unordered_map<std::string, std::deque<RollbackSnapshotRecord>> m_SnapshotsBySession;
        size_t m_MaxSnapshotsPerSession = 256;
        uint32_t m_RetentionFrames = 240;
    };

} // namespace Network
} // namespace Core

