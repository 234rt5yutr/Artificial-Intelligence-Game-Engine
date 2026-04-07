#pragma once

// FSM Builder and Manager
// Fluent builder API and singleton manager for FSMs

#include "FSM.h"
#include <mutex>

namespace Core {
namespace AI {

    // ============================================================================
    // FSM Builder - Fluent API
    // ============================================================================

    class FSMBuilder {
    public:
        explicit FSMBuilder(const std::string& name = "FSM")
            : m_FSM(std::make_unique<FSM>(name)) {}

        // ================================================================
        // State Building
        // ================================================================

        // Add a state with callbacks
        FSMBuilder& State(const std::string& name) {
            m_CurrentState = std::make_shared<FSMState>(name);
            m_FSM->AddState(m_CurrentState);
            return *this;
        }

        // Set OnEnter callback for current state
        FSMBuilder& OnEnter(FSMState::EnterFunc func) {
            if (m_CurrentState) {
                m_CurrentState->SetOnEnter(std::move(func));
            }
            return *this;
        }

        // Set OnUpdate callback for current state
        FSMBuilder& OnUpdate(FSMState::UpdateFunc func) {
            if (m_CurrentState) {
                m_CurrentState->SetOnUpdate(std::move(func));
            }
            return *this;
        }

        // Set OnExit callback for current state
        FSMBuilder& OnExit(FSMState::ExitFunc func) {
            if (m_CurrentState) {
                m_CurrentState->SetOnExit(std::move(func));
            }
            return *this;
        }

        // Set initial state
        FSMBuilder& InitialState(const std::string& stateName) {
            m_FSM->SetInitialState(stateName);
            return *this;
        }

        // ================================================================
        // Transition Building
        // ================================================================

        // Add a condition-based transition
        FSMBuilder& Transition(const std::string& name,
                               const std::string& fromState,
                               const std::string& toState) {
            m_CurrentTransition = std::make_shared<FSMTransition>(name, fromState, toState);
            m_FSM->AddTransition(m_CurrentTransition);
            return *this;
        }

        // Add an event-based transition
        FSMBuilder& EventTransition(const std::string& name,
                                     const std::string& fromState,
                                     const std::string& toState,
                                     const std::string& eventName) {
            auto transition = std::make_shared<AI::EventTransition>(name, fromState, toState, eventName);
            m_CurrentTransition = transition;
            m_FSM->AddTransition(transition);
            return *this;
        }

        // Set condition for current transition
        FSMBuilder& When(FSMTransition::ConditionFunc condition) {
            if (m_CurrentTransition) {
                m_CurrentTransition->SetCondition(std::move(condition));
            }
            return *this;
        }

        // Set action for current transition
        FSMBuilder& Do(FSMTransition::ActionFunc action) {
            if (m_CurrentTransition) {
                m_CurrentTransition->SetAction(std::move(action));
            }
            return *this;
        }

        // ================================================================
        // Build
        // ================================================================

        // Build and return the FSM
        std::unique_ptr<FSM> Build() {
            return std::move(m_FSM);
        }

    private:
        std::unique_ptr<FSM> m_FSM;
        std::shared_ptr<FSMState> m_CurrentState;
        std::shared_ptr<FSMTransition> m_CurrentTransition;
    };

    // ============================================================================
    // FSM Manager - Singleton
    // ============================================================================

    class FSMManager {
    public:
        // Get the singleton instance
        static FSMManager& Get() {
            static FSMManager instance;
            return instance;
        }

        // Deleted copy/move operations
        FSMManager(const FSMManager&) = delete;
        FSMManager& operator=(const FSMManager&) = delete;
        FSMManager(FSMManager&&) = delete;
        FSMManager& operator=(FSMManager&&) = delete;

        // ================================================================
        // Template Management
        // ================================================================

        // Register an FSM template
        void RegisterTemplate(const std::string& id, std::unique_ptr<FSM> fsm) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Templates[id] = std::move(fsm);
        }

        // Register a factory function for lazy creation
        void RegisterTemplateFactory(const std::string& id,
                                     std::function<std::unique_ptr<FSM>()> factory) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Factories[id] = std::move(factory);
        }

        // Check if template exists
        bool HasTemplate(const std::string& id) const {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return m_Templates.contains(id) || m_Factories.contains(id);
        }

        // Get a template (read-only)
        const FSM* GetTemplate(const std::string& id) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            EnsureTemplateCreated(id);
            
            auto it = m_Templates.find(id);
            return it != m_Templates.end() ? it->second.get() : nullptr;
        }

        // Unregister a template
        void UnregisterTemplate(const std::string& id) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Templates.erase(id);
            m_Factories.erase(id);
        }

        // Get all template IDs
        std::vector<std::string> GetTemplateIds() const {
            std::lock_guard<std::mutex> lock(m_Mutex);
            std::vector<std::string> ids;
            ids.reserve(m_Templates.size() + m_Factories.size());
            
            for (const auto& [id, _] : m_Templates) {
                ids.push_back(id);
            }
            for (const auto& [id, _] : m_Factories) {
                if (!m_Templates.contains(id)) {
                    ids.push_back(id);
                }
            }
            
            return ids;
        }

        // ================================================================
        // Instance Management
        // ================================================================

        // Create a new instance from a template
        std::unique_ptr<FSM> CreateInstance(const std::string& templateId) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            
            // Try factory first
            auto factoryIt = m_Factories.find(templateId);
            if (factoryIt != m_Factories.end()) {
                return factoryIt->second();
            }
            
            // For FSM we need the factory since state callbacks can't be serialized
            return nullptr;
        }

        // Assign an FSM instance to an entity
        void AssignToEntity(uint32_t entityId, std::unique_ptr<FSM> fsm) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_EntityFSMs[entityId] = std::move(fsm);
        }

        // Create instance from template and assign to entity
        bool AssignTemplateToEntity(uint32_t entityId, const std::string& templateId) {
            auto fsm = CreateInstance(templateId);
            if (fsm) {
                AssignToEntity(entityId, std::move(fsm));
                return true;
            }
            return false;
        }

        // Get the FSM assigned to an entity
        FSM* GetEntityFSM(uint32_t entityId) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            auto it = m_EntityFSMs.find(entityId);
            return it != m_EntityFSMs.end() ? it->second.get() : nullptr;
        }

        // Remove FSM from entity
        void RemoveFromEntity(uint32_t entityId) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_EntityFSMs.erase(entityId);
        }

        // Get all entities with FSMs
        std::vector<uint32_t> GetEntitiesWithFSMs() const {
            std::lock_guard<std::mutex> lock(m_Mutex);
            std::vector<uint32_t> entities;
            entities.reserve(m_EntityFSMs.size());
            
            for (const auto& [entityId, _] : m_EntityFSMs) {
                entities.push_back(entityId);
            }
            
            return entities;
        }

        // ================================================================
        // Lifecycle
        // ================================================================

        // Clear all templates and instances
        void Clear() {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Templates.clear();
            m_Factories.clear();
            m_EntityFSMs.clear();
        }

        // Get statistics
        struct Stats {
            size_t templateCount;
            size_t factoryCount;
            size_t entityFSMCount;
        };

        Stats GetStats() const {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return Stats{
                m_Templates.size(),
                m_Factories.size(),
                m_EntityFSMs.size()
            };
        }

    private:
        FSMManager() = default;
        ~FSMManager() = default;

        mutable std::mutex m_Mutex;

        std::unordered_map<std::string, std::unique_ptr<FSM>> m_Templates;
        std::unordered_map<std::string, std::function<std::unique_ptr<FSM>()>> m_Factories;
        std::unordered_map<uint32_t, std::unique_ptr<FSM>> m_EntityFSMs;

        void EnsureTemplateCreated(const std::string& id) {
            if (!m_Templates.contains(id) && m_Factories.contains(id)) {
                m_Templates[id] = m_Factories[id]();
            }
        }
    };

    // ============================================================================
    // Common FSM Patterns
    // ============================================================================
    namespace FSMPatterns {

        // Create a simple enemy AI FSM
        inline std::unique_ptr<FSM> CreateEnemyAI(
            std::function<bool(const Blackboard&)> canSeePlayer,
            std::function<bool(const Blackboard&)> isInRange,
            std::function<void(float, Blackboard&)> patrolUpdate,
            std::function<void(float, Blackboard&)> chaseUpdate,
            std::function<void(float, Blackboard&)> attackUpdate) {
            
            return FSMBuilder("EnemyAI")
                // States
                .State("Patrol")
                    .OnUpdate(patrolUpdate)
                .State("Chase")
                    .OnUpdate(chaseUpdate)
                .State("Attack")
                    .OnUpdate(attackUpdate)
                
                // Transitions
                .Transition("PatrolToChase", "Patrol", "Chase")
                    .When(canSeePlayer)
                .Transition("ChaseToAttack", "Chase", "Attack")
                    .When(isInRange)
                .Transition("AttackToChase", "Attack", "Chase")
                    .When([isInRange](const Blackboard& bb) { return !isInRange(bb); })
                .Transition("ChaseToPatrol", "Chase", "Patrol")
                    .When([canSeePlayer](const Blackboard& bb) { return !canSeePlayer(bb); })
                
                .InitialState("Patrol")
                .Build();
        }

        // Create a simple door FSM
        inline std::unique_ptr<FSM> CreateDoorFSM(
            std::function<void(Blackboard&)> openDoor,
            std::function<void(Blackboard&)> closeDoor) {
            
            return FSMBuilder("DoorFSM")
                .State("Closed")
                    .OnEnter(closeDoor)
                .State("Open")
                    .OnEnter(openDoor)
                
                .EventTransition("OpenDoor", "Closed", "Open", "interact")
                .EventTransition("CloseDoor", "Open", "Closed", "interact")
                
                .InitialState("Closed")
                .Build();
        }

        // Create a simple character state machine
        inline std::unique_ptr<FSM> CreateCharacterFSM(
            std::function<bool(const Blackboard&)> isMoving,
            std::function<bool(const Blackboard&)> isJumping,
            std::function<bool(const Blackboard&)> isGrounded) {
            
            return FSMBuilder("CharacterFSM")
                .State("Idle")
                .State("Walking")
                .State("Jumping")
                .State("Falling")
                
                .Transition("IdleToWalking", "Idle", "Walking")
                    .When(isMoving)
                .Transition("WalkingToIdle", "Walking", "Idle")
                    .When([isMoving](const Blackboard& bb) { return !isMoving(bb); })
                .Transition("IdleToJumping", "Idle", "Jumping")
                    .When(isJumping)
                .Transition("WalkingToJumping", "Walking", "Jumping")
                    .When(isJumping)
                .Transition("JumpingToFalling", "Jumping", "Falling")
                    .When([isGrounded](const Blackboard& bb) { return !isGrounded(bb); })
                .Transition("FallingToIdle", "Falling", "Idle")
                    .When([isGrounded, isMoving](const Blackboard& bb) { 
                        return isGrounded(bb) && !isMoving(bb); 
                    })
                .Transition("FallingToWalking", "Falling", "Walking")
                    .When([isGrounded, isMoving](const Blackboard& bb) { 
                        return isGrounded(bb) && isMoving(bb); 
                    })
                
                .InitialState("Idle")
                .Build();
        }

    } // namespace FSMPatterns

} // namespace AI
} // namespace Core
