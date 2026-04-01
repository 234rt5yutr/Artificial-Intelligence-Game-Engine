#pragma once

// IKSystem.h
// ECS system for processing Inverse Kinematics
// Solves IK chains and applies procedural adjustments on top of animation poses

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/IKComponent.h"
#include "Core/ECS/Components/SkeletalMeshComponent.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/ParallelECS.h"
#include "Core/Profile.h"
#include "Core/Log.h"
#include <vector>
#include <atomic>

namespace Core {

// Forward declarations
namespace Physics {
    class PhysicsWorld;
}

namespace ECS {

    //=========================================================================
    // IK System Statistics
    //=========================================================================

    struct IKStatistics {
        uint32_t EntitiesProcessed = 0;      // Entities with IK this frame
        uint32_t ChainsEvaluated = 0;        // Total IK chains evaluated
        uint32_t RaycastsPerformed = 0;      // Foot IK raycasts
        uint32_t SuccessfulSolves = 0;       // Chains that reached target

        void Reset() {
            EntitiesProcessed = 0;
            ChainsEvaluated = 0;
            RaycastsPerformed = 0;
            SuccessfulSolves = 0;
        }
    };

    //=========================================================================
    // Two-Bone IK Solver Result
    //=========================================================================

    struct TwoBoneIKResult {
        Math::Quat UpperRotation;    // Rotation for upper bone (e.g., thigh)
        Math::Quat LowerRotation;    // Rotation for lower bone (e.g., shin)
        Math::Quat EndRotation;      // Rotation for end effector (e.g., foot)
        bool Solved = false;         // Whether IK reached target
        float ReachFraction = 0.0f;  // How close we got (1 = at target)
    };

    //=========================================================================
    // IK System
    //=========================================================================

    class IKSystem : public ParallelSystemBase {
    public:
        IKSystem();
        ~IKSystem();

        //---------------------------------------------------------------------
        // Lifecycle
        //---------------------------------------------------------------------

        /// Initialize the IK system with physics world for raycasting
        void Initialize(Physics::PhysicsWorld* physicsWorld);

        /// Shutdown and release resources
        void Shutdown();

        //---------------------------------------------------------------------
        // Update Methods
        //---------------------------------------------------------------------

        /// Update all IK components (sequential)
        /// Should be called AFTER AnimatorSystem updates poses
        /// @param scene The scene containing entities to process
        /// @param deltaTime Time elapsed since last frame in seconds
        void Update(Scene& scene, float deltaTime);

        /// Parallel update for large scenes
        void UpdateParallel(Scene& scene, float deltaTime);

        //---------------------------------------------------------------------
        // Statistics
        //---------------------------------------------------------------------

        const IKStatistics& GetStatistics() const { return m_Statistics; }
        uint32_t GetEntitiesProcessed() const { return m_Statistics.EntitiesProcessed; }
        uint32_t GetChainsEvaluated() const { return m_Statistics.ChainsEvaluated; }

    private:
        //---------------------------------------------------------------------
        // Entity Processing
        //---------------------------------------------------------------------

        /// Process a single entity with IK
        void ProcessEntity(entt::entity entity,
                          IKComponent& ik,
                          SkeletalMeshComponent& skeletal,
                          const TransformComponent& transform,
                          float deltaTime);

        /// Initialize IK chain bone indices
        void InitializeChainIndices(IKComponent& ik,
                                    const SkeletalMeshComponent& skeletal);

        //---------------------------------------------------------------------
        // IK Solvers
        //---------------------------------------------------------------------

        /// Solve a two-bone IK chain
        /// @param upperPos World position of upper bone (e.g., shoulder)
        /// @param midPos World position of middle joint (e.g., elbow)
        /// @param endPos World position of end effector (e.g., hand)
        /// @param targetPos Target world position for end effector
        /// @param poleVector Direction to bend joint towards
        /// @param upperLength Length of upper bone
        /// @param lowerLength Length of lower bone
        /// @return IK solution with bone rotations
        TwoBoneIKResult SolveTwoBoneIK(
            const Math::Vec3& upperPos,
            const Math::Vec3& midPos,
            const Math::Vec3& endPos,
            const Math::Vec3& targetPos,
            const Math::Vec3& poleVector,
            float upperLength,
            float lowerLength,
            float minAngle,
            float maxAngle);

        /// Solve look-at constraint
        /// @param bonePos Current bone world position
        /// @param boneForward Current bone forward direction
        /// @param targetPos Target to look at
        /// @param constraints Min/max rotation angles
        /// @return Rotation to apply
        Math::Quat SolveLookAt(
            const Math::Vec3& bonePos,
            const Math::Vec3& boneForward,
            const Math::Vec3& targetPos,
            float minAngle,
            float maxAngle);

        //---------------------------------------------------------------------
        // Foot IK
        //---------------------------------------------------------------------

        /// Process foot IK for an entity
        void ProcessFootIK(IKComponent& ik,
                           SkeletalMeshComponent& skeletal,
                           const TransformComponent& transform,
                           float deltaTime);

        /// Raycast to find ground below foot
        /// @param footPos Current foot world position
        /// @param rayHeight Height above foot to start ray
        /// @param rayDepth Distance below foot to cast
        /// @param outHitPos Output: ground hit position
        /// @param outHitNormal Output: ground surface normal
        /// @return True if ground was hit
        bool RaycastGround(const Math::Vec3& footPos,
                           float rayHeight,
                           float rayDepth,
                           Math::Vec3& outHitPos,
                           Math::Vec3& outHitNormal);

        //---------------------------------------------------------------------
        // Chain Processing
        //---------------------------------------------------------------------

        /// Process a single IK chain
        void ProcessChain(const IKChainDefinition& chain,
                          IKChainState& state,
                          SkeletalMeshComponent& skeletal,
                          const TransformComponent& transform,
                          float deltaTime);

        /// Apply IK result to skeleton pose
        void ApplyIKToPose(const TwoBoneIKResult& result,
                           const IKChainDefinition& chain,
                           SkeletonPose& pose,
                           float weight);

        //---------------------------------------------------------------------
        // Utility
        //---------------------------------------------------------------------

        /// Smooth blend between two values
        float SmoothDamp(float current, float target, float& velocity,
                         float smoothTime, float deltaTime);

        /// Smooth blend between two Vec3s
        Math::Vec3 SmoothDampVec3(const Math::Vec3& current, 
                                   const Math::Vec3& target,
                                   Math::Vec3& velocity,
                                   float smoothTime, float deltaTime);

    private:
        //---------------------------------------------------------------------
        // Member Variables
        //---------------------------------------------------------------------

        Physics::PhysicsWorld* m_PhysicsWorld = nullptr;
        IKStatistics m_Statistics;
        
        // Atomic counters for parallel processing
        std::atomic<uint32_t> m_AtomicEntities{0};
        std::atomic<uint32_t> m_AtomicChains{0};
        std::atomic<uint32_t> m_AtomicRaycasts{0};

        bool m_Initialized = false;
    };

} // namespace ECS
} // namespace Core
