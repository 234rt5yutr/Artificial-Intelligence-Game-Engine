#pragma once

#include "Core/Animation/AnimationRuntimeTypes.h"
#include "Core/Animation/MotionMatching/MotionMatchingDatabase.h"

#include <cstdint>
#include <vector>

namespace Core::Animation {

struct MotionSearchQuery {
    std::vector<float> Features;
    uint32_t MaxCandidates = 32;
    float QueryBudgetMs = 0.25f;
};

struct MotionSearchResult {
    bool TimedOut = false;
    float QueryTimeMs = 0.0f;
    std::vector<MotionMatchingCandidate> Candidates;
};

class MotionSearchIndex {
public:
    bool Build(
        const MotionMatchingDatabaseAsset& database,
        std::vector<AnimationDiagnostic>& diagnostics);

    MotionSearchResult Query(
        const MotionSearchQuery& query) const;

    uint32_t GetFeatureDimension() const { return m_FeatureDimension; }
    uint32_t GetPoseCount() const { return static_cast<uint32_t>(m_Poses.size()); }

private:
    struct IndexedPose {
        std::string PoseId;
        std::vector<float> Features;
    };

    std::vector<IndexedPose> m_Poses;
    uint32_t m_FeatureDimension = 0;
};

} // namespace Core::Animation

