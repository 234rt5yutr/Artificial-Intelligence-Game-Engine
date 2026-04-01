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

        //---------------------------------------------------------------------
        // Advanced Animation Blending (Step 12.4)
        //---------------------------------------------------------------------

        /// Evaluate a blend tree and produce a blended skeleton pose
        /// Samples all active clips from the blend tree based on computed weights
        /// and blends multiple animation poses together using their weights.
        /// Supports 1D blend (speed parameter) and 2D blend (direction).
        /// @param tree The blend tree to evaluate
        /// @param animator The animator component containing parameters
        /// @param skeletal The skeletal mesh component with skeleton data
        /// @return The blended skeleton pose
        SkeletonPose EvaluateBlendTree(const BlendTree& tree,
                                       const AnimatorComponent& animator,
                                       const SkeletalMeshComponent& skeletal);

        /// Process multiple animation layers and blend them together
        /// Each layer can have a state machine or blend tree.
        /// Layers blend on top of each other (override or additive).
        /// Respects bone masks (AffectedBones list in AnimationLayer).
        /// @param animator The animator component with layer definitions
        /// @param skeletal The skeletal mesh component with skeleton data
        /// @param basePose The base pose from the main state machine
        /// @return The final blended pose with all layers applied
        SkeletonPose BlendLayers(const AnimatorComponent& animator,
                                 const SkeletalMeshComponent& skeletal,
                                 const SkeletonPose& basePose);

        /// Compute an additive pose by subtracting reference pose from animation
        /// Used for additive animation where the difference from a reference
        /// pose is applied on top of another animation.
        /// @param animation The source animation pose
        /// @param reference The reference pose to subtract
        /// @return The additive delta pose
        SkeletonPose ComputeAdditivePose(const SkeletonPose& animation,
                                         const SkeletonPose& reference);

        /// Apply an additive pose on top of a base pose
        /// Modifies the base pose in place by adding the additive deltas.
        /// @param base The base pose to modify (modified in place)
        /// @param additive The additive delta pose to apply
        /// @param weight The blend weight for the additive layer [0-1]
        void ApplyAdditivePose(SkeletonPose& base, 
                               const SkeletonPose& additive, 
                               float weight);

        /// Apply a transition curve to a linear progress value
        /// Transforms linear interpolation to eased curves.
        /// @param t Linear progress value [0-1]
        /// @param curveType The type of easing curve to apply
        /// @return The curved progress value [0-1]
        float ApplyTransitionCurve(float t, TransitionCurveType curveType);

        /// Evaluate a single animation layer and return its pose
        /// @param layer The animation layer to evaluate
        /// @param animator The animator component containing parameters
        /// @param skeletal The skeletal mesh component with skeleton data
        /// @param deltaTime Time elapsed since last frame
        /// @return The pose produced by this layer
        SkeletonPose EvaluateLayer(const AnimationLayer& layer,
                                   const AnimatorComponent& animator,
                                   const SkeletalMeshComponent& skeletal,
                                   float deltaTime);

        /// Blend two poses together with a bone mask
        /// Only affects bones specified in the mask (or all if mask is empty)
        /// @param base The base pose to blend into
        /// @param overlay The overlay pose to blend from
        /// @param weight The blend weight [0-1]
        /// @param affectedBones List of bone names affected (empty = all)
        /// @param skeleton The skeleton for bone name resolution
        /// @param isAdditive If true, overlay is added; if false, it overrides
        void BlendPoseWithMask(SkeletonPose& base,
                               const SkeletonPose& overlay,
                               float weight,
                               const std::vector<std::string>& affectedBones,
                               const Renderer::Skeleton& skeleton,
                               bool isAdditive);

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
