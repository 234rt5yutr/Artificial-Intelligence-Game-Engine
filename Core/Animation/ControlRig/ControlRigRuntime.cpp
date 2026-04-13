#include "Core/Animation/ControlRig/ControlRigRuntime.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Core::Animation {

namespace {

uint64_t HashString(const std::string& value) {
    uint64_t hash = 14695981039346656037ULL;
    for (const char character : value) {
        hash ^= static_cast<uint64_t>(static_cast<uint8_t>(character));
        hash *= 1099511628211ULL;
    }
    return hash;
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

bool HasError(const std::vector<AnimationDiagnostic>& diagnostics) {
    return std::any_of(
        diagnostics.begin(),
        diagnostics.end(),
        [](const AnimationDiagnostic& diagnostic) {
            return diagnostic.Severity == DiagnosticSeverity::Error;
        });
}

float ResolveFloatChannelValue(const ControlRigSolveRequest& request,
                               const ControlRigChannelDefinition& channel) {
    const auto overrideIt = request.FloatChannelOverrides.find(channel.ChannelName);
    if (overrideIt != request.FloatChannelOverrides.end()) {
        return overrideIt->second;
    }
    return channel.DefaultFloatValue;
}

} // namespace

ControlRigSolveResult EvaluateControlRig(
    const ControlRigSolveRequest& request) {
    ControlRigSolveResult result;

    if (request.Rig == nullptr) {
        PushDiagnostic(
            result.Diagnostics,
            "ANIM_RIG_BAKE_CONSTRAINT_FAILURE",
            "Control-rig solve request is missing rig definition.",
            DiagnosticSeverity::Error);
        return result;
    }
    if (request.Rig->RigId.empty()) {
        PushDiagnostic(
            result.Diagnostics,
            "ANIM_RIG_BAKE_CONSTRAINT_FAILURE",
            "Control-rig definition requires a rig id.",
            DiagnosticSeverity::Error);
    }

    std::vector<const ControlRigConstraintDefinition*> orderedConstraints;
    orderedConstraints.reserve(request.Rig->Constraints.size());
    for (const ControlRigConstraintDefinition& constraint : request.Rig->Constraints) {
        orderedConstraints.push_back(&constraint);
    }
    std::sort(
        orderedConstraints.begin(),
        orderedConstraints.end(),
        [](const ControlRigConstraintDefinition* lhs,
           const ControlRigConstraintDefinition* rhs) {
            if (lhs->ConstraintId != rhs->ConstraintId) {
                return lhs->ConstraintId < rhs->ConstraintId;
            }
            if (lhs->BoneName != rhs->BoneName) {
                return lhs->BoneName < rhs->BoneName;
            }
            return lhs->ChannelName < rhs->ChannelName;
        });

    for (const ControlRigConstraintDefinition* constraint : orderedConstraints) {
        const auto channelIt = std::find_if(
            request.Rig->Channels.begin(),
            request.Rig->Channels.end(),
            [&](const ControlRigChannelDefinition& channel) {
                return channel.ChannelName == constraint->ChannelName;
            });

        if (channelIt == request.Rig->Channels.end()) {
            if (constraint->Required) {
                PushDiagnostic(
                    result.Diagnostics,
                    "ANIM_RIG_BAKE_CONSTRAINT_FAILURE",
                    "Required control-rig channel binding was not found.",
                    DiagnosticSeverity::Error,
                    constraint->ChannelName);
            } else {
                PushDiagnostic(
                    result.Diagnostics,
                    "ANIM_RIG_BAKE_CONSTRAINT_IGNORED",
                    "Optional control-rig channel binding was not found.",
                    DiagnosticSeverity::Warning,
                    constraint->ChannelName);
            }
            continue;
        }

        const float channelValue = ResolveFloatChannelValue(request, *channelIt);
        const float influence = std::max(0.0f, constraint->Weight) * channelValue;
        const float oscillation =
            std::sin(request.TimeSec + static_cast<float>(HashString(constraint->ConstraintId) % 1024ULL) * 0.001f);

        ControlRigBoneTransform& transform = result.BoneTransforms[constraint->BoneName];
        transform.Translation[0] += influence * 0.01f;
        transform.Translation[1] += oscillation * 0.005f;
        transform.Translation[2] += influence * 0.0025f;
        transform.Rotation[0] = 1.0f;
        transform.Rotation[1] = oscillation * 0.01f;
        transform.Rotation[2] = influence * 0.005f;
        transform.Rotation[3] = 0.0f;
    }

    result.Success = !HasError(result.Diagnostics);
    if (result.Success) {
        PushDiagnostic(
            result.Diagnostics,
            "ANIM_RIG_SOLVE_SUCCESS",
            "Control-rig solved successfully with deterministic constraint ordering.",
            DiagnosticSeverity::Info,
            request.Rig->RigId);
    }
    return result;
}

} // namespace Core::Animation

