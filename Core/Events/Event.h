#pragma once

// Event Base and Type System
// Core event infrastructure for the event bus

#include <string>
#include <typeindex>
#include <chrono>
#include <memory>
#include <cstdint>
#include <atomic>
#include <nlohmann/json.hpp>

namespace Core {
namespace Events {

    using Json = nlohmann::json;

    //=========================================================================
    // Event Type ID System
    //=========================================================================

    using EventTypeId = std::size_t;

    /// Get a unique type ID for an event type at compile time
    template<typename T>
    inline EventTypeId GetEventTypeId() {
        return std::type_index(typeid(T)).hash_code();
    }

    //=========================================================================
    // Event Priority
    //=========================================================================

    enum class EventPriority : uint8_t {
        Low = 0,
        Normal = 1,
        High = 2,
        Critical = 3
    };

    //=========================================================================
    // Event Base Class
    //=========================================================================

    class EventBase {
    public:
        EventBase() 
            : m_Timestamp(std::chrono::high_resolution_clock::now())
            , m_Priority(EventPriority::Normal)
            , m_Handled(false)
            , m_Propagate(true)
            , m_Id(s_NextEventId.fetch_add(1)) {}

        virtual ~EventBase() = default;

        // Non-copyable, movable
        EventBase(const EventBase&) = delete;
        EventBase& operator=(const EventBase&) = delete;
        EventBase(EventBase&&) = default;
        EventBase& operator=(EventBase&&) = default;

        // ================================================================
        // Type Information
        // ================================================================

        /// Get the event type ID
        virtual EventTypeId GetTypeId() const = 0;

        /// Get human-readable event type name
        virtual const char* GetTypeName() const = 0;

        /// Get event category (for filtering)
        virtual const char* GetCategory() const { return "General"; }

        // ================================================================
        // Event Metadata
        // ================================================================

        /// Unique event instance ID
        uint64_t GetId() const { return m_Id; }

        /// Event timestamp
        auto GetTimestamp() const { return m_Timestamp; }

        /// Event priority
        EventPriority GetPriority() const { return m_Priority; }
        void SetPriority(EventPriority priority) { m_Priority = priority; }

        // ================================================================
        // Event Handling State
        // ================================================================

        /// Whether this event has been handled
        bool IsHandled() const { return m_Handled; }
        void SetHandled(bool handled = true) { m_Handled = handled; }

        /// Whether this event should continue propagating
        bool ShouldPropagate() const { return m_Propagate; }
        void StopPropagation() { m_Propagate = false; }

        // ================================================================
        // Serialization
        // ================================================================

        /// Serialize event to JSON
        virtual Json ToJson() const {
            Json j;
            j["type"] = GetTypeName();
            j["id"] = m_Id;
            j["category"] = GetCategory();
            j["priority"] = static_cast<int>(m_Priority);
            return j;
        }

        /// Get a debug string representation
        virtual std::string ToString() const {
            return std::string(GetTypeName()) + " [" + std::to_string(m_Id) + "]";
        }

    protected:
        std::chrono::high_resolution_clock::time_point m_Timestamp;
        EventPriority m_Priority;
        bool m_Handled;
        bool m_Propagate;
        uint64_t m_Id;

        static inline std::atomic<uint64_t> s_NextEventId{1};
    };

    //=========================================================================
    // Typed Event Template
    //=========================================================================

    /// Base template for creating typed events
    template<typename Derived>
    class Event : public EventBase {
    public:
        /// Get the static type ID for this event type
        static EventTypeId StaticTypeId() {
            return GetEventTypeId<Derived>();
        }

        /// Get the event type ID (virtual)
        EventTypeId GetTypeId() const override {
            return StaticTypeId();
        }

        /// Get the event type name (must be overridden in derived)
        const char* GetTypeName() const override {
            // Default implementation - derived classes should override
            return typeid(Derived).name();
        }
    };

    //=========================================================================
    // Event Smart Pointers
    //=========================================================================

    using EventPtr = std::unique_ptr<EventBase>;
    using SharedEventPtr = std::shared_ptr<EventBase>;

    /// Create an event with move semantics
    template<typename T, typename... Args>
    EventPtr MakeEvent(Args&&... args) {
        return std::make_unique<T>(std::forward<Args>(args)...);
    }

    /// Create a shared event
    template<typename T, typename... Args>
    SharedEventPtr MakeSharedEvent(Args&&... args) {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }

    //=========================================================================
    // Event Handler Types
    //=========================================================================

    /// Generic event handler signature
    using EventHandler = std::function<void(EventBase&)>;

    /// Typed event handler template
    template<typename T>
    using TypedEventHandler = std::function<void(T&)>;

    //=========================================================================
    // Handler Token (for unsubscription)
    //=========================================================================

    using HandlerToken = uint64_t;
    constexpr HandlerToken InvalidHandlerToken = 0;

    //=========================================================================
    // Event Filter
    //=========================================================================

    /// Predicate for filtering events
    using EventFilter = std::function<bool(const EventBase&)>;

    /// Filter builder helpers
    namespace Filters {

        /// Match events by category
        inline EventFilter ByCategory(const std::string& category) {
            return [category](const EventBase& e) {
                return std::string(e.GetCategory()) == category;
            };
        }

        /// Match events by priority
        inline EventFilter ByPriority(EventPriority priority) {
            return [priority](const EventBase& e) {
                return e.GetPriority() >= priority;
            };
        }

        /// Match events by type
        template<typename T>
        inline EventFilter ByType() {
            return [](const EventBase& e) {
                return e.GetTypeId() == GetEventTypeId<T>();
            };
        }

        /// Combine filters with AND
        inline EventFilter And(EventFilter a, EventFilter b) {
            return [a = std::move(a), b = std::move(b)](const EventBase& e) {
                return a(e) && b(e);
            };
        }

        /// Combine filters with OR
        inline EventFilter Or(EventFilter a, EventFilter b) {
            return [a = std::move(a), b = std::move(b)](const EventBase& e) {
                return a(e) || b(e);
            };
        }

        /// Negate a filter
        inline EventFilter Not(EventFilter f) {
            return [f = std::move(f)](const EventBase& e) {
                return !f(e);
            };
        }
    }

} // namespace Events
} // namespace Core
