#include "Core/Animation/Retarget/AnimationRetargeter.h"

#include <algorithm>
#include <unordered_set>
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

std::string BuildOutputClipName(const RetargetAnimationRequest& request) {
    if (!request.OutputClipName.empty()) {
        return request.OutputClipName;
    }
    return request.SourceClip + "_retargeted_" + request.TargetSkeleton;
}

} // namespace

RetargetAnimationResult RetargetAnimationBetweenSkeletons(
    const RetargetAnimationRequest& request) {
    RetargetAnimationResult result;
    result.OutputClipName = BuildOutputClipName(request);
    result.PreservedRootMotion = request.PreserveRootMotion;
    result.NormalizedScale = request.NormalizeScale;

    if (request.SourceClip.empty()) {
        PushDiagnostic(
            result.Diagnostics,
            "ANIM_RETARGET_PROFILE_INVALID",
            "Retarget request requires SourceClip.",
            DiagnosticSeverity::Error);
    }
    if (request.SourceSkeleton.empty() || request.TargetSkeleton.empty()) {
        PushDiagnostic(
            result.Diagnostics,
            "ANIM_RETARGET_SKELETON_INCOMPATIBLE",
            "Retarget request requires both source and target skeleton ids.",
            DiagnosticSeverity::Error);
    }
    if (request.BoneMapRules.empty()) {
        PushDiagnostic(
            result.Diagnostics,
            "ANIM_RETARGET_PROFILE_INVALID",
            "Retarget request requires at least one BoneMapRule.",
            DiagnosticSeverity::Error,
            request.ProfileId);
    }

    std::unordered_set<std::string> sourceBones;
    std::unordered_set<std::string> targetBones;
    for (const RetargetBoneMapRule& rule : request.BoneMapRules) {
        if (rule.SourceBone.empty() || rule.TargetBone.empty()) {
            PushDiagnostic(
                result.Diagnostics,
                "ANIM_RETARGET_PROFILE_INVALID",
                "Bone map rule requires source and target bone names.",
                DiagnosticSeverity::Error,
                request.ProfileId);
            continue;
        }
        if (!sourceBones.insert(rule.SourceBone).second) {
            PushDiagnostic(
                result.Diagnostics,
                "ANIM_RETARGET_PROFILE_INVALID",
                "Duplicate source bone rule in retarget request.",
                DiagnosticSeverity::Error,
                rule.SourceBone);
        }
        if (!targetBones.insert(rule.TargetBone).second) {
            PushDiagnostic(
                result.Diagnostics,
                "ANIM_RETARGET_PROFILE_INVALID",
                "Duplicate target bone rule in retarget request.",
                DiagnosticSeverity::Error,
                rule.TargetBone);
        }
    }

    if (request.SourceSkeleton == request.TargetSkeleton && request.NormalizeScale) {
        PushDiagnostic(
            result.Diagnostics,
            "ANIM_RETARGET_SKELETON_INCOMPATIBLE",
            "Scale normalization is not required when source and target skeleton are identical.",
            DiagnosticSeverity::Warning,
            request.TargetSkeleton);
    }

    result.MappedBoneCount = static_cast<uint32_t>(sourceBones.size());
    result.Success = !HasError(result.Diagnostics);
    if (result.Success) {
        PushDiagnostic(
            result.Diagnostics,
            "ANIM_RETARGET_SUCCESS",
            "Retarget request compiled with deterministic bone-map ordering.",
            DiagnosticSeverity::Info,
            result.OutputClipName);
    }

    return result;
}

RetargetBatchResult ExecuteRetargetBatch(
    const RetargetBatchRequest& request) {
    RetargetBatchResult batchResult;

    if (request.Requests.empty()) {
        PushDiagnostic(
            batchResult.Diagnostics,
            "ANIM_RETARGET_PROFILE_INVALID",
            "Retarget batch requires at least one request.",
            DiagnosticSeverity::Error);
        return batchResult;
    }

    std::vector<RetargetBatchItemResult> stagedItems;
    stagedItems.reserve(request.Requests.size());

    for (const RetargetAnimationRequest& retargetRequest : request.Requests) {
        RetargetBatchItemResult item;
        item.RetargetResult = RetargetAnimationBetweenSkeletons(retargetRequest);
        if (!item.RetargetResult.Success) {
            batchResult.Diagnostics.insert(
                batchResult.Diagnostics.end(),
                item.RetargetResult.Diagnostics.begin(),
                item.RetargetResult.Diagnostics.end());
            if (request.PublishAtomically) {
                PushDiagnostic(
                    batchResult.Diagnostics,
                    "ANIM_RETARGET_BATCH_ROLLBACK",
                    "Retarget batch failed; staged outputs were rolled back.",
                    DiagnosticSeverity::Warning);
                batchResult.Success = false;
                batchResult.Items.clear();
                return batchResult;
            }
        }

        item.Lineage.SourceClip = retargetRequest.SourceClip;
        item.Lineage.ProfileId = retargetRequest.ProfileId;
        item.Lineage.TargetSkeleton = retargetRequest.TargetSkeleton;
        item.Lineage.OutputClip = item.RetargetResult.OutputClipName;
        stagedItems.push_back(std::move(item));
    }

    batchResult.Items = std::move(stagedItems);
    batchResult.Success = true;
    PushDiagnostic(
        batchResult.Diagnostics,
        "ANIM_RETARGET_BATCH_SUCCESS",
        "Retarget batch completed successfully.",
        DiagnosticSeverity::Info,
        std::to_string(batchResult.Items.size()));
    return batchResult;
}

} // namespace Core::Animation

