#pragma once

// BehaviorTreeSystem.h
// ECS system for processing behavior tree components
// Updates all behavior trees each frame and manages tree lifecycle

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/BehaviorTreeComponent.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/AI/BehaviorTree/BehaviorTreeManager.h"
#include "Core/Profile.h"
#include "Core/Log.h"
#include <vector>
#include <atomic>
#include <mutex>

namespace Core {
namespace ECS {

    //=========================================================================
    // Behavior Tree System Statistics
    //=========================================================================

    struct BehaviorTreeStatistics {
        uint32_t ActiveTreeCount = 0;            // Number of active trees processed
        uint32_t TickCount = 0;                   // Total ticks executed this frame
        uint32_t RunningCount = 0;                // Trees in Running state
        uint32_t SuccessCount = 0;                // Trees that returned Success
        uint32_t FailureCount = 0;                // Trees that returned Failure
        float AverageTickTimeMs = 0.0f;           // Average tick time in milliseconds

        void Reset() {
            ActiveTreeCount = 0;
            TickCount = 0;
            RunningCount = 0;
            SuccessCount = 0;
            FailureCount = 0;
            AverageTickTimeMs = 0.0f;
        }
    };

    //=========================================================================
    // Behavior Tree System
    //=========================================================================

    class BehaviorTreeSystem {
    public:
        BehaviorTreeSystem() = default;
        ~BehaviorTreeSystem() = default;

        //---------------------------------------------------------------------
        // Lifecycle
        //---------------------------------------------------------------------

        /// Initialize the behavior tree system
        void Initialize() {
            LOG_INFO("BehaviorTreeSystem initialized");
            m_Initialized = true;
        }

        /// Shutdown and release resources
        void Shutdown() {
            m_Initialized = false;
            LOG_INFO("BehaviorTreeSystem shutdown");
        }

        //---------------------------------------------------------------------
        // Update Methods
        //---------------------------------------------------------------------

        /// Update all behavior tree components
        /// @param scene The scene containing entities to process
        /// @param deltaTime Time elapsed since last frame in seconds
        void Update(Scene& scene, float deltaTime) {
            PROFILE_SCOPE("BehaviorTreeSystem::Update");

            m_Statistics.Reset();

            auto view = scene.GetRegistry().view<BehaviorTreeComponent>();

            for (auto entity : view) {
                auto& btComp = view.get<BehaviorTreeComponent>(entity);
                ProcessEntity(scene, entity, btComp, deltaTime);
            }
        }

        //---------------------------------------------------------------------
        // Tree Management
        //---------------------------------------------------------------------

        /// Ensure a behavior tree instance is created for a component
        /// @param btComp The behavior tree component
        /// @return True if instance exists or was created
        bool EnsureTreeInstance(BehaviorTreeComponent& btComp) {
            if (btComp.TreeInstance) {
                return true;
            }

            if (btComp.TemplateId.empty()) {
                return false;
            }

            auto& manager = AI::BehaviorTreeManager::Get();
            btComp.TreeInstance = manager.CreateInstance(btComp.TemplateId);

            if (btComp.TreeInstance) {
                if (!btComp.InstanceName.empty()) {
                    btComp.TreeInstance->SetName(btComp.InstanceName);
                }
                return true;
            }

            LOG_WARNING("Failed to create behavior tree instance from template: {}", 
                       btComp.TemplateId);
            return false;
        }

        //---------------------------------------------------------------------
        // Statistics
        //---------------------------------------------------------------------

        /// Get current frame statistics
        const BehaviorTreeStatistics& GetStatistics() const { 
            return m_Statistics; 
        }

        /// Get number of active trees processed last frame
        uint32_t GetActiveTreeCount() const { 
            return m_Statistics.ActiveTreeCount; 
        }

    private:
        //---------------------------------------------------------------------
        // Internal Processing Methods
        //---------------------------------------------------------------------

        /// Process a single entity with behavior tree component
        void ProcessEntity(Scene& scene, entt::entity entity, 
                          BehaviorTreeComponent& btComp, float deltaTime) {
            // Skip inactive components
            if (!btComp.IsActive) {
                return;
            }

            // Ensure tree instance exists
            if (!EnsureTreeInstance(btComp)) {
                return;
            }

            // Check update frequency
            if (!btComp.ShouldUpdate(deltaTime)) {
                return;
            }

            // Inject entity ID into blackboard for tree access
            auto* blackboard = btComp.GetBlackboard();
            if (blackboard) {
                blackboard->Set("_entityId", static_cast<uint32_t>(entity));
                
                // Inject transform if available
                if (scene.GetRegistry().all_of<TransformComponent>(entity)) {
                    auto& transform = scene.GetRegistry().get<TransformComponent>(entity);
                    blackboard->Set("_position", transform.Position);
                    blackboard->Set("_rotation", transform.Rotation);
                }
            }

            // Execute tick
            AI::BTStatus status = btComp.Tick(deltaTime);

            // Update statistics
            m_Statistics.ActiveTreeCount++;
            m_Statistics.TickCount++;

            switch (status) {
                case AI::BTStatus::Success:
                    m_Statistics.SuccessCount++;
                    break;
                case AI::BTStatus::Failure:
                    m_Statistics.FailureCount++;
                    break;
                case AI::BTStatus::Running:
                    m_Statistics.RunningCount++;
                    break;
            }
        }

        //---------------------------------------------------------------------
        // Member Variables
        //---------------------------------------------------------------------

        /// Per-frame statistics
        BehaviorTreeStatistics m_Statistics;

        /// Initialization flag
        bool m_Initialized = false;
    };

} // namespace ECS
} // namespace Core
