#include "Core/Animation/Events/AnimationEventSynchronizer.h"

namespace Core::Animation {

AnimationEventSynchronizer& AnimationEventSynchronizer::Get() {
    static AnimationEventSynchronizer instance;
    return instance;
}

uint64_t AnimationEventSynchronizer::QueueParameterEvent(
    const std::string& parameterName,
    const EventSyncMode mode) {
    std::scoped_lock lock(m_Mutex);

    QueuedAnimationEvent event;
    event.ParameterName = parameterName;
    event.Mode = mode;
    event.Sequence = ++m_SequenceCounter;

    switch (mode) {
        case EventSyncMode::Immediate:
            m_ImmediateEvents.push_back(event);
            break;
        case EventSyncMode::TransitionBoundary:
            m_TransitionBoundaryEvents.push_back(event);
            break;
        case EventSyncMode::NextUpdate:
        default:
            m_NextUpdateEvents.push_back(event);
            break;
    }

    return event.Sequence;
}

std::vector<QueuedAnimationEvent> AnimationEventSynchronizer::ConsumeQueue(
    std::deque<QueuedAnimationEvent>& queue) {
    std::vector<QueuedAnimationEvent> events;
    events.reserve(queue.size());
    while (!queue.empty()) {
        events.push_back(std::move(queue.front()));
        queue.pop_front();
    }
    return events;
}

std::vector<QueuedAnimationEvent> AnimationEventSynchronizer::ConsumeImmediateEvents() {
    std::scoped_lock lock(m_Mutex);
    return ConsumeQueue(m_ImmediateEvents);
}

std::vector<QueuedAnimationEvent> AnimationEventSynchronizer::ConsumeTransitionBoundaryEvents() {
    std::scoped_lock lock(m_Mutex);
    return ConsumeQueue(m_TransitionBoundaryEvents);
}

void AnimationEventSynchronizer::PromoteNextUpdateEvents() {
    std::scoped_lock lock(m_Mutex);
    while (!m_NextUpdateEvents.empty()) {
        m_ImmediateEvents.push_back(std::move(m_NextUpdateEvents.front()));
        m_NextUpdateEvents.pop_front();
    }
}

void AnimationEventSynchronizer::Clear() {
    std::scoped_lock lock(m_Mutex);
    m_ImmediateEvents.clear();
    m_NextUpdateEvents.clear();
    m_TransitionBoundaryEvents.clear();
}

} // namespace Core::Animation
