#pragma once

#include "Core/Physics/Ragdoll/RagdollDefinition.h"
#include "Core/Math/Math.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Constraints/Constraint.h>

#include <entt/entity/entity.hpp>

#include <string>
#include <vector>
#include <cstdint>

namespace Core
{
    // Forward declarations
    class Scene;
}

namespace Core::Physics
{
    // Forward declarations
    class PhysicsWorld;
    class Skeleton;

    // =============================================================================
    // GenerationResult
    // =============================================================================

    /**
     * @brief Result structure returned from ragdoll generation operations.
     * 
     * Contains success status, error information if failed, and references to
     * all physics bodies and constraints created during generation.
     */
    struct GenerationResult
    {
        /** Whether the ragdoll generation completed successfully. */
        bool Success = false;

        /** Error message if generation failed, empty on success. */
        std::string ErrorMessage;

        /** 
         * All physics bodies created for the ragdoll bones.
         * Order corresponds to RagdollDefinition bone order.
         */
        std::vector<JPH::BodyID> CreatedBodies;

        /** 
         * All constraints created between ragdoll bones.
         * Constraints are owned by the physics system after creation.
         */
        std::vector<JPH::Constraint*> CreatedConstraints;

        // -------------------------------------------------------------------------
        // Utility Methods
        // -------------------------------------------------------------------------

        /**
         * @brief Implicit conversion to bool for convenient success checking.
         * @return True if generation was successful.
         */
        [[nodiscard]] explicit operator bool() const noexcept
        {
            return Success;
        }

        /**
         * @brief Creates a successful result with the given bodies and constraints.
         */
        [[nodiscard]] static GenerationResult Succeeded(
            std::vector<JPH::BodyID> bodies,
            std::vector<JPH::Constraint*> constraints)
        {
            return GenerationResult{
                .Success = true,
                .ErrorMessage = {},
                .CreatedBodies = std::move(bodies),
                .CreatedConstraints = std::move(constraints)
            };
        }

        /**
         * @brief Creates a failed result with the given error message.
         */
        [[nodiscard]] static GenerationResult Failed(std::string error)
        {
            return GenerationResult{
                .Success = false,
                .ErrorMessage = std::move(error),
                .CreatedBodies = {},
                .CreatedConstraints = {}
            };
        }
    };

    // =============================================================================
    // SkeletonPose
    // =============================================================================

    /**
     * @brief Simplified skeleton pose representation for ragdoll generation.
     * 
     * Contains the world-space transforms for each bone in a skeleton at a
     * specific point in time. Used to position ragdoll bodies at the correct
     * locations when transitioning from animation to physics simulation.
     */
    struct SkeletonPose
    {
        /** 
         * World-space transformation matrices for each bone.
         * Index corresponds to bone index in the skeleton.
         */
        std::vector<Math::Mat4> BoneTransforms;

        // -------------------------------------------------------------------------
        // Accessors
        // -------------------------------------------------------------------------

        /**
         * @brief Gets the world-space transform for a specific bone.
         * @param index The bone index in the skeleton.
         * @return The world-space transformation matrix for the bone.
         * @note Returns identity matrix if index is out of bounds.
         */
        [[nodiscard]] Math::Mat4 GetBoneWorldTransform(int32_t index) const
        {
            if (index < 0 || static_cast<size_t>(index) >= BoneTransforms.size())
            {
                return Math::Mat4(1.0f); // Identity matrix
            }
            return BoneTransforms[static_cast<size_t>(index)];
        }

        /**
         * @brief Gets the number of bone transforms in this pose.
         */
        [[nodiscard]] size_t GetBoneCount() const noexcept
        {
            return BoneTransforms.size();
        }

        /**
         * @brief Checks if the pose has valid transforms.
         */
        [[nodiscard]] bool IsValid() const noexcept
        {
            return !BoneTransforms.empty();
        }
    };

    // =============================================================================
    // RagdollGenerator
    // =============================================================================

    /**
     * @brief Static utility class for generating and managing ragdoll physics bodies.
     * 
     * Provides functionality for:
     * - Creating ragdoll bodies and constraints from a definition
     * - Auto-generating ragdoll definitions from skeleton data
     * - Blending between animation and ragdoll physics
     * - Applying impulses to ragdoll bones
     * 
     * This class follows the static utility pattern - all methods are static
     * and no instance state is maintained.
     * 
     * @example
     * @code
     * // Generate a ragdoll from a definition
     * auto result = RagdollGenerator::Generate(
     *     physicsWorld, scene, entity, definition, pose, worldTransform);
     * 
     * if (result.Success)
     * {
     *     // Ragdoll is now active
     * }
     * 
     * // Auto-generate a humanoid ragdoll definition
     * auto definition = RagdollGenerator::AutoGenerateDefinition(
     *     skeleton, RagdollProfile::Humanoid);
     * @endcode
     */
    class RagdollGenerator
    {
    public:
        // -------------------------------------------------------------------------
        // Deleted Constructors (Static Utility Class)
        // -------------------------------------------------------------------------
        
        RagdollGenerator() = delete;
        ~RagdollGenerator() = delete;
        RagdollGenerator(const RagdollGenerator&) = delete;
        RagdollGenerator& operator=(const RagdollGenerator&) = delete;
        RagdollGenerator(RagdollGenerator&&) = delete;
        RagdollGenerator& operator=(RagdollGenerator&&) = delete;

        // -------------------------------------------------------------------------
        // Ragdoll Generation
        // -------------------------------------------------------------------------

        /**
         * @brief Generates a complete ragdoll from a definition.
         * 
         * Creates all physics bodies and constraints defined in the ragdoll
         * definition, positioned according to the skeleton pose and entity
         * world transform.
         * 
         * @param physicsWorld The physics world to create bodies in.
         * @param scene The scene containing the entity.
         * @param entity The entity to attach the ragdoll to.
         * @param definition The ragdoll definition specifying bones and constraints.
         * @param pose The current skeleton pose for initial body positions.
         * @param entityWorldTransform The entity's world transformation matrix.
         * @return GenerationResult containing success status and created objects.
         */
        [[nodiscard]] static GenerationResult Generate(
            PhysicsWorld& physicsWorld,
            Scene& scene,
            entt::entity entity,
            RagdollDefinition& definition,
            const SkeletonPose& pose,
            const Math::Mat4& entityWorldTransform);

        /**
         * @brief Auto-generates a ragdoll definition from skeleton data.
         * 
         * Analyzes the skeleton structure and creates an appropriate ragdoll
         * definition based on the specified profile. Automatically determines
         * bone collider sizes and constraint limits.
         * 
         * @param skeleton The skeleton to generate a ragdoll for.
         * @param profile The ragdoll profile to use (Humanoid, Quadruped, etc.).
         * @return A complete RagdollDefinition ready for generation.
         */
        [[nodiscard]] static RagdollDefinition AutoGenerateDefinition(
            const Skeleton& skeleton,
            RagdollProfile profile);

        // -------------------------------------------------------------------------
        // Animation Blending
        // -------------------------------------------------------------------------

        /**
         * @brief Initiates a blend from animation to ragdoll physics.
         * 
         * Smoothly transitions the entity from animated state to physics-driven
         * ragdoll over the specified duration.
         * 
         * @param scene The scene containing the entity.
         * @param entity The entity to transition.
         * @param blendDuration Duration of the blend in seconds.
         */
        static void BlendToRagdoll(
            Scene& scene,
            entt::entity entity,
            float blendDuration);

        /**
         * @brief Initiates a blend from ragdoll physics back to animation.
         * 
         * Smoothly transitions the entity from physics-driven ragdoll back to
         * animated state over the specified duration. Often called "get up"
         * or recovery animation.
         * 
         * @param scene The scene containing the entity.
         * @param entity The entity to transition.
         * @param blendDuration Duration of the blend in seconds.
         */
        static void BlendFromRagdoll(
            Scene& scene,
            entt::entity entity,
            float blendDuration);

        // -------------------------------------------------------------------------
        // Body and Constraint Creation
        // -------------------------------------------------------------------------

        /**
         * @brief Creates a physics body for a single ragdoll bone.
         * 
         * @param physicsWorld The physics world to create the body in.
         * @param boneDefinition The bone definition specifying collider and mass.
         * @param worldTransform The world-space transform for the body.
         * @return The BodyID of the created physics body.
         */
        [[nodiscard]] static JPH::BodyID CreateBodyForBone(
            PhysicsWorld& physicsWorld,
            const RagdollBoneDefinition& boneDefinition,
            const Math::Mat4& worldTransform);

        /**
         * @brief Creates a constraint between two ragdoll bone bodies.
         * 
         * @param physicsWorld The physics world to create the constraint in.
         * @param boneDefinition The child bone definition specifying constraint type.
         * @param parentBody The parent body ID.
         * @param childBody The child body ID.
         * @return Pointer to the created constraint, or nullptr on failure.
         */
        [[nodiscard]] static JPH::Constraint* CreateConstraintForBone(
            PhysicsWorld& physicsWorld,
            const RagdollBoneDefinition& boneDefinition,
            JPH::BodyID parentBody,
            JPH::BodyID childBody);

        // -------------------------------------------------------------------------
        // Utility Methods
        // -------------------------------------------------------------------------

        /**
         * @brief Calculates appropriate capsule dimensions for a bone.
         * 
         * Computes sensible capsule radius and height based on bone length,
         * using anatomically-inspired proportions.
         * 
         * @param boneLength The length of the bone.
         * @return Vec3 containing (radius, height, unused) for capsule creation.
         */
        [[nodiscard]] static Math::Vec3 CalculateCapsuleDimensions(float boneLength);

        /**
         * @brief Applies an impulse to a ragdoll bone at a specific point.
         * 
         * Used for applying forces such as impacts, explosions, or other
         * physics-based effects to specific ragdoll bones.
         * 
         * @param physicsWorld The physics world containing the body.
         * @param bodyId The body ID to apply the impulse to.
         * @param impulse The impulse vector in world space.
         * @param point The world-space point where the impulse is applied.
         */
        static void ApplyImpulseToBone(
            PhysicsWorld& physicsWorld,
            JPH::BodyID bodyId,
            const Math::Vec3& impulse,
            const Math::Vec3& point);
    };

} // namespace Core::Physics
