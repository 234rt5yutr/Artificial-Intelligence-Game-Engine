#include "Core/Animation/Parameters/AnimatorParameterChannel.h"

#include "Core/Animation/Events/AnimationEventSynchronizer.h"
#include "Core/ECS/Components/AnimatorComponent.h"

#include <utility>

namespace Core::Animation {

namespace {

AnimatorParameterChannelType ToChannelType(const ECS::AnimatorParameterType type) {
    switch (type) {
        case ECS::AnimatorParameterType::Float:
            return AnimatorParameterChannelType::Float;
        case ECS::AnimatorParameterType::Bool:
            return AnimatorParameterChannelType::Bool;
        case ECS::AnimatorParameterType::Int:
            return AnimatorParameterChannelType::Int;
        case ECS::AnimatorParameterType::Trigger:
            return AnimatorParameterChannelType::Trigger;
        default:
            return AnimatorParameterChannelType::Float;
    }
}

std::string ParameterTypeName(const AnimatorParameterChannelType type) {
    switch (type) {
        case AnimatorParameterChannelType::Float:
            return "float";
        case AnimatorParameterChannelType::Bool:
            return "bool";
        case AnimatorParameterChannelType::Int:
            return "int";
        case AnimatorParameterChannelType::Trigger:
            return "trigger";
        default:
            return "unknown";
    }
}

void AddError(AnimatorParameterSetResult& result,
              const std::string& code,
              const std::string& message) {
    result.Success = false;
    result.ErrorCode = code;
    result.Message = message;
    AnimationDiagnostic diagnostic;
    diagnostic.Code = code;
    diagnostic.Message = message;
    diagnostic.Severity = DiagnosticSeverity::Error;
    diagnostic.Context = result.Message;
    result.Diagnostics.push_back(std::move(diagnostic));
}

} // namespace

AnimatorParameterSetResult SetAnimatorParameterValue(
    ECS::AnimatorComponent& animator,
    const AnimatorParameterSetRequest& request) {
    AnimatorParameterSetResult result;
    result.AppliedEventMode = request.EventMode;

    if (request.ParameterName.empty()) {
        AddError(
            result,
            "ANIM_PARAMETER_CHANNEL_MISSING",
            "Parameter name is required.");
        return result;
    }

    auto parameterIt = animator.Parameters.find(request.ParameterName);
    if (parameterIt == animator.Parameters.end()) {
        AddError(
            result,
            "ANIM_PARAMETER_CHANNEL_MISSING",
            "Parameter '" + request.ParameterName + "' was not found.");
        return result;
    }

    const AnimatorParameterChannelType actualType = ToChannelType(parameterIt->second.Type);
    if (actualType != request.ParameterType) {
        AddError(
            result,
            "ANIM_PARAMETER_TYPE_MISMATCH",
            "Parameter '" + request.ParameterName + "' expects " +
                ParameterTypeName(actualType) + " but request provided " +
                ParameterTypeName(request.ParameterType) + ".");
        return result;
    }

    switch (request.ParameterType) {
        case AnimatorParameterChannelType::Float:
            if (!request.FloatValue.has_value()) {
                AddError(
                    result,
                    "ANIM_PARAMETER_VALUE_MISSING",
                    "Float parameter request is missing FloatValue.");
                return result;
            }
            parameterIt->second.Value = request.FloatValue.value();
            break;

        case AnimatorParameterChannelType::Bool:
            if (!request.BoolValue.has_value()) {
                AddError(
                    result,
                    "ANIM_PARAMETER_VALUE_MISSING",
                    "Bool parameter request is missing BoolValue.");
                return result;
            }
            parameterIt->second.Value = request.BoolValue.value();
            break;

        case AnimatorParameterChannelType::Int:
            if (!request.IntValue.has_value()) {
                AddError(
                    result,
                    "ANIM_PARAMETER_VALUE_MISSING",
                    "Int parameter request is missing IntValue.");
                return result;
            }
            parameterIt->second.Value = request.IntValue.value();
            break;

        case AnimatorParameterChannelType::Trigger:
            parameterIt->second.Value = true;
            parameterIt->second.TriggerConsumed = false;
            break;
    }

    AnimationEventSynchronizer::Get().QueueParameterEvent(
        request.ParameterName,
        request.EventMode);

    result.Success = true;
    result.Message = "Parameter '" + request.ParameterName + "' updated.";
    AnimationDiagnostic diagnostic;
    diagnostic.Code = "ANIM_PARAMETER_SET_SUCCESS";
    diagnostic.Message = result.Message;
    diagnostic.Severity = DiagnosticSeverity::Info;
    diagnostic.Context = request.ParameterName;
    result.Diagnostics.push_back(std::move(diagnostic));
    return result;
}

} // namespace Core::Animation
