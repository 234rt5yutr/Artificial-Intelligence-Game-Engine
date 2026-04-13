#pragma once

#include "Core/Animation/AnimationRuntimeTypes.h"
#include "Core/Animation/Retarget/RetargetTypes.h"

#include <vector>

namespace Core::Animation {

RetargetAnimationResult RetargetAnimationBetweenSkeletons(
    const RetargetAnimationRequest& request);

struct RetargetBatchRequest {
    std::vector<RetargetAnimationRequest> Requests;
    bool PublishAtomically = true;
};

struct RetargetBatchItemResult {
    RetargetAnimationResult RetargetResult;
    RetargetArtifactLineage Lineage;
};

struct RetargetBatchResult {
    bool Success = false;
    std::vector<RetargetBatchItemResult> Items;
    std::vector<AnimationDiagnostic> Diagnostics;
};

RetargetBatchResult ExecuteRetargetBatch(
    const RetargetBatchRequest& request);

} // namespace Core::Animation

