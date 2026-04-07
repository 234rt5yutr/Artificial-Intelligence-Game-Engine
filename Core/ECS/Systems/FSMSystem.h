#pragma once

// FSMSystem.h
// ECS system for processing finite state machine components
// Updates all FSMs each frame and manages FSM lifecycle

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/FSMComponent.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/AI/FSM/FSMBuilder.h"
#include "Core/Profile.h"
#include "Core/Log.h"
#include <vector>
#include <atomic>
#include <mutex>

namespace Core {
namespace ECS {

    //=========================================================================
    // FSM System Statistics
    //=========================================================================

    struct FSMStatistics {
        uint32_t ActiveFSMCount = 0;             // Number of active FSMs processed
        uint32_t UpdateCount = 0;                 // Total updates executed this frame
        uint32_t TransitionCount = 0;             // State transitions this frame
        float AverageUpdateTimeMs = 0.0f;         // Average update time in milliseconds

        void Reset() {
            ActiveFSMCount = 0;
            UpdateCount = 0;
            TransitionCount = 0;
            AverageUpdateTimeMs = 0.0f;
        }
    };

    //=========================================================================
    // FSM System
    //=========================================================================

    class FSMSystem {
    public:
        FSMSystem() = default;
        ~FSMSystem() = default;

        //---------------------------------------------------------------------
        // Lifecycle
        //---------------------------------------------------------------------

        /// Initialize the FSM system
        void Initialize() {
            LOG_INFO("FSMSystem initialized");
            m_Initialized = true;
        }

        /// Shutdown and release resources
        void Shutdown() {
            m_Initialized = false;
            LOG_INFO("FSMSystem shutdown");
        }

        //---------------------------------------------------------------------
        // Update Methods
        //---------------------------------------------------------------------

        /// Update all FSM components
        /// @param scene The scene containing entities to process
        /// @param deltaTime Time elapsed since last frame in seconds
        void Update(Scene& scene, float deltaTime) {
            PROFILE_SCOPE("FSMSystem::Update");

            m_Statistics.Reset();

            auto view = scene.GetRegistry().view<FSMComponent>();

            for (auto entity : view) {
                auto& fsmComp = view.get<FSMComponent>(entity);
                ProcessEntity(scene, entity, fsmComp, deltaTime);
            }
        }

        //---------------------------------------------------------------------
        // FSM Management
        //---------------------------------------------------------------------

        /// Ensure an FSM instance is created for a component
        /// @param fsmComp The FSM component
        /// @return True if instance exists or was created
        bool EnsureFSMInstance(FSMComponent& fsmComp) {
            if (fsmComp.FSMInstance) {
                return true;
            }

            if (fsmComp.TemplateId.empty()) {
                return false;
            }

            auto& manager = AI::FSMManager::Get();
            auto instance = manager.CreateInstance(fsmComp.TemplateId);
            
            if (instance) {
                fsmComp.FSMInstance = std::move(instance);
                if (!fsmComp.InstanceName.empty()) {
                    fsmComp.FSMInstance->SetName(fsmComp.InstanceName);
                }
                return true;
            }

            LOG_WARNING("Failed to create FSM instance from template: {}", 
                       fsmComp.TemplateId);
            return false;
        }

        //---------------------------------------------------------------------
        // Event Dispatch
        //---------------------------------------------------------------------

        /// Send an event to a specific entity's FSM
        /// @param scene The scene containing the entity
        /// @param entity The target entity
        /// @param eventName The event to send
        void SendEventToEntity(Scene& scene, entt::entity entity, 
                              const std::string& eventName) {
            if (scene.GetRegistry().all_of<FSMComponent>(entity)) {
                auto& fsmComp = scene.GetRegistry().get<FSMComponent>(entity);
                fsmComp.SendEvent(eventName);
            }
        }

        /// Broadcast an event to all FSMs
        /// @param scene The scene containing entities
        /// @param eventName The event to broadcast
        void BroadcastEvent(Scene& scene, const std::string& eventName) {
            auto view = scene.GetRegistry().view<FSMComponent>();
            
            for (auto entity : view) {
                auto& fsmComp = view.get<FSMComponent>(entity);
                fsmComp.SendEvent(eventName);
            }
        }

        //---------------------------------------------------------------------
        // Statistics
        //---------------------------------------------------------------------

        /// Get current frame statistics
        const FSMStatistics& GetStatistics() const { 
            return m_Statistics; 
        }

        /// Get number of active FSMs processed last frame
        uint32_t GetActiveFSMCount() const { 
            return m_Statistics.ActiveFSMCount; 
        }

    private:
        //---------------------------------------------------------------------
        // Internal Processing Methods
        //---------------------------------------------------------------------

        /// Process a single entity with FSM component
        void ProcessEntity(Scene& scene, entt::entity entity, 
                          FSMComponent& fsmComp, float deltaTime) {
            // Skip inactive components
            if (!fsmComp.IsActive) {
                return;
            }

            // Ensure FSM instance exists
            if (!EnsureFSMInstance(fsmComp)) {
                return;
            }

            // Start FSM if not started
            if (!fsmComp.IsStarted) {
                fsmComp.Start();
            }

            // Check update frequency
            if (!fsmComp.ShouldUpdate(deltaTime)) {
                return;
            }

            // Inject entity ID into blackboard for FSM access
            auto* blackboard = fsmComp.GetBlackboard();
            if (blackboard) {
                blackboard->Set("_entityId", static_cast<uint32_t>(entity));
                
                // Inject transform if available
                if (scene.GetRegistry().all_of<TransformComponent>(entity)) {
                    auto& transform = scene.GetRegistry().get<TransformComponent>(entity);
                    blackboard->Set("_position", transform.Position);
                    blackboard->Set("_rotation", transform.Rotation);
                }
            }

            // Track state before update
            std::string prevState = fsmComp.GetCurrentStateName();

            // Execute update
            fsmComp.Update(deltaTime);

            // Check for state transition
            std::string newState = fsmComp.GetCurrentStateName();
            if (newState != prevState) {
                m_Statistics.TransitionCount++;
            }

            // Update statistics
            m_Statistics.ActiveFSMCount++;
            m_Statistics.UpdateCount++;
        }

        //---------------------------------------------------------------------
        // Member Variables
        //---------------------------------------------------------------------

        /// Per-frame statistics
        FSMStatistics m_Statistics;

        /// Initialization flag
        bool m_Initialized = false;
    };

} // namespace ECS
} // namespace Core
