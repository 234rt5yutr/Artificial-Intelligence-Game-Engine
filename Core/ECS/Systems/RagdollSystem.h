#pragma once

// RagdollSystem.h
// ECS system for managing ragdoll physics simulation lifecycle
// Handles activation, deactivation, blending, skeleton synchronization, and lifetime management

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/RagdollComponent.h"
#include "Core/Physics/Ragdoll/RagdollGenerator.h"
#include "Core/Math/Math.h"

#include <entt/entity/entity.hpp>

#include <cstdint>

namespace Core {

// Forward declarations
namespace Physics {
    class PhysicsWorld;
} // namespace Physics

namespace ECS {

    // =========================================================================
    // RagdollSystem
    // =========================================================================

    /**
     * @brief ECS system responsible for managing ragdoll physics simulation.
     * 
     * The RagdollSystem manages the complete lifecycle of ragdoll physics:
     *   - Spawning physics bodies from ragdoll definitions
     *   - Activating/deactivating ragdolls with smooth blending
     *   - Synchronizing physics body transforms back to the skeleton
     *   - Managing ragdoll lifetimes and automatic cleanup
     *   - Enforcing concurrent ragdoll limits for performance
     * 
     * This system should be updated after AnimatorSystem but before rendering
     * to ensure correct pose blending between animation and physics.
     * 
     * Usage:
     * @code
     *     RagdollSystem ragdollSystem(physicsWorld);
     *     
     *     // Main game loop
     *     ragdollSystem.Update(scene, deltaTime);
     *     
     *     // Activate ragdoll on entity (e.g., from damage event)
     *     ragdollSystem.ActivateRagdoll(scene, entity, impulse, hitPoint);
     * @endcode
     * 
     * @note This system is non-copyable and non-movable to ensure single
     *       ownership of physics world reference and internal state.
     */
    class RagdollSystem
    {
    public:
        // ---------------------------------------------------------------------
        // Constructors & Destructor
        // ---------------------------------------------------------------------

        /**
         * @brief Constructs the ragdoll system with a reference to the physics world.
         * 
         * @param physicsWorld Reference to the physics world for body creation/management.
         */
        explicit RagdollSystem(Physics::PhysicsWorld& physicsWorld);

        /**
         * @brief Destructor - cleans up any remaining ragdoll resources.
         */
        ~RagdollSystem();

        // Non-copyable
        RagdollSystem(const RagdollSystem&) = delete;
        RagdollSystem& operator=(const RagdollSystem&) = delete;

        // Non-movable
        RagdollSystem(RagdollSystem&&) = delete;
        RagdollSystem& operator=(RagdollSystem&&) = delete;

        // ---------------------------------------------------------------------
        // Main Update
        // ---------------------------------------------------------------------

        /**
         * @brief Main update loop for the ragdoll system.
         * 
         * Processes all entities with RagdollComponent:
         *   1. Spawns physics bodies for newly activated ragdolls
         *   2. Updates blend states for transitioning ragdolls
         *   3. Synchronizes physics transforms to skeletons
         *   4. Processes lifetime timers
         *   5. Cleans up expired ragdolls
         * 
         * @param scene The scene containing ragdoll entities to process.
         * @param deltaTime Time elapsed since last frame in seconds.
         */
        void Update(Scene& scene, float deltaTime);

        // ---------------------------------------------------------------------
        // Ragdoll Activation & Deactivation
        // ---------------------------------------------------------------------

        /**
         * @brief Activates ragdoll physics for an entity.
         * 
         * Transitions the entity's ragdoll from Inactive to BlendingIn state.
         * Physics bodies will be spawned and an initial impulse applied at
         * the specified impact point.
         * 
         * @param scene The scene containing the entity.
         * @param entity The entity to activate ragdoll on.
         * @param impulse Initial linear impulse to apply (world space).
         * @param impactPoint World-space position where the impact occurred.
         * 
         * @note Entity must have both RagdollComponent and SkeletalMeshComponent.
         * @note If max concurrent ragdolls is reached, oldest ragdoll may be deactivated.
         */
        void ActivateRagdoll(Scene& scene, 
                             entt::entity entity, 
                             const Math::Vec3& impulse, 
                             const Math::Vec3& impactPoint);

        /**
         * @brief Deactivates ragdoll physics for an entity.
         * 
         * Transitions the entity's ragdoll from Active to BlendingOut state.
         * After the blend completes, physics bodies will be despawned and
         * animation control will resume.
         * 
         * @param scene The scene containing the entity.
         * @param entity The entity to deactivate ragdoll on.
         */
        void DeactivateRagdoll(Scene& scene, entt::entity entity);

        // ---------------------------------------------------------------------
        // Physics Body Management
        // ---------------------------------------------------------------------

        /**
         * @brief Creates physics bodies for a ragdoll.
         * 
         * Instantiates physics bodies and constraints based on the entity's
         * RagdollComponent definition. Bodies are positioned to match the
         * current skeleton pose.
         * 
         * @param scene The scene containing the entity.
         * @param entity The entity to spawn ragdoll bodies for.
         * @return GenerationResult containing success status and created resources.
         * 
         * @note Called automatically during activation, but can be called
         *       manually for pre-warming or custom spawn scenarios.
         */
        [[nodiscard]] Physics::GenerationResult SpawnRagdoll(Scene& scene, entt::entity entity);

        /**
         * @brief Removes physics bodies for a ragdoll.
         * 
         * Destroys all physics bodies and constraints associated with the
         * entity's ragdoll, removing them from the physics world.
         * 
         * @param scene The scene containing the entity.
         * @param entity The entity to despawn ragdoll bodies from.
         * 
         * @note Called automatically during deactivation completion or cleanup.
         */
        void DespawnRagdoll(Scene& scene, entt::entity entity);

        // ---------------------------------------------------------------------
        // Blend State Management
        // ---------------------------------------------------------------------

        /**
         * @brief Updates the blend state of a ragdoll component.
         * 
         * Advances blend timers and handles state transitions:
         *   - BlendingIn -> Active (when timer reaches duration)
         *   - BlendingOut -> Inactive (when timer reaches duration)
         * 
         * @param ragdoll The ragdoll component to update.
         * @param deltaTime Time elapsed since last frame in seconds.
         */
        void UpdateBlendState(RagdollComponent& ragdoll, float deltaTime);

        // ---------------------------------------------------------------------
        // Skeleton Synchronization
        // ---------------------------------------------------------------------

        /**
         * @brief Copies physics body transforms to the skeleton.
         * 
         * For each ragdolled bone, reads the physics body transform and
         * applies it to the skeleton pose, blended with the animation pose
         * according to the current blend weight.
         * 
         * @param scene The scene containing the entity.
         * @param entity The entity to synchronize.
         * 
         * @note Should be called after physics simulation and before rendering.
         */
        void SyncSkeletonFromRagdoll(Scene& scene, entt::entity entity);

        // ---------------------------------------------------------------------
        // Lifetime Management
        // ---------------------------------------------------------------------

        /**
         * @brief Processes lifetime timers for all active ragdolls.
         * 
         * Decrements lifetime counters and marks expired ragdolls for cleanup.
         * Ragdolls with infinite lifetime (-1.0f) are not affected.
         * 
         * @param scene The scene containing ragdoll entities.
         * @param deltaTime Time elapsed since last frame in seconds.
         */
        void ProcessLifetimes(Scene& scene, float deltaTime);

        /**
         * @brief Cleans up ragdolls that have exceeded their lifetime.
         * 
         * For expired ragdolls:
         *   - Despawns physics bodies
         *   - Optionally destroys the entity (if DestroyOnTimeout is set)
         *   - Resets ragdoll component to Inactive state
         * 
         * @param scene The scene containing ragdoll entities.
         */
        void CleanupExpiredRagdolls(Scene& scene);

        // ---------------------------------------------------------------------
        // Statistics & Configuration
        // ---------------------------------------------------------------------

        /**
         * @brief Gets the current number of active ragdolls.
         * @return Number of ragdolls currently in Active, BlendingIn, or BlendingOut state.
         */
        [[nodiscard]] uint32_t GetActiveRagdollCount() const noexcept;

        /**
         * @brief Gets the maximum number of concurrent ragdolls allowed.
         * @return Maximum concurrent ragdoll limit.
         */
        [[nodiscard]] uint32_t GetMaxConcurrentRagdolls() const noexcept;

        /**
         * @brief Sets the maximum number of concurrent ragdolls allowed.
         * 
         * When this limit is reached, new ragdoll activations may cause
         * older ragdolls to be deactivated or denied.
         * 
         * @param maxRagdolls Maximum number of concurrent ragdolls.
         */
        void SetMaxConcurrentRagdolls(uint32_t maxRagdolls) noexcept;

    private:
        // ---------------------------------------------------------------------
        // Member Variables
        // ---------------------------------------------------------------------

        /** Reference to the physics world for body management. */
        Physics::PhysicsWorld& m_PhysicsWorld;

        /** Maximum number of ragdolls that can be active simultaneously. */
        uint32_t m_MaxConcurrentRagdolls = 32;

        /** Current count of active ragdolls. */
        uint32_t m_ActiveRagdollCount = 0;
    };

} // namespace ECS
} // namespace Core
