#pragma once

#include "Core/Animation/AnimationRuntimeTypes.h"
#include "Core/Animation/Retarget/AnimationRetargeter.h"

#include <vector>

namespace Core::Animation {

BakeControlRigResult BakeControlRigToAnimation(
    const BakeControlRigRequest& request);

struct RetargetRigBakeBatchJob {
    RetargetAnimationRequest RetargetRequest;
    bool RunRigBake = true;
    std::string RigId;
    float SampleRateHz = 30.0f;
    float StartTimeSec = 0.0f;
    float EndTimeSec = 0.0f;
    float KeyReductionTolerance = 0.0f;
};

struct RetargetRigBakeBatchRequest {
    std::vector<RetargetRigBakeBatchJob> Jobs;
    bool PublishAtomically = true;
};

struct RetargetRigBakeBatchItemResult {
    RetargetAnimationResult RetargetResult;
    BakeControlRigResult BakeResult;
    RetargetArtifactLineage Lineage;
};

struct RetargetRigBakeBatchResult {
    bool Success = false;
    std::vector<RetargetRigBakeBatchItemResult> Items;
    std::vector<AnimationDiagnostic> Diagnostics;
};

RetargetRigBakeBatchResult ExecuteRetargetRigBakeBatch(
    const RetargetRigBakeBatchRequest& request);

} // namespace Core::Animation

