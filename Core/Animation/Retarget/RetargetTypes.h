#pragma once

#include "Core/Animation/AnimationRuntimeTypes.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Core::Animation {

constexpr uint32_t RETARGET_PROFILE_SCHEMA_VERSION = 1;

struct RetargetProfileDefinition {
    uint32_t SchemaVersion = RETARGET_PROFILE_SCHEMA_VERSION;
    std::string ProfileId;
    std::string SourceSkeletonTag;
    std::string TargetSkeletonTag;
    std::vector<RetargetBoneMapRule> BoneMapRules;
    RetargetUnmappedBonePolicy UnmappedBonePolicy = RetargetUnmappedBonePolicy::Skip;
    bool PreserveRootMotion = true;
    bool NormalizeScale = false;
};

struct RetargetProfileValidationResult {
    bool Success = false;
    std::unordered_map<std::string, std::string> CanonicalBoneMap;
    std::vector<AnimationDiagnostic> Diagnostics;
};

struct RetargetArtifactLineage {
    std::string SourceClip;
    std::string ProfileId;
    std::string TargetSkeleton;
    std::string RigId;
    std::string OutputClip;
    std::string SettingsHash;
};

} // namespace Core::Animation

