#include "Core/Animation/MotionMatching/MotionMatchingRuntime.h"

#include "Core/Animation/MotionMatching/MotionSearchIndex.h"
#include "Core/Animation/MotionMatching/TrajectoryPredictor.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace Core::Animation {

namespace {

struct RegisteredMotionDatabase {
    MotionMatchingDatabaseAsset Database;
    MotionSearchIndex SearchIndex;
    std::string LastSelectedPoseId;
    float LastSelectedScore = 0.0f;
    AnimationRuntimeDiagnosticsState DiagnosticsState;
};

std::mutex g_MotionDatabaseMutex;
std::unordered_map<std::string, RegisteredMotionDatabase> g_MotionDatabases;

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

} // namespace

void RegisterMotionMatchingDatabase(
    const MotionMatchingDatabaseAsset& database) {
    std::vector<AnimationDiagnostic> diagnostics;
    if (!ValidateMotionMatchingDatabase(database, diagnostics)) {
        return;
    }

    RegisteredMotionDatabase registered;
    registered.Database = database;
    registered.SearchIndex.Build(database, diagnostics);
    registered.DiagnosticsState.ActiveMotionDatabaseId = database.DatabaseId;

    std::lock_guard<std::mutex> lock(g_MotionDatabaseMutex);
    g_MotionDatabases[database.DatabaseId] = std::move(registered);
}

void ClearMotionMatchingDatabases() {
    std::lock_guard<std::mutex> lock(g_MotionDatabaseMutex);
    g_MotionDatabases.clear();
}

MotionMatchingResult EvaluateMotionMatchingDatabase(
    const MotionMatchingQuery& query) {
    MotionMatchingResult result;

    if (query.DatabaseId.empty()) {
        PushDiagnostic(
            result.Diagnostics,
            "ANIM_MOTION_DB_MISSING",
            "Motion-matching query requires a database id.",
            DiagnosticSeverity::Error);
        result.UsedFallback = true;
        result.FallbackReason = "missing-database-id";
        return result;
    }

    std::lock_guard<std::mutex> lock(g_MotionDatabaseMutex);
    auto databaseIt = g_MotionDatabases.find(query.DatabaseId);
    if (databaseIt == g_MotionDatabases.end()) {
        PushDiagnostic(
            result.Diagnostics,
            "ANIM_MOTION_DB_MISSING",
            "Motion-matching database is not registered.",
            DiagnosticSeverity::Error,
            query.DatabaseId);
        result.UsedFallback = true;
        result.FallbackReason = "database-not-registered";
        return result;
    }

    RegisteredMotionDatabase& registered = databaseIt->second;
    const uint32_t featureDimension = registered.Database.FeatureDimension;
    std::vector<float> queryFeatures = BuildMotionQueryFeatureVector(query, featureDimension);
    if (queryFeatures.size() != featureDimension) {
        PushDiagnostic(
            result.Diagnostics,
            "ANIM_MOTION_FEATURE_DIMENSION_MISMATCH",
            "Query feature vector does not match database feature dimension.",
            DiagnosticSeverity::Error,
            query.DatabaseId);
        result.UsedFallback = true;
        result.FallbackReason = "feature-dimension-mismatch";
        return result;
    }

    MotionSearchQuery searchQuery;
    searchQuery.Features = std::move(queryFeatures);
    searchQuery.MaxCandidates = std::max(1U, query.MaxCandidates);
    searchQuery.QueryBudgetMs = query.QueryBudgetMs > 0.0f ? query.QueryBudgetMs : 0.25f;

    MotionSearchResult searchResult = registered.SearchIndex.Query(searchQuery);
    result.QueryTimeMs = searchResult.QueryTimeMs;
    result.Candidates = std::move(searchResult.Candidates);

    for (MotionMatchingCandidate& candidate : result.Candidates) {
        candidate.ContinuityPenalty = 0.0f;
        if (!query.CurrentPoseId.empty() && candidate.PoseId != query.CurrentPoseId) {
            candidate.ContinuityPenalty = query.ContinuityWeight * 0.01f;
        }
        candidate.Score = candidate.Distance + candidate.ContinuityPenalty;
    }

    std::sort(
        result.Candidates.begin(),
        result.Candidates.end(),
        [](const MotionMatchingCandidate& lhs, const MotionMatchingCandidate& rhs) {
            if (lhs.Score != rhs.Score) {
                return lhs.Score < rhs.Score;
            }
            return lhs.PoseId < rhs.PoseId;
        });

    if (result.Candidates.empty()) {
        result.UsedFallback = true;
        result.FallbackReason = "no-candidates";
        PushDiagnostic(
            result.Diagnostics,
            "ANIM_MOTION_QUERY_TIMEOUT",
            "Motion-matching query produced no candidates within the current budget.",
            DiagnosticSeverity::Warning,
            query.DatabaseId);
        return result;
    }

    MotionMatchingCandidate selectedCandidate = result.Candidates.front();
    if (!registered.LastSelectedPoseId.empty() && result.Candidates.size() > 1U) {
        const float hysteresisThreshold = 0.02f;
        const MotionMatchingCandidate& bestCandidate = result.Candidates.front();
        const MotionMatchingCandidate& secondCandidate = result.Candidates[1];
        if (secondCandidate.PoseId == registered.LastSelectedPoseId &&
            (secondCandidate.Score - bestCandidate.Score) <= hysteresisThreshold) {
            selectedCandidate = secondCandidate;
        }
    }

    result.Success = true;
    result.SelectedPoseId = selectedCandidate.PoseId;
    result.SelectedScore = selectedCandidate.Score;
    result.UsedFallback = false;
    result.FallbackReason.clear();

    if (searchResult.TimedOut) {
        PushDiagnostic(
            result.Diagnostics,
            "ANIM_MOTION_QUERY_TIMEOUT",
            "Motion-matching query hit budget; selected from partial candidate set.",
            DiagnosticSeverity::Warning,
            query.DatabaseId);
    }
    PushDiagnostic(
        result.Diagnostics,
        "ANIM_MOTION_QUERY_SUCCESS",
        "Motion-matching query selected deterministic best candidate.",
        DiagnosticSeverity::Info,
        result.SelectedPoseId);

    registered.LastSelectedPoseId = result.SelectedPoseId;
    registered.LastSelectedScore = result.SelectedScore;
    registered.DiagnosticsState.ActiveMotionDatabaseId = query.DatabaseId;
    registered.DiagnosticsState.LastMotionQueryTimeMs = result.QueryTimeMs;
    registered.DiagnosticsState.LastMotionBestScore = result.SelectedScore;
    registered.DiagnosticsState.LastMotionUsedFallback = result.UsedFallback;
    registered.DiagnosticsState.ActiveStateName = result.SelectedPoseId;
    return result;
}

AnimationRuntimeDiagnosticsState GetMotionMatchingDiagnosticsState(
    const std::string& databaseId) {
    std::lock_guard<std::mutex> lock(g_MotionDatabaseMutex);
    auto databaseIt = g_MotionDatabases.find(databaseId);
    if (databaseIt == g_MotionDatabases.end()) {
        return AnimationRuntimeDiagnosticsState{};
    }
    return databaseIt->second.DiagnosticsState;
}

} // namespace Core::Animation

