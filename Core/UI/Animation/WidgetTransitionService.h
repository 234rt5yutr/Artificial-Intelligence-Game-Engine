#pragma once

#include "Core/UI/Animation/WidgetTransitionTypes.h"

#include <nlohmann/json.hpp>

#include <deque>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace Core {
namespace UI {
namespace Animation {

    class WidgetTransitionService {
    public:
        static WidgetTransitionService& Get();

        WidgetTransitionResult AnimateWidgetTransition(const WidgetTransitionRequest& request);
        WidgetTransitionResult AnimateWidgetTransition(
            const std::string& widgetId,
            const std::string& transitionId,
            const nlohmann::json& timelinePayload,
            TransitionInterruptionPolicy interruptionPolicy = TransitionInterruptionPolicy::CancelPrevious,
            bool interruptExisting = true);

        bool CancelTransition(uint64_t transitionHandle);
        uint32_t CancelWidgetTransitions(const std::string& widgetId);
        void ClearTransitions();

        void SetPropertyLockMode(
            const std::string& widgetId,
            std::string_view propertyPath,
            TransitionPropertyLockMode mode);
        TransitionPropertyLockMode GetPropertyLockMode(
            const std::string& widgetId,
            std::string_view propertyPath) const;

        void UpdateTransitions(float deltaTime);

        std::optional<WidgetTransitionState> GetTransitionState(uint64_t transitionHandle) const;
        std::vector<WidgetTransitionState> GetWidgetTransitionStates(const std::string& widgetId) const;
        const WidgetTransitionDiagnostics& GetDiagnostics() const { return m_Diagnostics; }

    private:
        struct TransitionRecord {
            WidgetTransitionState State;
            WidgetTransitionTimeline Timeline;
            std::function<void(uint64_t, bool)> CompletionCallback;
        };

        WidgetTransitionService() = default;

        bool ValidateTimeline(
            const WidgetTransitionTimeline& timeline,
            std::string* errorCode,
            std::string* message) const;
        bool IsSupportedPropertyPath(const std::string& path) const;
        static std::string CanonicalizePropertyPath(std::string_view propertyPath);
        static std::string MakeWidgetPropertyKey(
            const std::string& widgetId,
            std::string_view propertyPath);

        void PromoteQueuedTransitions();
        void MarkTransitionCancelled(TransitionRecord* record);
        void MarkTransitionCompleted(TransitionRecord* record);
        void RefreshDiagnostics();

        std::optional<WidgetTransitionTimeline> ParseTimelinePayload(
            const nlohmann::json& timelinePayload,
            std::string* errorMessage) const;

    private:
        uint64_t m_NextTransitionHandle = 1;
        std::unordered_map<uint64_t, TransitionRecord> m_Transitions;
        std::unordered_map<std::string, std::deque<uint64_t>> m_QueuedByWidget;
        std::unordered_map<std::string, TransitionPropertyLockMode> m_PropertyLockModes;
        WidgetTransitionDiagnostics m_Diagnostics;
    };

} // namespace Animation
} // namespace UI
} // namespace Core

