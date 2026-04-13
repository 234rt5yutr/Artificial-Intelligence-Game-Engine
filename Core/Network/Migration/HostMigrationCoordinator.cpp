#include "Core/Network/Migration/HostMigrationCoordinator.h"

#include "Core/Network/Diagnostics/NetworkDiagnosticsState.h"
#include "Core/Security/PathValidator.h"

#include <algorithm>
#include <chrono>
#include <sstream>

namespace Core {
namespace Network {

    HostMigrationCoordinator& HostMigrationCoordinator::Get() {
        static HostMigrationCoordinator instance;
        return instance;
    }

    HostMigrationResult HostMigrationCoordinator::MigrateHostSession(const HostMigrationRequest& request) {
        HostMigrationResult result;
        result.PreviousHostId = request.CurrentHostId;

        if (request.SessionId.empty() || request.CurrentHostId.empty()) {
            result.Success = false;
            result.ErrorCode = NET_HOST_MIGRATION_CANDIDATE_INVALID;
            result.Message = "sessionId and currentHostId are required for host migration.";
            return result;
        }

        NetworkDiagnosticsState::Get().RecordHostMigrationStarted();
        NetworkDiagnosticsState::Get().SetMigrationState(std::string(ToString(HostMigrationPhase::CandidateSelection)));

        HostMigrationEpochRecord epoch;
        epoch.SessionId = request.SessionId;
        epoch.PreviousHostId = request.CurrentHostId;
        epoch.Phase = HostMigrationPhase::CandidateSelection;
        epoch.StartedAtUnixMs = GetNowUnixMilliseconds();

        std::scoped_lock lock(m_Mutex);
        uint64_t& nextEpoch = m_NextEpochBySession[request.SessionId];
        if (nextEpoch == 0) {
            nextEpoch = 1;
        }
        epoch.SessionEpoch = nextEpoch++;
        epoch.EpochId =
            request.SessionId + "-epoch-" + std::to_string(epoch.SessionEpoch);

        NetworkDiagnosticsState::Get().SetMigrationEpoch(epoch.SessionEpoch);

        std::vector<HostMigrationCandidate> rankedCandidates;
        rankedCandidates.reserve(request.Candidates.size());

        for (const HostMigrationCandidate& candidate : request.Candidates) {
            if (candidate.ClientId.empty() || candidate.EndpointAddress.empty() || candidate.EndpointPort == 0 ||
                candidate.IsCurrentHost || !candidate.Compatible) {
                result.FailedCandidates.push_back(BuildFailedCandidateMessage(candidate, "invalid candidate metadata"));
                continue;
            }
            rankedCandidates.push_back(candidate);
        }

        if (rankedCandidates.empty()) {
            epoch.Phase = HostMigrationPhase::Failed;
            epoch.CompletedAtUnixMs = GetNowUnixMilliseconds();
            m_EpochsBySession[request.SessionId].push_back(epoch);

            result.Success = false;
            result.ErrorCode = NET_HOST_MIGRATION_CANDIDATE_INVALID;
            result.Message = "No valid migration candidates were provided.";
            result.EpochId = epoch.EpochId;
            result.SessionEpoch = epoch.SessionEpoch;
            NetworkDiagnosticsState::Get().RecordHostMigrationCompleted(false);
            NetworkDiagnosticsState::Get().SetMigrationState(std::string(ToString(epoch.Phase)));
            NetworkDiagnosticsState::Get().RecordEvent("HostMigrationCandidateInvalid: " + request.SessionId);
            return result;
        }

        std::sort(rankedCandidates.begin(), rankedCandidates.end(), [this](const HostMigrationCandidate& left, const HostMigrationCandidate& right) {
            const float leftScore = ComputeCandidateScore(left);
            const float rightScore = ComputeCandidateScore(right);
            if (leftScore == rightScore) {
                return left.ClientId < right.ClientId;
            }
            return leftScore > rightScore;
        });

        if (request.PreferredCandidateId.has_value()) {
            auto preferredIt = std::find_if(
                rankedCandidates.begin(),
                rankedCandidates.end(),
                [&request](const HostMigrationCandidate& candidate) {
                    return candidate.ClientId == request.PreferredCandidateId.value();
                });
            if (preferredIt != rankedCandidates.end()) {
                std::rotate(rankedCandidates.begin(), preferredIt, preferredIt + 1);
            }
        }

        const uint32_t maxAttempts =
            (request.MaxCandidateAttempts == 0)
                ? static_cast<uint32_t>(rankedCandidates.size())
                : std::min<uint32_t>(request.MaxCandidateAttempts, static_cast<uint32_t>(rankedCandidates.size()));
        const float latencyBudgetMs = std::max(100.0f, static_cast<float>(request.CommitTimeoutMs) * 0.5f);

        for (uint32_t attemptIndex = 0; attemptIndex < maxAttempts; ++attemptIndex) {
            const HostMigrationCandidate& candidate = rankedCandidates[attemptIndex];

            epoch.Phase = HostMigrationPhase::TransferState;
            NetworkDiagnosticsState::Get().SetMigrationState(std::string(ToString(epoch.Phase)));

            bool artifactValid = true;
            if (request.RequireMigrationArtifact || request.MigrationArtifactPath.has_value()) {
                if (!request.MigrationArtifactPath.has_value()) {
                    artifactValid = false;
                } else {
                    const std::optional<std::filesystem::path> validatedArtifactPath =
                        Security::PathValidator::ValidateMigrationArtifactPath(request.MigrationArtifactPath.value());
                    artifactValid = validatedArtifactPath.has_value();
                }
            }

            const bool ackConverged = candidate.LastAckTick >= request.RequiredAckTick;
            const bool latencyWithinBudget = candidate.LatencyMs <= latencyBudgetMs;
            const bool candidateScoreHealthy = ComputeCandidateScore(candidate) >= 0.25f;

            if (artifactValid && ackConverged && latencyWithinBudget && candidateScoreHealthy) {
                epoch.Phase = HostMigrationPhase::RebindClients;
                NetworkDiagnosticsState::Get().SetMigrationState(std::string(ToString(epoch.Phase)));

                epoch.Phase = HostMigrationPhase::Commit;
                epoch.NewHostId = candidate.ClientId;
                epoch.CompletedAtUnixMs = GetNowUnixMilliseconds();
                m_EpochsBySession[request.SessionId].push_back(epoch);

                result.Success = true;
                result.Message = "Host migration committed.";
                result.EpochId = epoch.EpochId;
                result.SessionEpoch = epoch.SessionEpoch;
                result.SelectedHostId = candidate.ClientId;
                result.SelectedHostEndpoint = candidate.EndpointAddress;
                result.SelectedHostPort = candidate.EndpointPort;
                result.UsedFallbackCandidate = attemptIndex > 0;
                result.RolledBackToPreviousHost = false;

                NetworkDiagnosticsState::Get().RecordHostMigrationCompleted(true);
                NetworkDiagnosticsState::Get().SetMigrationState("committed");
                NetworkDiagnosticsState::Get().RecordEvent(
                    "HostMigrationCommitted: " + request.SessionId +
                    " epoch=" + std::to_string(epoch.SessionEpoch) +
                    " host=" + candidate.ClientId);
                return result;
            }

            std::ostringstream failureReason;
            if (!artifactValid) {
                failureReason << "artifact invalid; ";
            }
            if (!ackConverged) {
                failureReason << "ack tick below threshold; ";
            }
            if (!latencyWithinBudget) {
                failureReason << "latency exceeds commit budget; ";
            }
            if (!candidateScoreHealthy) {
                failureReason << "candidate score below threshold; ";
            }
            result.FailedCandidates.push_back(BuildFailedCandidateMessage(candidate, failureReason.str()));

            if (!request.AllowFallbackCandidates) {
                break;
            }
        }

        epoch.Phase = request.RollbackOnCommitFailure
            ? HostMigrationPhase::Rollback
            : HostMigrationPhase::Failed;
        epoch.CompletedAtUnixMs = GetNowUnixMilliseconds();
        m_EpochsBySession[request.SessionId].push_back(epoch);

        result.Success = false;
        result.ErrorCode = NET_HOST_MIGRATION_COMMIT_FAILED;
        result.Message = request.RollbackOnCommitFailure
            ? "Host migration failed and rolled back to previous host."
            : "Host migration failed before commit convergence.";
        result.EpochId = epoch.EpochId;
        result.SessionEpoch = epoch.SessionEpoch;
        result.RolledBackToPreviousHost = request.RollbackOnCommitFailure;

        NetworkDiagnosticsState::Get().RecordHostMigrationCompleted(false);
        NetworkDiagnosticsState::Get().SetMigrationState(std::string(ToString(epoch.Phase)));
        NetworkDiagnosticsState::Get().RecordEvent(
            "HostMigrationFailed: " + request.SessionId +
            " epoch=" + std::to_string(epoch.SessionEpoch));
        return result;
    }

    std::optional<HostMigrationEpochRecord> HostMigrationCoordinator::GetLatestEpoch(const std::string& sessionId) const {
        std::scoped_lock lock(m_Mutex);
        auto it = m_EpochsBySession.find(sessionId);
        if (it == m_EpochsBySession.end() || it->second.empty()) {
            return std::nullopt;
        }
        return it->second.back();
    }

    std::vector<HostMigrationEpochRecord> HostMigrationCoordinator::GetEpochHistory(const std::string& sessionId) const {
        std::scoped_lock lock(m_Mutex);
        auto it = m_EpochsBySession.find(sessionId);
        if (it == m_EpochsBySession.end()) {
            return {};
        }
        return it->second;
    }

    float HostMigrationCoordinator::ComputeCandidateScore(const HostMigrationCandidate& candidate) const {
        const float normalizedHealth = std::clamp(candidate.HealthScore, 0.0f, 1.0f);
        const float normalizedPerformance = std::clamp(candidate.PerformanceScore, 0.0f, 1.0f);
        const float normalizedLatency = 1.0f / (1.0f + std::max(0.0f, candidate.LatencyMs));
        return normalizedHealth * 0.45f + normalizedPerformance * 0.35f + normalizedLatency * 0.20f;
    }

    std::string HostMigrationCoordinator::BuildFailedCandidateMessage(
        const HostMigrationCandidate& candidate,
        const std::string& reason) const {
        const std::string candidateId = candidate.ClientId.empty() ? "<unknown>" : candidate.ClientId;
        return candidateId + ": " + reason;
    }

    uint64_t HostMigrationCoordinator::GetNowUnixMilliseconds() const {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

} // namespace Network
} // namespace Core

