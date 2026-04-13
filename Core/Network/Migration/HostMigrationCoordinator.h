#pragma once

#include "Core/Network/Migration/HostMigrationTypes.h"

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Core {
namespace Network {

    class HostMigrationCoordinator {
    public:
        static HostMigrationCoordinator& Get();

        HostMigrationResult MigrateHostSession(const HostMigrationRequest& request);

        std::optional<HostMigrationEpochRecord> GetLatestEpoch(const std::string& sessionId) const;
        std::vector<HostMigrationEpochRecord> GetEpochHistory(const std::string& sessionId) const;

    private:
        float ComputeCandidateScore(const HostMigrationCandidate& candidate) const;
        std::string BuildFailedCandidateMessage(
            const HostMigrationCandidate& candidate,
            const std::string& reason) const;
        uint64_t GetNowUnixMilliseconds() const;

        mutable std::mutex m_Mutex;
        std::unordered_map<std::string, std::vector<HostMigrationEpochRecord>> m_EpochsBySession;
        std::unordered_map<std::string, uint64_t> m_NextEpochBySession;
    };

} // namespace Network
} // namespace Core

