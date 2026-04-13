#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Core {
namespace UI {
namespace Animation {

    constexpr const char* UI_TRANSITION_INVALID_WIDGET = "UI_TRANSITION_INVALID_WIDGET";
    constexpr const char* UI_TRANSITION_INVALID_REQUEST = "UI_TRANSITION_INVALID_REQUEST";
    constexpr const char* UI_TRANSITION_INVALID_TIMELINE = "UI_TRANSITION_INVALID_TIMELINE";
    constexpr const char* UI_TRANSITION_INVALID_CHANNEL = "UI_TRANSITION_INVALID_CHANNEL";

    enum class TransitionInterruptionPolicy : uint8_t {
        CancelPrevious = 0,
        Queue,
        Blend
    };

    enum class TransitionChannelTarget : uint8_t {
        Alpha = 0,
        PositionX,
        PositionY,
        ScaleX,
        ScaleY,
        ColorR,
        ColorG,
        ColorB,
        ColorA,
        CustomProperty
    };

    enum class TransitionEasing : uint8_t {
        Linear = 0,
        EaseInQuad,
        EaseOutQuad,
        EaseInOutQuad,
        EaseOutCubic,
        EaseOutBack
    };

    enum class TransitionRuntimeState : uint8_t {
        Queued = 0,
        Running,
        Completed,
        Cancelled
    };

    enum class TransitionPropertyLockMode : uint8_t {
        BindingWins = 0,
        TransitionWins,
        Blend
    };

    struct TransitionChannelDefinition {
        TransitionChannelTarget Target = TransitionChannelTarget::Alpha;
        std::string PropertyPath;
        float From = 0.0f;
        float To = 0.0f;
        TransitionEasing Easing = TransitionEasing::Linear;
        float StartTime = 0.0f;
        float Duration = 0.0f;
    };

    struct WidgetTransitionTimeline {
        float Duration = 0.0f;
        std::vector<TransitionChannelDefinition> Channels;
    };

    struct WidgetTransitionRequest {
        std::string WidgetId;
        std::string TransitionId;
        WidgetTransitionTimeline Timeline;
        TransitionInterruptionPolicy InterruptionPolicy = TransitionInterruptionPolicy::CancelPrevious;
        bool InterruptExisting = true;
        std::function<void(uint64_t transitionHandle, bool completed)> CompletionCallback;
    };

    struct WidgetTransitionResult {
        bool Success = false;
        std::string ErrorCode;
        std::string Message;
        uint64_t TransitionHandle = 0;
        float Duration = 0.0f;
        TransitionRuntimeState State = TransitionRuntimeState::Running;
    };

    struct WidgetTransitionState {
        uint64_t Handle = 0;
        std::string WidgetId;
        std::string TransitionId;
        TransitionInterruptionPolicy InterruptionPolicy = TransitionInterruptionPolicy::CancelPrevious;
        TransitionRuntimeState State = TransitionRuntimeState::Running;
        float ElapsedTime = 0.0f;
        float Duration = 0.0f;
        uint32_t ChannelCount = 0;
    };

    struct WidgetTransitionDiagnostics {
        uint32_t ActiveTransitions = 0;
        uint32_t QueuedTransitions = 0;
        uint64_t CompletedTransitions = 0;
        uint64_t CancelledTransitions = 0;
        uint64_t ParseFailures = 0;
        uint64_t ApplyFailures = 0;
        uint64_t ArbitrationConflicts = 0;
        uint64_t BindingWinsResolutions = 0;
        uint64_t TransitionWinsResolutions = 0;
        uint64_t BlendResolutions = 0;
    };

    inline std::optional<TransitionInterruptionPolicy> TransitionInterruptionPolicyFromString(
        std::string_view value) {
        if (value == "cancel_previous") {
            return TransitionInterruptionPolicy::CancelPrevious;
        }
        if (value == "queue") {
            return TransitionInterruptionPolicy::Queue;
        }
        if (value == "blend") {
            return TransitionInterruptionPolicy::Blend;
        }
        return std::nullopt;
    }

    inline std::string TransitionInterruptionPolicyToString(TransitionInterruptionPolicy policy) {
        switch (policy) {
            case TransitionInterruptionPolicy::CancelPrevious:
                return "cancel_previous";
            case TransitionInterruptionPolicy::Queue:
                return "queue";
            case TransitionInterruptionPolicy::Blend:
                return "blend";
            default:
                return "cancel_previous";
        }
    }

    inline std::string TransitionRuntimeStateToString(TransitionRuntimeState state) {
        switch (state) {
            case TransitionRuntimeState::Queued:
                return "queued";
            case TransitionRuntimeState::Running:
                return "running";
            case TransitionRuntimeState::Completed:
                return "completed";
            case TransitionRuntimeState::Cancelled:
                return "cancelled";
            default:
                return "running";
        }
    }

    inline std::optional<TransitionPropertyLockMode> TransitionPropertyLockModeFromString(
        std::string_view value) {
        if (value == "binding_wins") {
            return TransitionPropertyLockMode::BindingWins;
        }
        if (value == "transition_wins") {
            return TransitionPropertyLockMode::TransitionWins;
        }
        if (value == "blend") {
            return TransitionPropertyLockMode::Blend;
        }
        return std::nullopt;
    }

    inline std::string TransitionPropertyLockModeToString(TransitionPropertyLockMode mode) {
        switch (mode) {
            case TransitionPropertyLockMode::BindingWins:
                return "binding_wins";
            case TransitionPropertyLockMode::TransitionWins:
                return "transition_wins";
            case TransitionPropertyLockMode::Blend:
                return "blend";
            default:
                return "transition_wins";
        }
    }

} // namespace Animation
} // namespace UI
} // namespace Core

