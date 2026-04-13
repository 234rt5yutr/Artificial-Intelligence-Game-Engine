#include "Core/Animation/Retarget/RetargetProfile.h"

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

const char* ToUnmappedBonePolicyString(const RetargetUnmappedBonePolicy policy) {
    switch (policy) {
        case RetargetUnmappedBonePolicy::KeepBind:
            return "keep-bind";
        case RetargetUnmappedBonePolicy::InheritParent:
            return "inherit-parent";
        case RetargetUnmappedBonePolicy::Skip:
            return "skip";
        default:
            return "skip";
    }
}

RetargetUnmappedBonePolicy ParseUnmappedBonePolicy(const std::string& value) {
    if (value == "keep-bind") {
        return RetargetUnmappedBonePolicy::KeepBind;
    }
    if (value == "inherit-parent") {
        return RetargetUnmappedBonePolicy::InheritParent;
    }
    return RetargetUnmappedBonePolicy::Skip;
}

const char* ToTransformPolicyString(const RetargetTransformPolicy policy) {
    switch (policy) {
        case RetargetTransformPolicy::Copy:
            return "copy";
        case RetargetTransformPolicy::Relative:
            return "relative";
        case RetargetTransformPolicy::Scaled:
            return "scaled";
        default:
            return "relative";
    }
}

RetargetTransformPolicy ParseTransformPolicy(const std::string& value) {
    if (value == "copy") {
        return RetargetTransformPolicy::Copy;
    }
    if (value == "scaled") {
        return RetargetTransformPolicy::Scaled;
    }
    return RetargetTransformPolicy::Relative;
}

} // namespace

RetargetProfileDefinition CanonicalizeRetargetProfile(
    const RetargetProfileDefinition& profile) {
    RetargetProfileDefinition canonical = profile;
    canonical.SchemaVersion = RETARGET_PROFILE_SCHEMA_VERSION;
    std::sort(
        canonical.BoneMapRules.begin(),
        canonical.BoneMapRules.end(),
        [](const RetargetBoneMapRule& lhs, const RetargetBoneMapRule& rhs) {
            if (lhs.SourceBone != rhs.SourceBone) {
                return lhs.SourceBone < rhs.SourceBone;
            }
            return lhs.TargetBone < rhs.TargetBone;
        });
    return canonical;
}

RetargetProfileValidationResult ValidateRetargetProfile(
    const RetargetProfileDefinition& profile) {
    RetargetProfileValidationResult result;

    if (profile.ProfileId.empty()) {
        PushDiagnostic(
            result.Diagnostics,
            "ANIM_RETARGET_PROFILE_INVALID",
            "Retarget profile id must not be empty.",
            DiagnosticSeverity::Error);
    }
    if (profile.SourceSkeletonTag.empty()) {
        PushDiagnostic(
            result.Diagnostics,
            "ANIM_RETARGET_PROFILE_INVALID",
            "Retarget profile source skeleton tag must not be empty.",
            DiagnosticSeverity::Error);
    }
    if (profile.TargetSkeletonTag.empty()) {
        PushDiagnostic(
            result.Diagnostics,
            "ANIM_RETARGET_PROFILE_INVALID",
            "Retarget profile target skeleton tag must not be empty.",
            DiagnosticSeverity::Error);
    }
    if (profile.BoneMapRules.empty()) {
        PushDiagnostic(
            result.Diagnostics,
            "ANIM_RETARGET_PROFILE_INVALID",
            "Retarget profile requires at least one bone mapping rule.",
            DiagnosticSeverity::Error,
            profile.ProfileId);
    }

    std::unordered_set<std::string> sourceBones;
    std::unordered_set<std::string> targetBones;
    for (const RetargetBoneMapRule& rule : profile.BoneMapRules) {
        if (rule.SourceBone.empty() || rule.TargetBone.empty()) {
            PushDiagnostic(
                result.Diagnostics,
                "ANIM_RETARGET_PROFILE_INVALID",
                "Retarget mapping rule must define both source and target bones.",
                DiagnosticSeverity::Error,
                profile.ProfileId);
            continue;
        }

        if (!sourceBones.insert(rule.SourceBone).second) {
            PushDiagnostic(
                result.Diagnostics,
                "ANIM_RETARGET_PROFILE_INVALID",
                "Duplicate source bone mapping in retarget profile.",
                DiagnosticSeverity::Error,
                rule.SourceBone);
        }
        if (!targetBones.insert(rule.TargetBone).second) {
            PushDiagnostic(
                result.Diagnostics,
                "ANIM_RETARGET_PROFILE_INVALID",
                "Duplicate target bone mapping in retarget profile.",
                DiagnosticSeverity::Error,
                rule.TargetBone);
        }

        result.CanonicalBoneMap[rule.SourceBone] = rule.TargetBone;
    }

    result.Success = !HasError(result.Diagnostics);
    if (result.Success) {
        PushDiagnostic(
            result.Diagnostics,
            "ANIM_RETARGET_PROFILE_VALIDATED",
            "Retarget profile validated successfully.",
            DiagnosticSeverity::Info,
            profile.ProfileId);
    }
    return result;
}

nlohmann::json SerializeRetargetProfile(
    const RetargetProfileDefinition& profile) {
    const RetargetProfileDefinition canonical = CanonicalizeRetargetProfile(profile);

    nlohmann::json document = nlohmann::json::object();
    document["schemaVersion"] = canonical.SchemaVersion;
    document["profileId"] = canonical.ProfileId;
    document["sourceSkeletonTag"] = canonical.SourceSkeletonTag;
    document["targetSkeletonTag"] = canonical.TargetSkeletonTag;
    document["unmappedBonePolicy"] = ToUnmappedBonePolicyString(canonical.UnmappedBonePolicy);
    document["preserveRootMotion"] = canonical.PreserveRootMotion;
    document["normalizeScale"] = canonical.NormalizeScale;

    nlohmann::json rules = nlohmann::json::array();
    for (const RetargetBoneMapRule& rule : canonical.BoneMapRules) {
        nlohmann::json jsonRule = nlohmann::json::object();
        jsonRule["sourceBone"] = rule.SourceBone;
        jsonRule["targetBone"] = rule.TargetBone;
        jsonRule["rotationPolicy"] = ToTransformPolicyString(rule.RotationPolicy);
        jsonRule["translationPolicy"] = ToTransformPolicyString(rule.TranslationPolicy);
        rules.push_back(std::move(jsonRule));
    }
    document["boneMapRules"] = std::move(rules);
    return document;
}

std::optional<RetargetProfileDefinition> DeserializeRetargetProfile(
    const nlohmann::json& document,
    std::vector<AnimationDiagnostic>& diagnostics) {
    diagnostics.clear();

    if (!document.is_object()) {
        PushDiagnostic(
            diagnostics,
            "ANIM_RETARGET_PROFILE_INVALID",
            "Retarget profile document must be a JSON object.",
            DiagnosticSeverity::Error);
        return std::nullopt;
    }

    RetargetProfileDefinition profile;
    profile.SchemaVersion = document.value("schemaVersion", RETARGET_PROFILE_SCHEMA_VERSION);
    profile.ProfileId = document.value("profileId", std::string{});
    profile.SourceSkeletonTag = document.value("sourceSkeletonTag", std::string{});
    profile.TargetSkeletonTag = document.value("targetSkeletonTag", std::string{});
    profile.UnmappedBonePolicy =
        ParseUnmappedBonePolicy(document.value("unmappedBonePolicy", std::string{"skip"}));
    profile.PreserveRootMotion = document.value("preserveRootMotion", true);
    profile.NormalizeScale = document.value("normalizeScale", false);

    if (document.contains("boneMapRules") && document["boneMapRules"].is_array()) {
        for (const nlohmann::json& jsonRule : document["boneMapRules"]) {
            RetargetBoneMapRule rule;
            rule.SourceBone = jsonRule.value("sourceBone", std::string{});
            rule.TargetBone = jsonRule.value("targetBone", std::string{});
            rule.RotationPolicy =
                ParseTransformPolicy(jsonRule.value("rotationPolicy", std::string{"relative"}));
            rule.TranslationPolicy =
                ParseTransformPolicy(jsonRule.value("translationPolicy", std::string{"relative"}));
            profile.BoneMapRules.push_back(std::move(rule));
        }
    }

    const RetargetProfileValidationResult validation = ValidateRetargetProfile(profile);
    diagnostics = validation.Diagnostics;
    if (!validation.Success) {
        return std::nullopt;
    }
    return CanonicalizeRetargetProfile(profile);
}

} // namespace Core::Animation

