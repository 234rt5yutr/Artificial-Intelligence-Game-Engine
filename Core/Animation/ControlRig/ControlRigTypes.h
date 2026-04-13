#pragma once

#include "Core/Animation/AnimationRuntimeTypes.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Core::Animation {

constexpr uint32_t CONTROL_RIG_SCHEMA_VERSION = 1;

enum class ControlRigChannelType : uint8_t {
    Float = 0,
    Bool,
    Vec3
};

struct ControlRigChannelDefinition {
    std::string ChannelName;
    ControlRigChannelType Type = ControlRigChannelType::Float;
    float DefaultFloatValue = 0.0f;
    bool DefaultBoolValue = false;
    std::array<float, 3> DefaultVec3Value{0.0f, 0.0f, 0.0f};
};

struct ControlRigConstraintDefinition {
    std::string ConstraintId;
    std::string BoneName;
    std::string ChannelName;
    float Weight = 1.0f;
    bool Required = true;
};

struct ControlRigDefinition {
    uint32_t SchemaVersion = CONTROL_RIG_SCHEMA_VERSION;
    std::string RigId;
    std::string TargetSkeletonTag;
    std::vector<ControlRigChannelDefinition> Channels;
    std::vector<ControlRigConstraintDefinition> Constraints;
};

struct ControlRigSolveRequest {
    const ControlRigDefinition* Rig = nullptr;
    float TimeSec = 0.0f;
    std::unordered_map<std::string, float> FloatChannelOverrides;
    std::unordered_map<std::string, bool> BoolChannelOverrides;
    std::unordered_map<std::string, std::array<float, 3>> Vec3ChannelOverrides;
};

struct ControlRigBoneTransform {
    std::array<float, 3> Translation{0.0f, 0.0f, 0.0f};
    std::array<float, 4> Rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct ControlRigSolveResult {
    bool Success = false;
    std::unordered_map<std::string, ControlRigBoneTransform> BoneTransforms;
    std::vector<AnimationDiagnostic> Diagnostics;
};

} // namespace Core::Animation

