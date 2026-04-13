#include "Core/Animation/MotionMatching/MotionSearchIndex.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

namespace Core::Animation {

namespace {

void PushDiagnostic(std::vector<AnimationDiagnostic>& diagnostics,
                    const std::string& code,
                    const std::string& message,
                    const DiagnosticSeverity severity,
                    const std::string& context = {}) {
    AnimationDiagnostic diagnostic;
    diagnostic.Code = code;
    diagnostic.Message = message;
    diagnostic.Severity = severity;
    diagnostic.Context = context;
    diagnostics.push_back(std::move(diagnostic));
}

float ComputeDistanceSquared(const std::vector<float>& lhs, const std::vector<float>& rhs) {
    float distance = 0.0f;
    for (size_t i = 0; i < lhs.size(); ++i) {
        const float delta = lhs[i] - rhs[i];
        distance += delta * delta;
    }
    return distance;
}

} // namespace

bool MotionSearchIndex::Build(
    const MotionMatchingDatabaseAsset& database,
    std::vector<AnimationDiagnostic>& diagnostics) {
    diagnostics.clear();
    m_Poses.clear();
    m_FeatureDimension = database.FeatureDimension;

    if (database.Poses.empty() || database.FeatureDimension == 0) {
        PushDiagnostic(
            diagnostics,
            "ANIM_MOTION_DB_MISSING",
            "Motion search index build requires a non-empty database with valid feature dimension.",
            DiagnosticSeverity::Error,
            database.DatabaseId);
        return false;
    }

    m_Poses.reserve(database.Poses.size());
    for (const MotionPoseRecord& pose : database.Poses) {
        if (pose.Features.size() != database.FeatureDimension) {
            PushDiagnostic(
                diagnostics,
                "ANIM_MOTION_FEATURE_DIMENSION_MISMATCH",
                "Pose feature vector size does not match index feature dimension.",
                DiagnosticSeverity::Error,
                pose.PoseId);
            continue;
        }
        IndexedPose indexedPose;
        indexedPose.PoseId = pose.PoseId;
        indexedPose.Features = pose.Features;
        m_Poses.push_back(std::move(indexedPose));
    }

    std::sort(
        m_Poses.begin(),
        m_Poses.end(),
        [](const IndexedPose& lhs, const IndexedPose& rhs) {
            return lhs.PoseId < rhs.PoseId;
        });

    if (!diagnostics.empty()) {
        return false;
    }
    return true;
}

MotionSearchResult MotionSearchIndex::Query(
    const MotionSearchQuery& query) const {
    MotionSearchResult result;
    if (m_Poses.empty() || m_FeatureDimension == 0 || query.Features.size() != m_FeatureDimension) {
        return result;
    }

    const uint32_t maxCandidates = std::max(1U, query.MaxCandidates);
    const float budgetMs = query.QueryBudgetMs > 0.0f ? query.QueryBudgetMs : 0.25f;
    const auto queryStart = std::chrono::steady_clock::now();

    std::vector<MotionMatchingCandidate> scoredCandidates;
    scoredCandidates.reserve(m_Poses.size());

    for (const IndexedPose& indexedPose : m_Poses) {
        const auto now = std::chrono::steady_clock::now();
        const float elapsedMs = static_cast<float>(
            std::chrono::duration_cast<std::chrono::microseconds>(now - queryStart).count()) / 1000.0f;
        if (elapsedMs > budgetMs) {
            result.TimedOut = true;
            break;
        }

        MotionMatchingCandidate candidate;
        candidate.PoseId = indexedPose.PoseId;
        candidate.Distance = std::sqrt(ComputeDistanceSquared(query.Features, indexedPose.Features));
        candidate.ContinuityPenalty = 0.0f;
        candidate.Score = candidate.Distance;
        scoredCandidates.push_back(std::move(candidate));
    }

    std::sort(
        scoredCandidates.begin(),
        scoredCandidates.end(),
        [](const MotionMatchingCandidate& lhs, const MotionMatchingCandidate& rhs) {
            if (lhs.Score != rhs.Score) {
                return lhs.Score < rhs.Score;
            }
            return lhs.PoseId < rhs.PoseId;
        });

    if (scoredCandidates.size() > maxCandidates) {
        scoredCandidates.resize(maxCandidates);
    }

    result.Candidates = std::move(scoredCandidates);
    const auto queryEnd = std::chrono::steady_clock::now();
    result.QueryTimeMs = static_cast<float>(
        std::chrono::duration_cast<std::chrono::microseconds>(queryEnd - queryStart).count()) / 1000.0f;
    return result;
}

} // namespace Core::Animation

