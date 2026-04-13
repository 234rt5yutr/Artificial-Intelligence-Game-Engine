#pragma once

#include "Core/Animation/AnimationRuntimeTypes.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace Core::Animation {

struct QueuedAnimationEvent {
    std::string ParameterName;
    EventSyncMode Mode = EventSyncMode::NextUpdate;
    uint64_t Sequence = 0;
};

class AnimationEventSynchronizer {
public:
    static AnimationEventSynchronizer& Get();

    uint64_t QueueParameterEvent(
        const std::string& parameterName,
        EventSyncMode mode);

    std::vector<QueuedAnimationEvent> ConsumeImmediateEvents();
    std::vector<QueuedAnimationEvent> ConsumeTransitionBoundaryEvents();
    void PromoteNextUpdateEvents();
    void Clear();

private:
    std::vector<QueuedAnimationEvent> ConsumeQueue(
        std::deque<QueuedAnimationEvent>& queue);

    std::mutex m_Mutex;
    std::deque<QueuedAnimationEvent> m_ImmediateEvents;
    std::deque<QueuedAnimationEvent> m_NextUpdateEvents;
    std::deque<QueuedAnimationEvent> m_TransitionBoundaryEvents;
    uint64_t m_SequenceCounter = 0;
};

} // namespace Core::Animation
