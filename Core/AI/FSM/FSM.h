#pragma once

// Finite State Machine Core Classes
// State, Transition, and FSM container classes

#include "../BehaviorTree/Blackboard.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace Core {
namespace AI {

    using Json = nlohmann::json;

    // Forward declarations
    class FSM;
    class FSMState;
    class FSMTransition;

    // ============================================================================
    // FSM State
    // ============================================================================

    class FSMState {
    public:
        using UpdateFunc = std::function<void(float, Blackboard&)>;
        using EnterFunc = std::function<void(Blackboard&)>;
        using ExitFunc = std::function<void(Blackboard&)>;

        explicit FSMState(const std::string& name)
            : m_Name(name) {}

        virtual ~FSMState() = default;

        // Lifecycle methods
        virtual void OnEnter(Blackboard& blackboard) {
            if (m_EnterFunc) {
                m_EnterFunc(blackboard);
            }
        }

        virtual void OnUpdate(float deltaTime, Blackboard& blackboard) {
            if (m_UpdateFunc) {
                m_UpdateFunc(deltaTime, blackboard);
            }
        }

        virtual void OnExit(Blackboard& blackboard) {
            if (m_ExitFunc) {
                m_ExitFunc(blackboard);
            }
        }

        // Set callback functions
        void SetOnEnter(EnterFunc func) { m_EnterFunc = std::move(func); }
        void SetOnUpdate(UpdateFunc func) { m_UpdateFunc = std::move(func); }
        void SetOnExit(ExitFunc func) { m_ExitFunc = std::move(func); }

        // Name accessor
        const std::string& GetName() const { return m_Name; }

        // Add outgoing transition
        void AddTransition(std::shared_ptr<FSMTransition> transition) {
            m_Transitions.push_back(transition);
        }

        // Get all transitions from this state
        const std::vector<std::shared_ptr<FSMTransition>>& GetTransitions() const {
            return m_Transitions;
        }

        // Serialization
        virtual Json ToJson() const {
            Json j;
            j["name"] = m_Name;
            j["type"] = "FSMState";
            
            Json transitionNames = Json::array();
            for (const auto& t : m_Transitions) {
                if (t) {
                    transitionNames.push_back(t->GetName());
                }
            }
            j["transitions"] = transitionNames;
            
            return j;
        }

    protected:
        std::string m_Name;
        std::vector<std::shared_ptr<FSMTransition>> m_Transitions;
        UpdateFunc m_UpdateFunc;
        EnterFunc m_EnterFunc;
        ExitFunc m_ExitFunc;
    };

    // ============================================================================
    // FSM Transition
    // ============================================================================

    class FSMTransition {
    public:
        using ConditionFunc = std::function<bool(const Blackboard&)>;
        using ActionFunc = std::function<void(Blackboard&)>;

        FSMTransition(const std::string& name,
                      const std::string& fromState,
                      const std::string& toState)
            : m_Name(name)
            , m_FromState(fromState)
            , m_ToState(toState) {}

        virtual ~FSMTransition() = default;

        // Check if transition condition is met
        virtual bool CanTransition(const Blackboard& blackboard) const {
            if (m_ConditionFunc) {
                return m_ConditionFunc(blackboard);
            }
            return false;
        }

        // Execute transition action (if any)
        virtual void Execute(Blackboard& blackboard) {
            if (m_ActionFunc) {
                m_ActionFunc(blackboard);
            }
        }

        // Setters
        void SetCondition(ConditionFunc func) { m_ConditionFunc = std::move(func); }
        void SetAction(ActionFunc func) { m_ActionFunc = std::move(func); }

        // Getters
        const std::string& GetName() const { return m_Name; }
        const std::string& GetFromState() const { return m_FromState; }
        const std::string& GetToState() const { return m_ToState; }

        // Serialization
        virtual Json ToJson() const {
            Json j;
            j["name"] = m_Name;
            j["from"] = m_FromState;
            j["to"] = m_ToState;
            return j;
        }

    protected:
        std::string m_Name;
        std::string m_FromState;
        std::string m_ToState;
        ConditionFunc m_ConditionFunc;
        ActionFunc m_ActionFunc;
    };

    // ============================================================================
    // Event-based Transition
    // ============================================================================

    class EventTransition : public FSMTransition {
    public:
        EventTransition(const std::string& name,
                        const std::string& fromState,
                        const std::string& toState,
                        const std::string& eventName)
            : FSMTransition(name, fromState, toState)
            , m_EventName(eventName)
            , m_EventTriggered(false) {}

        // Check if the event was triggered
        bool CanTransition(const Blackboard& blackboard) const override {
            // Check base condition first
            if (m_ConditionFunc && !m_ConditionFunc(blackboard)) {
                return false;
            }
            return m_EventTriggered;
        }

        // Trigger the event
        void TriggerEvent() { m_EventTriggered = true; }
        
        // Reset event state
        void ResetEvent() { m_EventTriggered = false; }

        const std::string& GetEventName() const { return m_EventName; }

        Json ToJson() const override {
            Json j = FSMTransition::ToJson();
            j["type"] = "EventTransition";
            j["event"] = m_EventName;
            return j;
        }

    private:
        std::string m_EventName;
        bool m_EventTriggered;
    };

    // ============================================================================
    // FSM Container
    // ============================================================================

    class FSM {
    public:
        explicit FSM(const std::string& name = "FSM")
            : m_Name(name)
            , m_CurrentState(nullptr) {}

        ~FSM() = default;

        // Non-copyable but movable
        FSM(const FSM&) = delete;
        FSM& operator=(const FSM&) = delete;
        FSM(FSM&&) = default;
        FSM& operator=(FSM&&) = default;

        // ================================================================
        // State Management
        // ================================================================

        // Add a state
        void AddState(std::shared_ptr<FSMState> state) {
            if (state) {
                m_States[state->GetName()] = state;
            }
        }

        // Get a state by name
        FSMState* GetState(const std::string& name) {
            auto it = m_States.find(name);
            return it != m_States.end() ? it->second.get() : nullptr;
        }

        // Check if state exists
        bool HasState(const std::string& name) const {
            return m_States.contains(name);
        }

        // Get all state names
        std::vector<std::string> GetStateNames() const {
            std::vector<std::string> names;
            names.reserve(m_States.size());
            for (const auto& [name, _] : m_States) {
                names.push_back(name);
            }
            return names;
        }

        // Set initial state
        void SetInitialState(const std::string& stateName) {
            m_InitialStateName = stateName;
        }

        // ================================================================
        // Transition Management
        // ================================================================

        // Add a transition
        void AddTransition(std::shared_ptr<FSMTransition> transition) {
            if (!transition) return;
            
            m_Transitions[transition->GetName()] = transition;
            
            // Add to source state
            auto* fromState = GetState(transition->GetFromState());
            if (fromState) {
                fromState->AddTransition(transition);
            }
        }

        // Get a transition by name
        FSMTransition* GetTransition(const std::string& name) {
            auto it = m_Transitions.find(name);
            return it != m_Transitions.end() ? it->second.get() : nullptr;
        }

        // ================================================================
        // Execution
        // ================================================================

        // Start the FSM
        void Start() {
            if (m_CurrentState) return; // Already started
            
            auto it = m_States.find(m_InitialStateName);
            if (it != m_States.end()) {
                m_CurrentState = it->second.get();
                m_CurrentState->OnEnter(m_Blackboard);
            }
        }

        // Stop the FSM
        void Stop() {
            if (m_CurrentState) {
                m_CurrentState->OnExit(m_Blackboard);
                m_CurrentState = nullptr;
            }
        }

        // Update the FSM
        void Update(float deltaTime) {
            if (!m_CurrentState) return;

            // Check transitions
            for (const auto& transition : m_CurrentState->GetTransitions()) {
                if (transition && transition->CanTransition(m_Blackboard)) {
                    TransitionTo(transition.get());
                    break;
                }
            }

            // Update current state
            if (m_CurrentState) {
                m_CurrentState->OnUpdate(deltaTime, m_Blackboard);
            }
        }

        // Force transition to a specific state
        bool ForceTransition(const std::string& stateName) {
            auto it = m_States.find(stateName);
            if (it == m_States.end()) return false;

            if (m_CurrentState) {
                m_CurrentState->OnExit(m_Blackboard);
            }

            m_CurrentState = it->second.get();
            m_CurrentState->OnEnter(m_Blackboard);
            
            return true;
        }

        // Send an event to the FSM
        void SendEvent(const std::string& eventName) {
            if (!m_CurrentState) return;

            for (const auto& transition : m_CurrentState->GetTransitions()) {
                auto* eventTrans = dynamic_cast<EventTransition*>(transition.get());
                if (eventTrans && eventTrans->GetEventName() == eventName) {
                    eventTrans->TriggerEvent();
                }
            }
        }

        // ================================================================
        // State Access
        // ================================================================

        // Get current state
        const FSMState* GetCurrentState() const { return m_CurrentState; }
        FSMState* GetCurrentState() { return m_CurrentState; }

        // Get current state name
        std::string GetCurrentStateName() const {
            return m_CurrentState ? m_CurrentState->GetName() : "";
        }

        // Check if in a specific state
        bool IsInState(const std::string& stateName) const {
            return m_CurrentState && m_CurrentState->GetName() == stateName;
        }

        // Blackboard access
        Blackboard& GetBlackboard() { return m_Blackboard; }
        const Blackboard& GetBlackboard() const { return m_Blackboard; }

        // Name accessor
        const std::string& GetName() const { return m_Name; }
        void SetName(const std::string& name) { m_Name = name; }

        // ================================================================
        // Serialization
        // ================================================================

        Json ToJson() const {
            Json j;
            j["name"] = m_Name;
            j["initialState"] = m_InitialStateName;
            j["currentState"] = GetCurrentStateName();
            j["blackboard"] = m_Blackboard.ToJson();

            Json statesJson = Json::array();
            for (const auto& [name, state] : m_States) {
                if (state) {
                    statesJson.push_back(state->ToJson());
                }
            }
            j["states"] = statesJson;

            Json transitionsJson = Json::array();
            for (const auto& [name, transition] : m_Transitions) {
                if (transition) {
                    transitionsJson.push_back(transition->ToJson());
                }
            }
            j["transitions"] = transitionsJson;

            return j;
        }

        // ================================================================
        // Debug
        // ================================================================

        // Get transition history (last N transitions)
        const std::vector<std::string>& GetTransitionHistory() const {
            return m_TransitionHistory;
        }

        void SetHistorySize(size_t size) {
            m_MaxHistorySize = size;
            while (m_TransitionHistory.size() > m_MaxHistorySize) {
                m_TransitionHistory.erase(m_TransitionHistory.begin());
            }
        }

    private:
        std::string m_Name;
        std::string m_InitialStateName;
        Blackboard m_Blackboard;

        std::unordered_map<std::string, std::shared_ptr<FSMState>> m_States;
        std::unordered_map<std::string, std::shared_ptr<FSMTransition>> m_Transitions;

        FSMState* m_CurrentState = nullptr;

        std::vector<std::string> m_TransitionHistory;
        size_t m_MaxHistorySize = 10;

        void TransitionTo(FSMTransition* transition) {
            if (!transition) return;

            auto* targetState = GetState(transition->GetToState());
            if (!targetState) return;

            // Exit current state
            if (m_CurrentState) {
                m_CurrentState->OnExit(m_Blackboard);
            }

            // Execute transition action
            transition->Execute(m_Blackboard);

            // Reset event transitions
            auto* eventTrans = dynamic_cast<EventTransition*>(transition);
            if (eventTrans) {
                eventTrans->ResetEvent();
            }

            // Record history
            std::string historyEntry = (m_CurrentState ? m_CurrentState->GetName() : "null") 
                                     + " -> " + targetState->GetName()
                                     + " (" + transition->GetName() + ")";
            m_TransitionHistory.push_back(historyEntry);
            if (m_TransitionHistory.size() > m_MaxHistorySize) {
                m_TransitionHistory.erase(m_TransitionHistory.begin());
            }

            // Enter new state
            m_CurrentState = targetState;
            m_CurrentState->OnEnter(m_Blackboard);
        }
    };

} // namespace AI
} // namespace Core
