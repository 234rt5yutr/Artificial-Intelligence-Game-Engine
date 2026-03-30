#pragma once

#include <functional>
#include <string>

namespace Core {

    enum class EventType {
        None = 0,
        WindowClose, WindowResize,
        KeyPressed, KeyReleased,
        MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled
    };

    class Event {
    public:
        virtual ~Event() = default;
        virtual EventType GetEventType() const = 0;
        virtual const char* GetName() const = 0;
        
        bool Handled = false;
    };

    #define EVENT_CLASS_TYPE(type) static EventType GetStaticType() { return EventType::type; }\
                                   virtual EventType GetEventType() const override { return GetStaticType(); }\
                                   virtual const char* GetName() const override { return #type; }

    class WindowCloseEvent : public Event {
    public:
        WindowCloseEvent() = default;
        EVENT_CLASS_TYPE(WindowClose)
    };

    class EventDispatcher {
    public:
        // Accept the event tightly by reference
        EventDispatcher(Event& event)
            : m_Event(event)
        {
        }

        // T is the Event Type
        // F is the function signature: bool(T&)
        template<typename T, typename F>
        bool Dispatch(const F& func)
        {
            if (m_Event.GetEventType() == T::GetStaticType())
            {
                m_Event.Handled |= func(static_cast<T&>(m_Event));
                return true;
            }
            return false;
        }
    private:
        Event& m_Event;
    };

} // namespace Core