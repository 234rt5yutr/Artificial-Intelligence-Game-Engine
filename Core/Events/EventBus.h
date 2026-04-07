#pragma once

// EventBus Core Implementation
// Thread-safe publish/subscribe event system

#include "Event.h"
#include <unordered_map>
#include <vector>
#include <queue>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <algorithm>

namespace Core {
namespace Events {

    //=========================================================================
    // Handler Entry
    //=========================================================================

    struct HandlerEntry {
        HandlerToken Token;
        EventHandler Handler;
        EventPriority Priority;
        bool Once;  // Remove after first invocation
    };

    //=========================================================================
    // EventBus Singleton
    //=========================================================================

    class EventBus {
    public:
        /// Get the singleton instance
        static EventBus& Get() {
            static EventBus instance;
            return instance;
        }

        // Non-copyable
        EventBus(const EventBus&) = delete;
        EventBus& operator=(const EventBus&) = delete;

        // ================================================================
        // Subscription Methods
        // ================================================================

        /// Subscribe to an event type with a generic handler
        template<typename T>
        HandlerToken Subscribe(TypedEventHandler<T> handler, 
                               EventPriority priority = EventPriority::Normal) {
            return SubscribeInternal(
                GetEventTypeId<T>(),
                [handler = std::move(handler)](EventBase& e) {
                    handler(static_cast<T&>(e));
                },
                priority,
                false
            );
        }

        /// Subscribe to an event type, handler will be removed after first call
        template<typename T>
        HandlerToken SubscribeOnce(TypedEventHandler<T> handler,
                                   EventPriority priority = EventPriority::Normal) {
            return SubscribeInternal(
                GetEventTypeId<T>(),
                [handler = std::move(handler)](EventBase& e) {
                    handler(static_cast<T&>(e));
                },
                priority,
                true
            );
        }

        /// Subscribe to all events (with filter)
        HandlerToken SubscribeAll(EventHandler handler, 
                                  EventFilter filter = nullptr,
                                  EventPriority priority = EventPriority::Normal) {
            HandlerToken token = m_NextToken.fetch_add(1);
            
            std::unique_lock lock(m_GlobalMutex);
            m_GlobalHandlers.push_back({token, std::move(handler), priority, false});
            
            if (filter) {
                m_GlobalFilters[token] = std::move(filter);
            }
            
            SortGlobalHandlers();
            return token;
        }

        /// Unsubscribe a handler by token
        void Unsubscribe(HandlerToken token) {
            if (token == InvalidHandlerToken) return;

            // Remove from type-specific handlers
            {
                std::unique_lock lock(m_TypeMutex);
                for (auto& [typeId, handlers] : m_TypeHandlers) {
                    handlers.erase(
                        std::remove_if(handlers.begin(), handlers.end(),
                            [token](const HandlerEntry& e) { return e.Token == token; }),
                        handlers.end()
                    );
                }
            }

            // Remove from global handlers
            {
                std::unique_lock lock(m_GlobalMutex);
                m_GlobalHandlers.erase(
                    std::remove_if(m_GlobalHandlers.begin(), m_GlobalHandlers.end(),
                        [token](const HandlerEntry& e) { return e.Token == token; }),
                    m_GlobalHandlers.end()
                );
                m_GlobalFilters.erase(token);
            }
        }

        /// Unsubscribe all handlers for a specific event type
        template<typename T>
        void UnsubscribeAll() {
            std::unique_lock lock(m_TypeMutex);
            m_TypeHandlers.erase(GetEventTypeId<T>());
        }

        // ================================================================
        // Publishing Methods
        // ================================================================

        /// Publish an event immediately (synchronous)
        template<typename T>
        void Publish(T& event) {
            static_assert(std::is_base_of_v<EventBase, T>, 
                         "T must derive from EventBase");
            
            PublishInternal(event);
        }

        /// Publish an event immediately (move ownership)
        template<typename T>
        void Publish(std::unique_ptr<T> event) {
            if (event) {
                PublishInternal(*event);
            }
        }

        /// Create and publish an event
        template<typename T, typename... Args>
        void Emit(Args&&... args) {
            T event(std::forward<Args>(args)...);
            PublishInternal(event);
        }

        // ================================================================
        // Deferred Publishing
        // ================================================================

        /// Queue an event for deferred processing
        template<typename T>
        void QueueEvent(std::unique_ptr<T> event) {
            if (!event) return;
            
            std::unique_lock lock(m_QueueMutex);
            m_EventQueue.push(std::move(event));
        }

        /// Create and queue an event
        template<typename T, typename... Args>
        void QueueEmit(Args&&... args) {
            QueueEvent(std::make_unique<T>(std::forward<Args>(args)...));
        }

        /// Process all queued events
        void ProcessQueue() {
            std::queue<EventPtr> toProcess;
            
            // Swap queue to minimize lock time
            {
                std::unique_lock lock(m_QueueMutex);
                std::swap(toProcess, m_EventQueue);
            }

            // Process events
            while (!toProcess.empty()) {
                auto& event = toProcess.front();
                if (event) {
                    PublishInternal(*event);
                }
                toProcess.pop();
            }
        }

        /// Get number of queued events
        size_t GetQueueSize() const {
            std::shared_lock lock(m_QueueMutex);
            return m_EventQueue.size();
        }

        /// Clear the event queue without processing
        void ClearQueue() {
            std::unique_lock lock(m_QueueMutex);
            std::queue<EventPtr> empty;
            std::swap(m_EventQueue, empty);
        }

        // ================================================================
        // Statistics
        // ================================================================

        struct Statistics {
            uint64_t TotalEventsPublished = 0;
            uint64_t TotalEventsQueued = 0;
            uint64_t TotalHandlersInvoked = 0;
            size_t RegisteredTypeHandlers = 0;
            size_t RegisteredGlobalHandlers = 0;
        };

        Statistics GetStatistics() const {
            Statistics stats;
            stats.TotalEventsPublished = m_TotalEventsPublished.load();
            stats.TotalEventsQueued = m_TotalEventsQueued.load();
            stats.TotalHandlersInvoked = m_TotalHandlersInvoked.load();
            
            {
                std::shared_lock lock(m_TypeMutex);
                for (const auto& [_, handlers] : m_TypeHandlers) {
                    stats.RegisteredTypeHandlers += handlers.size();
                }
            }
            
            {
                std::shared_lock lock(m_GlobalMutex);
                stats.RegisteredGlobalHandlers = m_GlobalHandlers.size();
            }
            
            return stats;
        }

        void ResetStatistics() {
            m_TotalEventsPublished = 0;
            m_TotalEventsQueued = 0;
            m_TotalHandlersInvoked = 0;
        }

        // ================================================================
        // Lifecycle
        // ================================================================

        /// Clear all handlers and queued events
        void Clear() {
            {
                std::unique_lock lock(m_TypeMutex);
                m_TypeHandlers.clear();
            }
            {
                std::unique_lock lock(m_GlobalMutex);
                m_GlobalHandlers.clear();
                m_GlobalFilters.clear();
            }
            ClearQueue();
        }

    private:
        EventBus() = default;
        ~EventBus() = default;

        // ================================================================
        // Internal Methods
        // ================================================================

        HandlerToken SubscribeInternal(EventTypeId typeId, EventHandler handler,
                                       EventPriority priority, bool once) {
            HandlerToken token = m_NextToken.fetch_add(1);
            
            std::unique_lock lock(m_TypeMutex);
            m_TypeHandlers[typeId].push_back({token, std::move(handler), priority, once});
            SortTypeHandlers(typeId);
            
            return token;
        }

        void PublishInternal(EventBase& event) {
            m_TotalEventsPublished++;

            EventTypeId typeId = event.GetTypeId();
            std::vector<HandlerEntry> handlersToInvoke;
            std::vector<HandlerToken> toRemove;

            // Collect type-specific handlers
            {
                std::shared_lock lock(m_TypeMutex);
                auto it = m_TypeHandlers.find(typeId);
                if (it != m_TypeHandlers.end()) {
                    handlersToInvoke = it->second;
                }
            }

            // Invoke type-specific handlers
            for (const auto& entry : handlersToInvoke) {
                if (!event.ShouldPropagate()) break;
                
                entry.Handler(event);
                m_TotalHandlersInvoked++;
                
                if (entry.Once) {
                    toRemove.push_back(entry.Token);
                }
            }

            // Invoke global handlers
            if (event.ShouldPropagate()) {
                std::vector<HandlerEntry> globalHandlers;
                {
                    std::shared_lock lock(m_GlobalMutex);
                    globalHandlers = m_GlobalHandlers;
                }

                for (const auto& entry : globalHandlers) {
                    if (!event.ShouldPropagate()) break;
                    
                    // Check filter
                    bool passFilter = true;
                    {
                        std::shared_lock lock(m_GlobalMutex);
                        auto filterIt = m_GlobalFilters.find(entry.Token);
                        if (filterIt != m_GlobalFilters.end()) {
                            passFilter = filterIt->second(event);
                        }
                    }
                    
                    if (passFilter) {
                        entry.Handler(event);
                        m_TotalHandlersInvoked++;
                        
                        if (entry.Once) {
                            toRemove.push_back(entry.Token);
                        }
                    }
                }
            }

            // Remove one-shot handlers
            for (auto token : toRemove) {
                Unsubscribe(token);
            }
        }

        void SortTypeHandlers(EventTypeId typeId) {
            // Must be called with m_TypeMutex held
            auto it = m_TypeHandlers.find(typeId);
            if (it != m_TypeHandlers.end()) {
                std::sort(it->second.begin(), it->second.end(),
                    [](const HandlerEntry& a, const HandlerEntry& b) {
                        return static_cast<int>(a.Priority) > static_cast<int>(b.Priority);
                    });
            }
        }

        void SortGlobalHandlers() {
            // Must be called with m_GlobalMutex held
            std::sort(m_GlobalHandlers.begin(), m_GlobalHandlers.end(),
                [](const HandlerEntry& a, const HandlerEntry& b) {
                    return static_cast<int>(a.Priority) > static_cast<int>(b.Priority);
                });
        }

        // ================================================================
        // Member Variables
        // ================================================================

        // Type-specific handlers
        mutable std::shared_mutex m_TypeMutex;
        std::unordered_map<EventTypeId, std::vector<HandlerEntry>> m_TypeHandlers;

        // Global handlers (receive all events)
        mutable std::shared_mutex m_GlobalMutex;
        std::vector<HandlerEntry> m_GlobalHandlers;
        std::unordered_map<HandlerToken, EventFilter> m_GlobalFilters;

        // Event queue for deferred processing
        mutable std::shared_mutex m_QueueMutex;
        std::queue<EventPtr> m_EventQueue;

        // Token generator
        std::atomic<HandlerToken> m_NextToken{1};

        // Statistics
        std::atomic<uint64_t> m_TotalEventsPublished{0};
        std::atomic<uint64_t> m_TotalEventsQueued{0};
        std::atomic<uint64_t> m_TotalHandlersInvoked{0};
    };

    // ========================================================================
    // Scoped Subscription Helper
    // ========================================================================

    /// RAII helper that automatically unsubscribes on destruction
    class ScopedSubscription {
    public:
        ScopedSubscription() : m_Token(InvalidHandlerToken) {}
        
        explicit ScopedSubscription(HandlerToken token) : m_Token(token) {}
        
        ScopedSubscription(ScopedSubscription&& other) noexcept 
            : m_Token(other.m_Token) {
            other.m_Token = InvalidHandlerToken;
        }
        
        ScopedSubscription& operator=(ScopedSubscription&& other) noexcept {
            if (this != &other) {
                Unsubscribe();
                m_Token = other.m_Token;
                other.m_Token = InvalidHandlerToken;
            }
            return *this;
        }
        
        ~ScopedSubscription() {
            Unsubscribe();
        }

        // Non-copyable
        ScopedSubscription(const ScopedSubscription&) = delete;
        ScopedSubscription& operator=(const ScopedSubscription&) = delete;

        HandlerToken GetToken() const { return m_Token; }
        
        void Release() { m_Token = InvalidHandlerToken; }
        
        void Unsubscribe() {
            if (m_Token != InvalidHandlerToken) {
                EventBus::Get().Unsubscribe(m_Token);
                m_Token = InvalidHandlerToken;
            }
        }

    private:
        HandlerToken m_Token;
    };

} // namespace Events
} // namespace Core
