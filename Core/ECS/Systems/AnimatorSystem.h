#pragma once

// AnimatorSystem.h
// ECS system for processing animation state machines and updating skeletal poses
// Evaluates AnimatorComponent state machines and applies resulting poses to SkeletalMeshComponent

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/AnimatorComponent.h"
#include "Core/ECS/Components/SkeletalMeshComponent.h"
#include "Core/ECS/ParallelECS.h"
#include "Core/Renderer/Mesh.h"
#include "Core/Profile.h"
#include "Core/Log.h"
#include <vector>
#include <atomic>
#include <mutex>

namespace Core {
namespace ECS {

    //=========================================================================
    // Animator System Statistics
    //=========================================================================

    struct AnimatorStatistics {
        uint32_t AnimatedEntityCount = 0;       // Number of entities processed
        uint32_t ActiveTransitionCount = 0;      // Number of active state transitions
        uint32_t StateMachineEvaluations = 0;    // Total evaluations this frame

        void Reset() {
            AnimatedEntityCount = 0;
            ActiveTransitionCount = 0;
            StateMachineEvaluations = 0;
        }
    };

    //=========================================================================
    // State Machine Evaluation Result (for parallel processing)
    //=========================================================================

    struct StateMachineEvalResult {
        entt::entity Entity;
        bool TransitionTriggered;
        float BlendWeight;                       // Current crossfade blend weight
        SkeletonPose ResultPose;                 // Final blended pose
    };

    //=========================================================================
    // Animator System
    //=========================================================================

    class AnimatorSystem : public ParallelSystemBase {
    public:
        AnimatorSystem() = default;
        ~AnimatorSystem() = default;

        //---------------------------------------------------------------------
        // Lifecycle
        //---------------------------------------------------------------------

        /// Initialize the animator system
        void Initialize();

        /// Shutdown and release resources
        void Shutdown();

        //---------------------------------------------------------------------
        // Update Methods
        //---------------------------------------------------------------------

        /// Update all animator components (sequential)
        /// @param scene The scene containing entities to process
        /// @param deltaTime Time elapsed since last frame in seconds
        void Update(Scene& scene, float deltaTime);

        /// Parallel update for large scenes
        /// @param scene The scene containing entities to process
        /// @param deltaTime Time elapsed since last frame in seconds
        void UpdateParallel(Scene& scene, float deltaTime);

        //---------------------------------------------------------------------
        // Statistics
        //---------------------------------------------------------------------

        /// Get number of animated entities processed last frame
        uint32_t GetAnimatedEntityCount() const { 
            return m_Statistics.AnimatedEntityCount; 
        }

        /// Get number of active state transitions last frame
        uint32_t GetActiveTransitionCount() const { 
            return m_Statistics.ActiveTransitionCount; 
        }

        /// Get total state machine evaluations last frame
        uint32_t GetStateMachineEvaluations() const { 
            return m_Statistics.StateMachineEvaluations; 
        }

        /// Get full statistics structure
        const AnimatorStatistics& GetStatistics() const { 
            return m_Statistics; 
        }

    private:
        //---------------------------------------------------------------------
        // Internal Processing Methods
        //---------------------------------------------------------------------

        /// Evaluate the animation state machine for an entity
        /// Checks transition conditions and updates current state
        /// @param animator The animator component to evaluate
        /// @param deltaTime Time elapsed since last frame
        /// @return True if a state transition was triggered
        bool EvaluateStateMachine(AnimatorComponent& animator, float deltaTime);

        /// Blend animation states during crossfade transitions
        /// Interpolates between current and target state poses
        /// @param animator The animator component with active transition
        /// @param skeletal The skeletal mesh component containing skeleton data
        /// @param deltaTime Time elapsed since last frame
        /// @return The blended skeleton pose
        SkeletonPose BlendAnimationStates(const AnimatorComponent& animator,
                                          const SkeletalMeshComponent& skeletal,
                                          float deltaTime);

        /// Apply the final computed pose to the skeletal mesh component
        /// Writes pose data ready for GPU skinning
        /// @param pose The computed skeleton pose
        /// @param skeletal The target skeletal mesh component to update
        void ApplyPoseToComponent(const SkeletonPose& pose, 
                                  SkeletalMeshComponent& skeletal);

        /// Process a single entity (used by both sequential and parallel paths)
        /// @param entity The entity to process
        /// @param animator The animator component
        /// @param skeletal The skeletal mesh component
        /// @param deltaTime Time elapsed since last frame
        void ProcessEntity(entt::entity entity,
                          AnimatorComponent& animator,
                          SkeletalMeshComponent& skeletal,
                          float deltaTime);

    private:
        //---------------------------------------------------------------------
        // Member Variables
        //---------------------------------------------------------------------

        /// Per-frame statistics (reset each update)
        AnimatorStatistics m_Statistics;

        /// Thread-safe statistics for parallel processing
        std::atomic<uint32_t> m_AtomicEntityCount{0};
        std::atomic<uint32_t> m_AtomicTransitionCount{0};
        std::atomic<uint32_t> m_AtomicEvaluationCount{0};

        /// Thread-local scratch buffers for parallel pose computation
        ThreadLocalScratch<std::vector<StateMachineEvalResult>> m_ThreadLocalResults;

        /// Mutex for any shared state updates during parallel processing
        std::mutex m_StateMutex;

        /// Initialization flag
        bool m_Initialized = false;
    };

} // namespace ECS
} // namespace Core
