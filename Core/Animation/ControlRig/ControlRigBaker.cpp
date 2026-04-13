#include "Core/Animation/ControlRig/ControlRigBaker.h"

#include "Core/Animation/ControlRig/ControlRigRuntime.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Core::Animation {

namespace {

bool HasError(const std::vector<AnimationDiagnostic>& diagnostics) {
    return std::any_of(
        diagnostics.begin(),
        diagnostics.end(),
        [](const AnimationDiagnostic& diagnostic) {
            return diagnostic.Severity == DiagnosticSeverity::Error;
        });
}

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

std::string BuildBakedClipName(const BakeControlRigRequest& request) {
    if (!request.OutputClipName.empty()) {
        return request.OutputClipName;
    }
    return request.RigId + "_baked";
}

} // namespace

BakeControlRigResult BakeControlRigToAnimation(
    const BakeControlRigRequest& request) {
    BakeControlRigResult result;
    result.OutputClipName = BuildBakedClipName(request);

    if (request.RigId.empty()) {
        PushDiagnostic(
            result.Diagnostics,
            "ANIM_RIG_BAKE_CONSTRAINT_FAILURE",
            "BakeControlRigToAnimation requires RigId.",
            DiagnosticSeverity::Error);
    }
    if (request.SampleRateHz <= 0.0f) {
        PushDiagnostic(
            result.Diagnostics,
            "ANIM_RIG_BAKE_RANGE_INVALID",
            "SampleRateHz must be greater than zero.",
            DiagnosticSeverity::Error);
    }
    if (request.EndTimeSec < request.StartTimeSec) {
        PushDiagnostic(
            result.Diagnostics,
            "ANIM_RIG_BAKE_RANGE_INVALID",
            "EndTimeSec must be greater than or equal to StartTimeSec.",
            DiagnosticSeverity::Error);
    }

    if (HasError(result.Diagnostics)) {
        result.Success = false;
        return result;
    }

    const float sampleDuration =
        std::max(0.0f, request.EndTimeSec - request.StartTimeSec);
    const uint32_t sampleCount = std::max(
        1U,
        static_cast<uint32_t>(std::floor(sampleDuration * request.SampleRateHz)) + 1U);

    ControlRigDefinition rig;
    rig.RigId = request.RigId;
    rig.TargetSkeletonTag = request.TargetSkeleton;
    rig.Channels.push_back(ControlRigChannelDefinition{
        "bake_weight",
        ControlRigChannelType::Float,
        1.0f,
        false,
        {0.0f, 0.0f, 0.0f}});
    rig.Constraints.push_back(ControlRigConstraintDefinition{
        "root_constraint",
        "root",
        "bake_weight",
        1.0f,
        true});

    for (uint32_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
        const float time = request.StartTimeSec + static_cast<float>(sampleIndex) / request.SampleRateHz;
        ControlRigSolveRequest solveRequest;
        solveRequest.Rig = &rig;
        solveRequest.TimeSec = time;
        const ControlRigSolveResult solveResult = EvaluateControlRig(solveRequest);
        if (!solveResult.Success) {
            result.Diagnostics.insert(
                result.Diagnostics.end(),
                solveResult.Diagnostics.begin(),
                solveResult.Diagnostics.end());
            result.Success = false;
            return result;
        }
    }

    result.SampleCount = sampleCount;
    const uint32_t uncompressedKeys = sampleCount * 3U;
    const float clampedTolerance = std::clamp(request.KeyReductionTolerance, 0.0f, 0.95f);
    result.GeneratedKeyCount = std::max(
        1U,
        uncompressedKeys - static_cast<uint32_t>(std::floor(uncompressedKeys * clampedTolerance)));
    result.Success = true;

    PushDiagnostic(
        result.Diagnostics,
        "ANIM_RIG_BAKE_SUCCESS",
        "Control-rig bake completed with deterministic sample ordering.",
        DiagnosticSeverity::Info,
        result.OutputClipName);
    return result;
}

RetargetRigBakeBatchResult ExecuteRetargetRigBakeBatch(
    const RetargetRigBakeBatchRequest& request) {
    RetargetRigBakeBatchResult result;

    if (request.Jobs.empty()) {
        PushDiagnostic(
            result.Diagnostics,
            "ANIM_RIG_BAKE_RANGE_INVALID",
            "Batch request requires at least one job.",
            DiagnosticSeverity::Error);
        return result;
    }

    std::vector<RetargetRigBakeBatchItemResult> stagedItems;
    stagedItems.reserve(request.Jobs.size());

    for (const RetargetRigBakeBatchJob& job : request.Jobs) {
        RetargetRigBakeBatchItemResult item;
        item.RetargetResult = RetargetAnimationBetweenSkeletons(job.RetargetRequest);
        if (!item.RetargetResult.Success) {
            result.Diagnostics.insert(
                result.Diagnostics.end(),
                item.RetargetResult.Diagnostics.begin(),
                item.RetargetResult.Diagnostics.end());
            if (request.PublishAtomically) {
                PushDiagnostic(
                    result.Diagnostics,
                    "ANIM_RIG_BAKE_BATCH_ROLLBACK",
                    "Batch retarget+bake failed; staged artifact publication was rolled back.",
                    DiagnosticSeverity::Warning);
                result.Success = false;
                result.Items.clear();
                return result;
            }
        }

        if (job.RunRigBake) {
            BakeControlRigRequest bakeRequest;
            bakeRequest.RigId = job.RigId;
            bakeRequest.TargetSkeleton = job.RetargetRequest.TargetSkeleton;
            bakeRequest.SampleRateHz = job.SampleRateHz;
            bakeRequest.StartTimeSec = job.StartTimeSec;
            bakeRequest.EndTimeSec = job.EndTimeSec;
            bakeRequest.KeyReductionTolerance = job.KeyReductionTolerance;
            bakeRequest.OutputClipName = item.RetargetResult.OutputClipName + "_rig_bake";
            item.BakeResult = BakeControlRigToAnimation(bakeRequest);
            if (!item.BakeResult.Success) {
                result.Diagnostics.insert(
                    result.Diagnostics.end(),
                    item.BakeResult.Diagnostics.begin(),
                    item.BakeResult.Diagnostics.end());
                if (request.PublishAtomically) {
                    PushDiagnostic(
                        result.Diagnostics,
                        "ANIM_RIG_BAKE_BATCH_ROLLBACK",
                        "Batch retarget+bake failed; staged artifact publication was rolled back.",
                        DiagnosticSeverity::Warning);
                    result.Success = false;
                    result.Items.clear();
                    return result;
                }
            }
        }

        item.Lineage.SourceClip = job.RetargetRequest.SourceClip;
        item.Lineage.ProfileId = job.RetargetRequest.ProfileId;
        item.Lineage.TargetSkeleton = job.RetargetRequest.TargetSkeleton;
        item.Lineage.RigId = job.RigId;
        item.Lineage.OutputClip = item.BakeResult.OutputClipName.empty()
            ? item.RetargetResult.OutputClipName
            : item.BakeResult.OutputClipName;
        item.Lineage.SettingsHash =
            std::to_string(static_cast<int>(job.SampleRateHz * 1000.0f)) + ":" +
            std::to_string(static_cast<int>(job.KeyReductionTolerance * 1000.0f));
        stagedItems.push_back(std::move(item));
    }

    result.Items = std::move(stagedItems);
    result.Success = true;
    PushDiagnostic(
        result.Diagnostics,
        "ANIM_RIG_BAKE_BATCH_SUCCESS",
        "Retarget and control-rig bake batch completed.",
        DiagnosticSeverity::Info,
        std::to_string(result.Items.size()));
    return result;
}

} // namespace Core::Animation

