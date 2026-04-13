#include "Core/UI/Animation/WidgetTransitionService.h"

#include "Core/UI/Binding/UIBindingService.h"
#include "Core/UI/Widgets/WidgetSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <type_traits>
#include <utility>

namespace Core {
namespace UI {
namespace Animation {
namespace {

    constexpr float kFloatEpsilon = 0.0001f;

    struct ScalarAccumulator {
        float Sum = 0.0f;
        uint32_t Count = 0;
    };

    struct Vec2Accumulator {
        ScalarAccumulator X;
        ScalarAccumulator Y;
    };

    struct Vec4Accumulator {
        ScalarAccumulator R;
        ScalarAccumulator G;
        ScalarAccumulator B;
        ScalarAccumulator A;
    };

    struct WidgetChannelAccumulators {
        ScalarAccumulator Alpha;
        Vec2Accumulator Position;
        Vec2Accumulator Scale;
        Vec4Accumulator Color;
        std::unordered_map<std::string, ScalarAccumulator> Custom;
    };

    void AccumulateValue(ScalarAccumulator* accumulator, float value) {
        if (accumulator == nullptr) {
            return;
        }
        accumulator->Sum += value;
        ++accumulator->Count;
    }

    bool HasValue(const ScalarAccumulator& accumulator) {
        return accumulator.Count > 0;
    }

    float GetAverage(const ScalarAccumulator& accumulator, float fallback) {
        if (!HasValue(accumulator)) {
            return fallback;
        }
        return accumulator.Sum / static_cast<float>(accumulator.Count);
    }

    float EvaluateEasing(TransitionEasing easing, float t) {
        t = glm::clamp(t, 0.0f, 1.0f);
        switch (easing) {
            case TransitionEasing::Linear:
                return t;
            case TransitionEasing::EaseInQuad:
                return t * t;
            case TransitionEasing::EaseOutQuad:
                return 1.0f - (1.0f - t) * (1.0f - t);
            case TransitionEasing::EaseInOutQuad:
                if (t < 0.5f) {
                    return 2.0f * t * t;
                }
                return 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;
            case TransitionEasing::EaseOutCubic:
                return 1.0f - std::pow(1.0f - t, 3.0f);
            case TransitionEasing::EaseOutBack: {
                constexpr float c1 = 1.70158f;
                constexpr float c3 = c1 + 1.0f;
                const float x = t - 1.0f;
                return 1.0f + c3 * x * x * x + c1 * x * x;
            }
            default:
                return t;
        }
    }

    std::optional<TransitionEasing> ParseEasing(std::string_view easingValue) {
        if (easingValue.empty() || easingValue == "linear") {
            return TransitionEasing::Linear;
        }
        if (easingValue == "ease_in_quad") {
            return TransitionEasing::EaseInQuad;
        }
        if (easingValue == "ease_out_quad") {
            return TransitionEasing::EaseOutQuad;
        }
        if (easingValue == "ease_in_out_quad") {
            return TransitionEasing::EaseInOutQuad;
        }
        if (easingValue == "ease_out_cubic") {
            return TransitionEasing::EaseOutCubic;
        }
        if (easingValue == "ease_out_back") {
            return TransitionEasing::EaseOutBack;
        }
        return std::nullopt;
    }

    std::optional<TransitionChannelTarget> ParseChannelTarget(std::string_view propertyPath) {
        if (propertyPath == "alpha") {
            return TransitionChannelTarget::Alpha;
        }
        if (propertyPath == "position.x") {
            return TransitionChannelTarget::PositionX;
        }
        if (propertyPath == "position.y") {
            return TransitionChannelTarget::PositionY;
        }
        if (propertyPath == "scale.x") {
            return TransitionChannelTarget::ScaleX;
        }
        if (propertyPath == "scale.y") {
            return TransitionChannelTarget::ScaleY;
        }
        if (propertyPath == "color.r") {
            return TransitionChannelTarget::ColorR;
        }
        if (propertyPath == "color.g") {
            return TransitionChannelTarget::ColorG;
        }
        if (propertyPath == "color.b") {
            return TransitionChannelTarget::ColorB;
        }
        if (propertyPath == "color.a") {
            return TransitionChannelTarget::ColorA;
        }
        return std::nullopt;
    }

    std::optional<glm::vec2> ExtractVec2(
        const std::optional<Widgets::WidgetSystem::WidgetPropertyValue>& value) {
        if (!value.has_value()) {
            return std::nullopt;
        }
        if (const glm::vec2* vec2Value = std::get_if<glm::vec2>(&value.value())) {
            return *vec2Value;
        }
        return std::nullopt;
    }

    std::optional<glm::vec4> ExtractVec4(
        const std::optional<Widgets::WidgetSystem::WidgetPropertyValue>& value) {
        if (!value.has_value()) {
            return std::nullopt;
        }
        if (const glm::vec4* vec4Value = std::get_if<glm::vec4>(&value.value())) {
            return *vec4Value;
        }
        return std::nullopt;
    }

    std::optional<float> ExtractFloat(
        const std::optional<Widgets::WidgetSystem::WidgetPropertyValue>& value) {
        if (!value.has_value()) {
            return std::nullopt;
        }
        if (const float* floatValue = std::get_if<float>(&value.value())) {
            return *floatValue;
        }
        if (const int32_t* intValue = std::get_if<int32_t>(&value.value())) {
            return static_cast<float>(*intValue);
        }
        if (const bool* boolValue = std::get_if<bool>(&value.value())) {
            return *boolValue ? 1.0f : 0.0f;
        }
        return std::nullopt;
    }

} // namespace

    WidgetTransitionService& WidgetTransitionService::Get() {
        static WidgetTransitionService instance;
        return instance;
    }

    WidgetTransitionResult WidgetTransitionService::AnimateWidgetTransition(
        const WidgetTransitionRequest& request) {
        WidgetTransitionResult result;
        result.Success = false;
        result.State = TransitionRuntimeState::Running;

        if (request.WidgetId.empty()) {
            result.ErrorCode = UI_TRANSITION_INVALID_REQUEST;
            result.Message = "WidgetId is required.";
            return result;
        }

        if (Widgets::WidgetSystem::Get().FindWidget(request.WidgetId) == nullptr) {
            result.ErrorCode = UI_TRANSITION_INVALID_WIDGET;
            result.Message = "Widget is not registered in WidgetSystem.";
            return result;
        }

        std::string errorCode;
        std::string message;
        if (!ValidateTimeline(request.Timeline, &errorCode, &message)) {
            result.ErrorCode = errorCode;
            result.Message = message;
            return result;
        }

        if (request.InterruptExisting || request.InterruptionPolicy == TransitionInterruptionPolicy::CancelPrevious) {
            (void)CancelWidgetTransitions(request.WidgetId);
        }

        const bool hasRunningTransition = std::any_of(
            m_Transitions.begin(),
            m_Transitions.end(),
            [&request](const auto& pair) {
                const TransitionRecord& record = pair.second;
                return record.State.WidgetId == request.WidgetId &&
                       record.State.State == TransitionRuntimeState::Running;
            });

        TransitionRecord record;
        record.State.Handle = m_NextTransitionHandle++;
        record.State.WidgetId = request.WidgetId;
        record.State.TransitionId =
            request.TransitionId.empty() ? ("transition_" + std::to_string(record.State.Handle)) : request.TransitionId;
        record.State.InterruptionPolicy = request.InterruptionPolicy;
        record.State.ElapsedTime = 0.0f;
        record.State.Duration = request.Timeline.Duration;
        record.State.ChannelCount = static_cast<uint32_t>(request.Timeline.Channels.size());
        record.Timeline = request.Timeline;
        record.CompletionCallback = request.CompletionCallback;
        const uint64_t transitionHandle = record.State.Handle;

        if (request.InterruptionPolicy == TransitionInterruptionPolicy::Queue && hasRunningTransition) {
            record.State.State = TransitionRuntimeState::Queued;
            m_Transitions[transitionHandle] = std::move(record);
            m_QueuedByWidget[request.WidgetId].push_back(transitionHandle);
        } else {
            record.State.State = TransitionRuntimeState::Running;
            m_Transitions[transitionHandle] = std::move(record);
        }

        RefreshDiagnostics();

        result.Success = true;
        result.TransitionHandle = transitionHandle;
        result.Duration = request.Timeline.Duration;
        result.State = m_Transitions[result.TransitionHandle].State.State;
        return result;
    }

    WidgetTransitionResult WidgetTransitionService::AnimateWidgetTransition(
        const std::string& widgetId,
        const std::string& transitionId,
        const nlohmann::json& timelinePayload,
        TransitionInterruptionPolicy interruptionPolicy,
        bool interruptExisting) {
        WidgetTransitionResult result;
        std::string parseError;
        std::optional<WidgetTransitionTimeline> timeline =
            ParseTimelinePayload(timelinePayload, &parseError);
        if (!timeline.has_value()) {
            ++m_Diagnostics.ParseFailures;
            result.Success = false;
            result.ErrorCode = UI_TRANSITION_INVALID_TIMELINE;
            result.Message = parseError.empty() ? "Timeline parse failed." : parseError;
            return result;
        }

        WidgetTransitionRequest request;
        request.WidgetId = widgetId;
        request.TransitionId = transitionId;
        request.Timeline = timeline.value();
        request.InterruptionPolicy = interruptionPolicy;
        request.InterruptExisting = interruptExisting;
        return AnimateWidgetTransition(request);
    }

    bool WidgetTransitionService::CancelTransition(uint64_t transitionHandle) {
        auto transitionIt = m_Transitions.find(transitionHandle);
        if (transitionIt == m_Transitions.end()) {
            return false;
        }

        MarkTransitionCancelled(&transitionIt->second);
        if (transitionIt->second.CompletionCallback) {
            transitionIt->second.CompletionCallback(transitionIt->second.State.Handle, false);
        }

        for (auto& [widgetId, queue] : m_QueuedByWidget) {
            (void)widgetId;
            queue.erase(std::remove(queue.begin(), queue.end(), transitionHandle), queue.end());
        }
        m_Transitions.erase(transitionIt);
        RefreshDiagnostics();
        return true;
    }

    uint32_t WidgetTransitionService::CancelWidgetTransitions(const std::string& widgetId) {
        uint32_t cancelled = 0;
        std::vector<uint64_t> handlesToCancel;
        for (const auto& [handle, record] : m_Transitions) {
            if (record.State.WidgetId == widgetId) {
                handlesToCancel.push_back(handle);
            }
        }

        for (const uint64_t handle : handlesToCancel) {
            if (CancelTransition(handle)) {
                ++cancelled;
            }
        }
        return cancelled;
    }

    void WidgetTransitionService::ClearTransitions() {
        std::vector<uint64_t> handles;
        handles.reserve(m_Transitions.size());
        for (const auto& [handle, record] : m_Transitions) {
            (void)record;
            handles.push_back(handle);
        }

        for (const uint64_t handle : handles) {
            (void)CancelTransition(handle);
        }

        m_Transitions.clear();
        m_QueuedByWidget.clear();
        RefreshDiagnostics();
    }

    void WidgetTransitionService::SetPropertyLockMode(
        const std::string& widgetId,
        std::string_view propertyPath,
        TransitionPropertyLockMode mode) {
        if (widgetId.empty()) {
            return;
        }
        const std::string key = MakeWidgetPropertyKey(widgetId, propertyPath);
        m_PropertyLockModes[key] = mode;
    }

    TransitionPropertyLockMode WidgetTransitionService::GetPropertyLockMode(
        const std::string& widgetId,
        std::string_view propertyPath) const {
        if (widgetId.empty()) {
            return TransitionPropertyLockMode::TransitionWins;
        }
        const std::string key = MakeWidgetPropertyKey(widgetId, propertyPath);
        const auto modeIt = m_PropertyLockModes.find(key);
        if (modeIt == m_PropertyLockModes.end()) {
            return TransitionPropertyLockMode::TransitionWins;
        }
        return modeIt->second;
    }

    void WidgetTransitionService::UpdateTransitions(float deltaTime) {
        if (deltaTime < 0.0f) {
            deltaTime = 0.0f;
        }

        PromoteQueuedTransitions();

        std::vector<uint64_t> runningHandles;
        for (const auto& [handle, record] : m_Transitions) {
            if (record.State.State == TransitionRuntimeState::Running) {
                runningHandles.push_back(handle);
            }
        }
        std::sort(runningHandles.begin(), runningHandles.end());

        std::unordered_map<std::string, WidgetChannelAccumulators> widgetAccumulators;
        std::vector<uint64_t> completedHandles;
        completedHandles.reserve(runningHandles.size());
        std::vector<uint64_t> cancelledHandles;

        for (const uint64_t handle : runningHandles) {
            auto transitionIt = m_Transitions.find(handle);
            if (transitionIt == m_Transitions.end()) {
                continue;
            }

            TransitionRecord& record = transitionIt->second;
            if (Widgets::WidgetSystem::Get().FindWidget(record.State.WidgetId) == nullptr) {
                MarkTransitionCancelled(&record);
                cancelledHandles.push_back(handle);
                continue;
            }

            const float previousElapsed = record.State.ElapsedTime;
            record.State.ElapsedTime =
                glm::clamp(record.State.ElapsedTime + deltaTime, 0.0f, record.State.Duration);
            const float elapsed = record.State.ElapsedTime;

            WidgetChannelAccumulators& accumulators = widgetAccumulators[record.State.WidgetId];
            for (const TransitionChannelDefinition& channel : record.Timeline.Channels) {
                const float channelDuration = channel.Duration;
                if (channelDuration <= kFloatEpsilon) {
                    continue;
                }
                if (elapsed + kFloatEpsilon < channel.StartTime) {
                    continue;
                }

                const float channelElapsed = glm::clamp((elapsed - channel.StartTime) / channelDuration, 0.0f, 1.0f);
                if (channelElapsed <= 0.0f && previousElapsed + kFloatEpsilon < channel.StartTime) {
                    continue;
                }

                const float eased = EvaluateEasing(channel.Easing, channelElapsed);
                const float value = glm::mix(channel.From, channel.To, eased);

                switch (channel.Target) {
                    case TransitionChannelTarget::Alpha:
                        AccumulateValue(&accumulators.Alpha, value);
                        break;
                    case TransitionChannelTarget::PositionX:
                        AccumulateValue(&accumulators.Position.X, value);
                        break;
                    case TransitionChannelTarget::PositionY:
                        AccumulateValue(&accumulators.Position.Y, value);
                        break;
                    case TransitionChannelTarget::ScaleX:
                        AccumulateValue(&accumulators.Scale.X, value);
                        break;
                    case TransitionChannelTarget::ScaleY:
                        AccumulateValue(&accumulators.Scale.Y, value);
                        break;
                    case TransitionChannelTarget::ColorR:
                        AccumulateValue(&accumulators.Color.R, value);
                        break;
                    case TransitionChannelTarget::ColorG:
                        AccumulateValue(&accumulators.Color.G, value);
                        break;
                    case TransitionChannelTarget::ColorB:
                        AccumulateValue(&accumulators.Color.B, value);
                        break;
                    case TransitionChannelTarget::ColorA:
                        AccumulateValue(&accumulators.Color.A, value);
                        break;
                    case TransitionChannelTarget::CustomProperty:
                        AccumulateValue(&accumulators.Custom[channel.PropertyPath], value);
                        break;
                    default:
                        break;
                }
            }

            if (record.State.ElapsedTime + kFloatEpsilon >= record.State.Duration) {
                completedHandles.push_back(handle);
            }
        }

        Binding::UIBindingService& bindingService = Binding::UIBindingService::Get();

        for (auto& [widgetId, accumulators] : widgetAccumulators) {
            if (Widgets::WidgetSystem::Get().FindWidget(widgetId) == nullptr) {
                continue;
            }

            auto resolveArbitration = [&](std::string_view propertyPath) {
                const bool hasBinding = bindingService.HasBindingForProperty(widgetId, propertyPath);
                const TransitionPropertyLockMode mode = GetPropertyLockMode(widgetId, propertyPath);
                if (hasBinding) {
                    ++m_Diagnostics.ArbitrationConflicts;
                    switch (mode) {
                        case TransitionPropertyLockMode::BindingWins:
                            ++m_Diagnostics.BindingWinsResolutions;
                            break;
                        case TransitionPropertyLockMode::Blend:
                            ++m_Diagnostics.BlendResolutions;
                            break;
                        case TransitionPropertyLockMode::TransitionWins:
                        default:
                            ++m_Diagnostics.TransitionWinsResolutions;
                            break;
                    }
                }
                return std::make_pair(mode, hasBinding);
            };

            if (HasValue(accumulators.Alpha)) {
                float alpha = glm::clamp(GetAverage(accumulators.Alpha, 1.0f), 0.0f, 1.0f);
                const float currentAlpha = ExtractFloat(
                    Widgets::WidgetSystem::Get().GetWidgetProperty(widgetId, "alpha")).value_or(alpha);
                auto [mode, hasBinding] = resolveArbitration("alpha");
                if (hasBinding && mode == TransitionPropertyLockMode::BindingWins) {
                    continue;
                }
                if (hasBinding && mode == TransitionPropertyLockMode::Blend) {
                    alpha = glm::mix(currentAlpha, alpha, 0.5f);
                }

                const bool applied = Widgets::WidgetSystem::Get().SetWidgetProperty(
                    widgetId,
                    "alpha",
                    Widgets::WidgetSystem::WidgetPropertyValue(alpha));
                if (!applied) {
                    ++m_Diagnostics.ApplyFailures;
                } else if (hasBinding) {
                    bindingService.NotifyTransitionMutation(widgetId, "alpha");
                }
            }

            if (HasValue(accumulators.Position.X) || HasValue(accumulators.Position.Y)) {
                glm::vec2 base = {0.0f, 0.0f};
                if (std::optional<glm::vec2> current =
                        ExtractVec2(Widgets::WidgetSystem::Get().GetWidgetProperty(widgetId, "position"))) {
                    base = current.value();
                }
                base.x = GetAverage(accumulators.Position.X, base.x);
                base.y = GetAverage(accumulators.Position.Y, base.y);

                auto [mode, hasBinding] = resolveArbitration("position");
                if (hasBinding && mode == TransitionPropertyLockMode::BindingWins) {
                    continue;
                }
                if (hasBinding && mode == TransitionPropertyLockMode::Blend) {
                    const glm::vec2 current = ExtractVec2(
                        Widgets::WidgetSystem::Get().GetWidgetProperty(widgetId, "position")).value_or(base);
                    base.x = glm::mix(current.x, base.x, 0.5f);
                    base.y = glm::mix(current.y, base.y, 0.5f);
                }

                const bool applied = Widgets::WidgetSystem::Get().SetWidgetProperty(
                    widgetId,
                    "position",
                    Widgets::WidgetSystem::WidgetPropertyValue(base));
                if (!applied) {
                    ++m_Diagnostics.ApplyFailures;
                } else if (hasBinding) {
                    bindingService.NotifyTransitionMutation(widgetId, "position");
                }
            }

            if (HasValue(accumulators.Scale.X) || HasValue(accumulators.Scale.Y)) {
                glm::vec2 base = {1.0f, 1.0f};
                if (std::optional<glm::vec2> current =
                        ExtractVec2(Widgets::WidgetSystem::Get().GetWidgetProperty(widgetId, "scale"))) {
                    base = current.value();
                }
                base.x = glm::max(0.0f, GetAverage(accumulators.Scale.X, base.x));
                base.y = glm::max(0.0f, GetAverage(accumulators.Scale.Y, base.y));

                auto [mode, hasBinding] = resolveArbitration("scale");
                if (hasBinding && mode == TransitionPropertyLockMode::BindingWins) {
                    continue;
                }
                if (hasBinding && mode == TransitionPropertyLockMode::Blend) {
                    const glm::vec2 current = ExtractVec2(
                        Widgets::WidgetSystem::Get().GetWidgetProperty(widgetId, "scale")).value_or(base);
                    base.x = glm::mix(current.x, base.x, 0.5f);
                    base.y = glm::mix(current.y, base.y, 0.5f);
                }

                const bool applied = Widgets::WidgetSystem::Get().SetWidgetProperty(
                    widgetId,
                    "scale",
                    Widgets::WidgetSystem::WidgetPropertyValue(base));
                if (!applied) {
                    ++m_Diagnostics.ApplyFailures;
                } else if (hasBinding) {
                    bindingService.NotifyTransitionMutation(widgetId, "scale");
                }
            }

            if (HasValue(accumulators.Color.R) ||
                HasValue(accumulators.Color.G) ||
                HasValue(accumulators.Color.B) ||
                HasValue(accumulators.Color.A)) {
                glm::vec4 base = {1.0f, 1.0f, 1.0f, 1.0f};
                if (std::optional<glm::vec4> current =
                        ExtractVec4(Widgets::WidgetSystem::Get().GetWidgetProperty(widgetId, "color"))) {
                    base = current.value();
                }
                base.r = glm::clamp(GetAverage(accumulators.Color.R, base.r), 0.0f, 1.0f);
                base.g = glm::clamp(GetAverage(accumulators.Color.G, base.g), 0.0f, 1.0f);
                base.b = glm::clamp(GetAverage(accumulators.Color.B, base.b), 0.0f, 1.0f);
                base.a = glm::clamp(GetAverage(accumulators.Color.A, base.a), 0.0f, 1.0f);

                auto [mode, hasBinding] = resolveArbitration("color");
                if (hasBinding && mode == TransitionPropertyLockMode::BindingWins) {
                    continue;
                }
                if (hasBinding && mode == TransitionPropertyLockMode::Blend) {
                    const glm::vec4 current = ExtractVec4(
                        Widgets::WidgetSystem::Get().GetWidgetProperty(widgetId, "color")).value_or(base);
                    base.r = glm::mix(current.r, base.r, 0.5f);
                    base.g = glm::mix(current.g, base.g, 0.5f);
                    base.b = glm::mix(current.b, base.b, 0.5f);
                    base.a = glm::mix(current.a, base.a, 0.5f);
                }

                const bool applied = Widgets::WidgetSystem::Get().SetWidgetProperty(
                    widgetId,
                    "color",
                    Widgets::WidgetSystem::WidgetPropertyValue(base));
                if (!applied) {
                    ++m_Diagnostics.ApplyFailures;
                } else if (hasBinding) {
                    bindingService.NotifyTransitionMutation(widgetId, "color");
                }
            }

            for (const auto& [propertyPath, customAccumulator] : accumulators.Custom) {
                if (!HasValue(customAccumulator)) {
                    continue;
                }

                float customValue = GetAverage(customAccumulator, 0.0f);
                const float currentValue = ExtractFloat(
                    Widgets::WidgetSystem::Get().GetWidgetProperty(widgetId, propertyPath)).value_or(customValue);
                auto [mode, hasBinding] = resolveArbitration(propertyPath);
                if (hasBinding && mode == TransitionPropertyLockMode::BindingWins) {
                    continue;
                }
                if (hasBinding && mode == TransitionPropertyLockMode::Blend) {
                    customValue = glm::mix(currentValue, customValue, 0.5f);
                }

                const bool applied = Widgets::WidgetSystem::Get().SetWidgetProperty(
                    widgetId,
                    propertyPath,
                    Widgets::WidgetSystem::WidgetPropertyValue(customValue));
                if (!applied) {
                    ++m_Diagnostics.ApplyFailures;
                } else if (hasBinding) {
                    bindingService.NotifyTransitionMutation(widgetId, propertyPath);
                }
            }
        }

        for (const uint64_t handle : completedHandles) {
            auto transitionIt = m_Transitions.find(handle);
            if (transitionIt == m_Transitions.end()) {
                continue;
            }
            MarkTransitionCompleted(&transitionIt->second);
            if (transitionIt->second.CompletionCallback) {
                transitionIt->second.CompletionCallback(handle, true);
            }
            m_Transitions.erase(transitionIt);
        }

        for (const uint64_t handle : cancelledHandles) {
            auto transitionIt = m_Transitions.find(handle);
            if (transitionIt == m_Transitions.end()) {
                continue;
            }
            if (transitionIt->second.CompletionCallback) {
                transitionIt->second.CompletionCallback(handle, false);
            }
            m_Transitions.erase(transitionIt);
        }

        PromoteQueuedTransitions();
        RefreshDiagnostics();
    }

    std::optional<WidgetTransitionState> WidgetTransitionService::GetTransitionState(uint64_t transitionHandle) const {
        const auto transitionIt = m_Transitions.find(transitionHandle);
        if (transitionIt == m_Transitions.end()) {
            return std::nullopt;
        }
        return transitionIt->second.State;
    }

    std::vector<WidgetTransitionState> WidgetTransitionService::GetWidgetTransitionStates(
        const std::string& widgetId) const {
        std::vector<WidgetTransitionState> states;
        for (const auto& [handle, record] : m_Transitions) {
            (void)handle;
            if (record.State.WidgetId == widgetId) {
                states.push_back(record.State);
            }
        }

        std::sort(
            states.begin(),
            states.end(),
            [](const WidgetTransitionState& lhs, const WidgetTransitionState& rhs) {
                return lhs.Handle < rhs.Handle;
            });
        return states;
    }

    std::vector<WidgetTransitionState> WidgetTransitionService::GetAllTransitionStates() const {
        std::vector<WidgetTransitionState> states;
        states.reserve(m_Transitions.size());
        for (const auto& [handle, record] : m_Transitions) {
            (void)handle;
            states.push_back(record.State);
        }

        std::sort(
            states.begin(),
            states.end(),
            [](const WidgetTransitionState& lhs, const WidgetTransitionState& rhs) {
                return lhs.Handle < rhs.Handle;
            });
        return states;
    }

    bool WidgetTransitionService::ValidateTimeline(
        const WidgetTransitionTimeline& timeline,
        std::string* errorCode,
        std::string* message) const {
        if (timeline.Duration <= kFloatEpsilon || timeline.Channels.empty()) {
            if (errorCode != nullptr) {
                *errorCode = UI_TRANSITION_INVALID_TIMELINE;
            }
            if (message != nullptr) {
                *message = "Timeline must define a positive duration and at least one channel.";
            }
            return false;
        }

        for (const TransitionChannelDefinition& channel : timeline.Channels) {
            if (channel.StartTime < 0.0f || channel.StartTime > timeline.Duration) {
                if (errorCode != nullptr) {
                    *errorCode = UI_TRANSITION_INVALID_CHANNEL;
                }
                if (message != nullptr) {
                    *message = "Transition channel start time is outside timeline bounds.";
                }
                return false;
            }

            if (channel.Duration <= kFloatEpsilon) {
                if (errorCode != nullptr) {
                    *errorCode = UI_TRANSITION_INVALID_CHANNEL;
                }
                if (message != nullptr) {
                    *message = "Transition channel duration must be positive.";
                }
                return false;
            }

            if (channel.StartTime + channel.Duration > timeline.Duration + kFloatEpsilon) {
                if (errorCode != nullptr) {
                    *errorCode = UI_TRANSITION_INVALID_CHANNEL;
                }
                if (message != nullptr) {
                    *message = "Transition channel duration exceeds timeline bounds.";
                }
                return false;
            }

            if (channel.Target == TransitionChannelTarget::CustomProperty &&
                !IsSupportedPropertyPath(channel.PropertyPath)) {
                if (errorCode != nullptr) {
                    *errorCode = UI_TRANSITION_INVALID_CHANNEL;
                }
                if (message != nullptr) {
                    *message = "Custom transition channel path is invalid.";
                }
                return false;
            }
        }

        return true;
    }

    bool WidgetTransitionService::IsSupportedPropertyPath(const std::string& path) const {
        if (path.empty() || path.front() == '.' || path.back() == '.') {
            return false;
        }
        bool previousWasDot = false;
        for (const char c : path) {
            if (c == '.') {
                if (previousWasDot) {
                    return false;
                }
                previousWasDot = true;
                continue;
            }

            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
                return false;
            }
            previousWasDot = false;
        }
        return true;
    }

    std::string WidgetTransitionService::CanonicalizePropertyPath(std::string_view propertyPath) {
        if (propertyPath == "offset") {
            return "position";
        }
        if (propertyPath == "position" || propertyPath == "position.x" || propertyPath == "position.y") {
            return "position";
        }
        if (propertyPath == "scale" || propertyPath == "scale.x" || propertyPath == "scale.y") {
            return "scale";
        }
        if (propertyPath == "color" || propertyPath == "color.r" || propertyPath == "color.g" ||
            propertyPath == "color.b" || propertyPath == "color.a") {
            return "color";
        }
        if (propertyPath == "alpha") {
            return "alpha";
        }
        return std::string(propertyPath);
    }

    std::string WidgetTransitionService::MakeWidgetPropertyKey(
        const std::string& widgetId,
        std::string_view propertyPath) {
        return widgetId + "|" + CanonicalizePropertyPath(propertyPath);
    }

    void WidgetTransitionService::PromoteQueuedTransitions() {
        for (auto& [widgetId, queue] : m_QueuedByWidget) {
            const bool hasRunningTransition = std::any_of(
                m_Transitions.begin(),
                m_Transitions.end(),
                [&widgetId](const auto& pair) {
                    const TransitionRecord& record = pair.second;
                    return record.State.WidgetId == widgetId &&
                           record.State.State == TransitionRuntimeState::Running;
                });

            if (hasRunningTransition || queue.empty()) {
                continue;
            }

            const uint64_t nextHandle = queue.front();
            queue.pop_front();
            auto transitionIt = m_Transitions.find(nextHandle);
            if (transitionIt != m_Transitions.end()) {
                transitionIt->second.State.State = TransitionRuntimeState::Running;
                transitionIt->second.State.ElapsedTime = 0.0f;
            }
        }
    }

    void WidgetTransitionService::MarkTransitionCancelled(TransitionRecord* record) {
        if (record == nullptr || record->State.State == TransitionRuntimeState::Cancelled) {
            return;
        }
        record->State.State = TransitionRuntimeState::Cancelled;
        ++m_Diagnostics.CancelledTransitions;
    }

    void WidgetTransitionService::MarkTransitionCompleted(TransitionRecord* record) {
        if (record == nullptr || record->State.State == TransitionRuntimeState::Completed) {
            return;
        }
        record->State.State = TransitionRuntimeState::Completed;
        ++m_Diagnostics.CompletedTransitions;
    }

    void WidgetTransitionService::RefreshDiagnostics() {
        uint32_t activeTransitions = 0;
        uint32_t queuedTransitions = 0;
        for (const auto& [handle, record] : m_Transitions) {
            (void)handle;
            if (record.State.State == TransitionRuntimeState::Queued) {
                ++queuedTransitions;
            } else if (record.State.State == TransitionRuntimeState::Running) {
                ++activeTransitions;
            }
        }
        m_Diagnostics.ActiveTransitions = activeTransitions;
        m_Diagnostics.QueuedTransitions = queuedTransitions;
    }

    std::optional<WidgetTransitionTimeline> WidgetTransitionService::ParseTimelinePayload(
        const nlohmann::json& timelinePayload,
        std::string* errorMessage) const {
        if (!timelinePayload.is_object()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Timeline payload must be an object.";
            }
            return std::nullopt;
        }

        WidgetTransitionTimeline timeline;
        timeline.Duration = timelinePayload.value("duration", 0.0f);
        if (timeline.Duration <= kFloatEpsilon) {
            if (errorMessage != nullptr) {
                *errorMessage = "Timeline duration must be > 0.";
            }
            return std::nullopt;
        }

        const nlohmann::json channelsPayload = timelinePayload.value("channels", nlohmann::json::array());
        if (!channelsPayload.is_array() || channelsPayload.empty()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Timeline channels must be a non-empty array.";
            }
            return std::nullopt;
        }

        timeline.Channels.reserve(channelsPayload.size());
        for (const nlohmann::json& channelPayload : channelsPayload) {
            if (!channelPayload.is_object()) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Timeline channel entries must be objects.";
                }
                return std::nullopt;
            }

            const std::string property = channelPayload.value("property", std::string{});
            if (property.empty()) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Timeline channel property is required.";
                }
                return std::nullopt;
            }

            TransitionChannelDefinition channel;
            channel.PropertyPath = property;
            channel.From = channelPayload.value("from", 0.0f);
            channel.To = channelPayload.value("to", 0.0f);
            const float channelStart = channelPayload.contains("start")
                                           ? channelPayload.value("start", 0.0f)
                                           : channelPayload.value("startTime", 0.0f);
            channel.StartTime = glm::max(0.0f, channelStart);
            channel.Duration = channelPayload.value("duration", timeline.Duration - channel.StartTime);

            if (std::optional<TransitionChannelTarget> target = ParseChannelTarget(property)) {
                channel.Target = target.value();
            } else {
                channel.Target = TransitionChannelTarget::CustomProperty;
                if (!IsSupportedPropertyPath(channel.PropertyPath)) {
                    if (errorMessage != nullptr) {
                        *errorMessage = "Unsupported custom transition channel property path.";
                    }
                    return std::nullopt;
                }
            }

            const std::string easingStr = channelPayload.value("easing", "linear");
            const std::optional<TransitionEasing> easing = ParseEasing(easingStr);
            if (!easing.has_value()) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Unsupported easing function '" + easingStr + "'.";
                }
                return std::nullopt;
            }
            channel.Easing = easing.value();

            if (channel.Duration <= kFloatEpsilon ||
                channel.StartTime + channel.Duration > timeline.Duration + kFloatEpsilon) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Timeline channel duration must stay within timeline duration.";
                }
                return std::nullopt;
            }

            timeline.Channels.push_back(channel);
        }

        return timeline;
    }

} // namespace Animation
} // namespace UI
} // namespace Core

