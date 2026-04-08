#pragma once

/**
 * @file ConstraintSystem.h
 * @brief ECS system for managing physics constraints between rigid bodies.
 *
 * The ConstraintSystem is responsible for synchronizing ECS constraint components
 * with the underlying Jolt Physics constraint simulation. It handles:
 * - Creation and destruction of Jolt Physics constraints
 * - Synchronization of constraint parameters from ECS to physics
 * - Breakage detection and cleanup for breakable constraints
 * - Processing of specialized constraint types (hinge, slider, spring)
 *
 * ## Architecture
 * The system follows a deferred creation pattern where ECS components define
 * constraint parameters, and the system lazily creates corresponding Jolt
 * constraints when needed. This decouples game logic from physics internals.
 *
 * ## Update Flow
 * 1. Process all constraint components marked with NeedsSync
 * 2. Check breakable constraints for force/torque threshold violations
 * 3. Clean up broken constraints
 * 4. Update runtime state (current angles, positions) from Jolt back to ECS
 *
 * @note This system should run before the main physics step to ensure constraints
 *       are properly configured for the simulation.
 *
 * @see ConstraintBase for common constraint properties
 * @see HingeConstraintComponent, SliderConstraintComponent, SpringConstraintComponent
 *
 * @author Core Engine Team
 * @version 1.0
 */

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/HingeConstraintComponent.h"
#include "Core/ECS/Components/SliderConstraintComponent.h"
#include "Core/ECS/Components/SpringConstraintComponent.h"
#include "Core/Physics/Constraints/ConstraintTypes.h"

// Jolt Physics headers for constraint creation
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Constraints/Constraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>

#include <cstdint>
#include <vector>

// Forward declarations
namespace Core::Physics {
    class PhysicsWorld;
}

namespace Core::ECS {

/**
 * @class ConstraintSystem
 * @brief Manages the lifecycle and synchronization of physics constraints in the ECS.
 *
 * The ConstraintSystem bridges the gap between high-level ECS constraint components
 * and the low-level Jolt Physics constraint implementation. It provides a clean
 * abstraction for game code to define physical connections between entities without
 * directly interacting with the physics engine.
 *
 * ## Responsibilities
 * - **Constraint Creation**: Converts ECS component data into Jolt constraints
 * - **Synchronization**: Keeps Jolt constraints in sync with ECS component changes
 * - **Breakage Handling**: Monitors force/torque and breaks constraints when exceeded
 * - **Cleanup**: Removes broken or orphaned constraints from the physics world
 * - **State Feedback**: Updates ECS components with runtime state (angles, positions)
 *
 * ## Thread Safety
 * This system is NOT thread-safe and should only be called from the main game thread.
 * The underlying Jolt Physics operations require exclusive access to the physics world.
 *
 * ## Usage Example
 * @code
 * // Initialize the system with the physics world
 * Core::Physics::PhysicsWorld physicsWorld;
 * Core::ECS::ConstraintSystem constraintSystem(physicsWorld);
 *
 * // In the game loop
 * void GameLoop(float deltaTime) {
 *     // Update constraints before physics step
 *     constraintSystem.Update(scene, deltaTime);
 *
 *     // Step physics simulation
 *     physicsWorld.Step(deltaTime);
 * }
 * @endcode
 *
 * ## Performance Considerations
 * - Constraint creation is deferred until the first Update() call
 * - Only constraints marked with NeedsSync are processed each frame
 * - Breakage checking is amortized across frames for large constraint counts
 */
class ConstraintSystem
{
public:
    // =========================================================================
    // Construction & Destruction
    // =========================================================================

    /**
     * @brief Constructs a ConstraintSystem with a reference to the physics world.
     *
     * The physics world must remain valid for the lifetime of this system.
     * The system does not take ownership of the physics world.
     *
     * @param physicsWorld Reference to the Jolt Physics world wrapper.
     *
     * @pre physicsWorld must be fully initialized before creating constraints.
     * @post The system is ready to process constraints via Update().
     */
    explicit ConstraintSystem(Physics::PhysicsWorld& physicsWorld);

    /**
     * @brief Destructor cleans up any remaining Jolt constraints.
     *
     * All constraints created by this system are destroyed. Entities retain
     * their constraint components, but JoltConstraint pointers become invalid.
     */
    ~ConstraintSystem();

    // Non-copyable, non-movable (holds reference to physics world)
    ConstraintSystem(const ConstraintSystem&) = delete;
    ConstraintSystem& operator=(const ConstraintSystem&) = delete;
    ConstraintSystem(ConstraintSystem&&) = delete;
    ConstraintSystem& operator=(ConstraintSystem&&) = delete;

    // =========================================================================
    // Core Update Methods
    // =========================================================================

    /**
     * @brief Main update loop for processing all constraint components.
     *
     * This method performs the complete constraint update cycle:
     * 1. Creates Jolt constraints for new components
     * 2. Synchronizes modified constraints (NeedsSync == true)
     * 3. Processes specialized constraint types for motor/limit updates
     * 4. Checks breakable constraints for threshold violations
     * 5. Cleans up broken constraints
     *
     * @param scene The ECS scene containing constraint components.
     * @param deltaTime Time elapsed since last update in seconds.
     *
     * @note Should be called once per frame before the physics step.
     * @note Components with IsEnabled == false are skipped.
     *
     * @warning Do not call from multiple threads simultaneously.
     */
    void Update(Scene& scene, float deltaTime);

    /**
     * @brief Synchronizes a single constraint entity with Jolt Physics.
     *
     * Forces immediate synchronization of the specified entity's constraint
     * component to its corresponding Jolt constraint. Creates the Jolt
     * constraint if it doesn't exist.
     *
     * @param entity The entity with a constraint component to synchronize.
     *
     * @note The entity must have at least one constraint component.
     * @note This is automatically called by Update() for dirty constraints.
     *
     * @example
     * @code
     * // Force sync after programmatic constraint modification
     * auto& hinge = registry.get<HingeConstraintComponent>(doorEntity);
     * hinge.MotorTargetVelocity = 5.0f;
     * constraintSystem.SyncConstraint(doorEntity);
     * @endcode
     */
    void SyncConstraint(entt::entity entity);

    // =========================================================================
    // Constraint Lifecycle Management
    // =========================================================================

    /**
     * @brief Creates a Jolt Physics constraint from ECS constraint data.
     *
     * Instantiates the appropriate Jolt constraint type based on the provided
     * ConstraintType and populates it with data from the ConstraintBase.
     * The created constraint is automatically added to the physics world.
     *
     * @param type The type of constraint to create.
     * @param constraint Reference to the constraint data with body references and pivot points.
     *
     * @return Pointer to the created Jolt constraint, or nullptr on failure.
     *
     * @pre Both BodyA and BodyB (if not null) must have valid RigidBodyComponents
     *      with initialized Jolt bodies.
     * @post constraint.JoltConstraint is set to the returned pointer.
     *
     * @note For world-anchored constraints, set BodyB to entt::null.
     * @note The returned pointer is owned by Jolt's ref-counting system.
     */
    JPH::Constraint* CreateJoltConstraint(Physics::ConstraintType type,
                                           Physics::ConstraintBase& constraint);

    /**
     * @brief Destroys a Jolt Physics constraint and cleans up references.
     *
     * Removes the constraint from the physics world and releases the Jolt
     * reference. The constraint component's JoltConstraint pointer is set to nullptr.
     *
     * @param constraint Reference to the constraint to destroy.
     *
     * @pre constraint.JoltConstraint must be a valid pointer.
     * @post constraint.JoltConstraint is set to nullptr.
     *
     * @note Safe to call with already-destroyed constraints (JoltConstraint == nullptr).
     * @note The component itself is not removed from the ECS; only the physics representation.
     */
    void DestroyJoltConstraint(Physics::ConstraintBase& constraint);

    // =========================================================================
    // Breakage Handling
    // =========================================================================

    /**
     * @brief Checks if a constraint has exceeded its break force/torque threshold.
     *
     * Queries the Jolt constraint for current applied force and torque magnitudes,
     * comparing them against the constraint's BreakForce and BreakTorque limits.
     * If exceeded, the constraint is marked as broken (IsBroken = true).
     *
     * @param constraint Reference to the breakable constraint to check.
     *
     * @return True if the constraint just broke (or was already broken), false otherwise.
     *
     * @note Only meaningful for constraints with IsBreakable == true.
     * @note Once broken, subsequent calls always return true.
     * @note Broken constraints are cleaned up by CleanupBrokenConstraints().
     *
     * @example
     * @code
     * if (constraintSystem.CheckBreakage(ropeConstraint)) {
     *     // Trigger break effects (particles, sound, etc.)
     *     PlayBreakSound(ropeConstraint);
     * }
     * @endcode
     */
    bool CheckBreakage(Physics::ConstraintBase& constraint);

    /**
     * @brief Removes all broken constraints from the scene and physics world.
     *
     * Iterates through all constraint components, destroys their Jolt constraints
     * if broken, and removes the components from their entities. This cleanup
     * ensures broken constraints don't accumulate and consume resources.
     *
     * @param scene The ECS scene to clean up.
     *
     * @note Called automatically at the end of Update().
     * @note Components are removed, not just the Jolt constraints.
     */
    void CleanupBrokenConstraints(Scene& scene);

    // =========================================================================
    // Specialized Constraint Processing
    // =========================================================================

    /**
     * @brief Processes hinge constraint motor and limit updates.
     *
     * Updates hinge-specific features that require per-frame attention:
     * - Motor velocity targets
     * - Motor torque limits
     * - Angular limit changes
     * - Reads current angle back into the component
     *
     * @param scene The ECS scene (used for related component access).
     * @param hinge Reference to the hinge constraint component.
     *
     * @note Called automatically for each hinge constraint during Update().
     */
    void ProcessHingeConstraint(Scene& scene, HingeConstraintComponent& hinge);

    /**
     * @brief Processes slider constraint motor and position updates.
     *
     * Updates slider-specific features that require per-frame attention:
     * - Velocity motor targets
     * - Position motor targets and servo behavior
     * - Linear limit changes
     * - Reads current position back into the component
     *
     * @param scene The ECS scene (used for related component access).
     * @param slider Reference to the slider constraint component.
     *
     * @note Called automatically for each slider constraint during Update().
     */
    void ProcessSliderConstraint(Scene& scene, SliderConstraintComponent& slider);

    /**
     * @brief Processes spring constraint dynamics updates.
     *
     * Updates spring-specific features that require per-frame attention:
     * - Stiffness and damping changes
     * - Rest length modifications
     * - Length limit updates
     * - Reads current length back into the component
     *
     * @param scene The ECS scene (used for related component access).
     * @param spring Reference to the spring constraint component.
     *
     * @note Called automatically for each spring constraint during Update().
     */
    void ProcessSpringConstraint(Scene& scene, SpringConstraintComponent& spring);

    // =========================================================================
    // Statistics & Diagnostics
    // =========================================================================

    /**
     * @brief Returns the total number of active constraints in the physics world.
     *
     * This count includes all constraint types (hinge, slider, spring, etc.)
     * currently instantiated in the Jolt Physics world.
     *
     * @return Number of active Jolt constraints managed by this system.
     *
     * @note Useful for debugging and performance monitoring.
     * @note Broken but not-yet-cleaned constraints are not included.
     */
    [[nodiscard]] uint32_t GetConstraintCount() const noexcept;

    /**
     * @brief Returns the number of constraints broken since the last reset.
     *
     * Tracks how many constraints have exceeded their break thresholds.
     * Useful for gameplay systems that react to structural damage.
     *
     * @return Cumulative count of broken constraints.
     */
    [[nodiscard]] uint32_t GetBrokenConstraintCount() const noexcept { return m_BrokenCount; }

    /**
     * @brief Resets the broken constraint counter to zero.
     */
    void ResetBrokenCount() noexcept { m_BrokenCount = 0; }

private:
    // =========================================================================
    // Internal Helper Methods
    // =========================================================================

    /**
     * @brief Creates a Jolt HingeConstraint from component data.
     */
    JPH::Constraint* CreateHingeConstraint(HingeConstraintComponent& hinge);

    /**
     * @brief Creates a Jolt SliderConstraint from component data.
     */
    JPH::Constraint* CreateSliderConstraint(SliderConstraintComponent& slider);

    /**
     * @brief Creates a Jolt DistanceConstraint (spring) from component data.
     */
    JPH::Constraint* CreateSpringConstraint(SpringConstraintComponent& spring);

    /**
     * @brief Resolves an entity reference to a Jolt BodyID.
     *
     * @param entity The entity to resolve, or entt::null for world anchor.
     * @return The corresponding Jolt BodyID.
     */
    JPH::BodyID ResolveBodyID(entt::entity entity) const;

    /**
     * @brief Converts ECS pivot position to Jolt world space.
     */
    JPH::Vec3 ConvertPivot(const Physics::Math::Vec3& pivot) const;

    /**
     * @brief Converts ECS axis direction to Jolt format.
     */
    JPH::Vec3 ConvertAxis(const Physics::Math::Vec3& axis) const;

    // =========================================================================
    // Member Variables
    // =========================================================================

    /** @brief Reference to the Jolt Physics world wrapper. */
    Physics::PhysicsWorld& m_PhysicsWorld;

    /** @brief Pointer to the ECS registry (set during Update). */
    entt::registry* m_Registry{ nullptr };

    /** @brief Count of currently active Jolt constraints. */
    uint32_t m_ConstraintCount{ 0 };

    /** @brief Count of constraints that have broken. */
    uint32_t m_BrokenCount{ 0 };

    /** @brief Accumulated time for rate-limited breakage checks. */
    float m_BreakageCheckAccumulator{ 0.0f };

    /** @brief Interval between full breakage check passes (seconds). */
    static constexpr float BREAKAGE_CHECK_INTERVAL = 0.1f;

    /** @brief Temporary storage for entities pending constraint destruction. */
    std::vector<entt::entity> m_PendingDestruction;
};

} // namespace Core::ECS
