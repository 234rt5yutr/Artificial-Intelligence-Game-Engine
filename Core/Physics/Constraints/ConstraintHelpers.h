#pragma once

/**
 * @file ConstraintHelpers.h
 * @brief Utility functions for creating and managing Jolt Physics constraints.
 *
 * This header provides a comprehensive set of helper functions that bridge the gap
 * between the engine's ECS constraint components and Jolt Physics constraint objects.
 * It handles the conversion of component data to Jolt-specific types and manages
 * constraint creation, motor updates, and force/torque queries.
 *
 * ## Design Philosophy
 * These helpers follow the principle of single responsibility - each function does
 * one thing well. The namespace encapsulates all Jolt Physics interaction logic,
 * making it easy to modify physics backend integration without affecting the ECS layer.
 *
 * ## Thread Safety
 * Constraint creation functions require exclusive access to the PhysicsSystem.
 * Motor and parameter update functions should be called from the physics update thread
 * or while the physics simulation is paused.
 *
 * @see HingeConstraintComponent for hinge joint configuration
 * @see SliderConstraintComponent for prismatic joint configuration
 * @see SpringConstraintComponent for spring-damper configuration
 * @see ConstraintSystem for ECS integration
 */

#include "Core/Math/Math.h"

// Jolt Physics forward declarations - avoid including full Jolt headers
namespace JPH
{
    class PhysicsSystem;
    class Constraint;
    class HingeConstraint;
    class SliderConstraint;
    class DistanceConstraint;
    class BodyID;
    class Vec3;
    class Quat;

    template<typename T>
    class Ref;
}

// Forward declarations for ECS components
namespace Core::ECS
{
    struct HingeConstraintComponent;
    struct SliderConstraintComponent;
    struct SpringConstraintComponent;
}

namespace Core::Physics::JoltConstraintHelpers
{

// =============================================================================
// Type Conversion Utilities
// =============================================================================

/**
 * @brief Converts an engine Math::Vec3 to a Jolt Physics JPH::Vec3.
 *
 * This is the primary vector conversion function for interfacing with Jolt Physics.
 * The conversion is direct as both types use the same underlying component layout.
 *
 * @param v The engine vector to convert.
 * @return A Jolt Physics vector with the same component values.
 *
 * @note This function is marked inline for performance-critical code paths.
 *
 * @example
 * @code
 * Math::Vec3 pivot{1.0f, 2.0f, 3.0f};
 * JPH::Vec3 joltPivot = ToJPHVec3(pivot);
 * @endcode
 */
[[nodiscard]] inline JPH::Vec3 ToJPHVec3(const Math::Vec3& v);

/**
 * @brief Converts an engine Math::Quat to a Jolt Physics JPH::Quat.
 *
 * Handles the quaternion conversion between GLM and Jolt Physics.
 * Note that Jolt uses (x, y, z, w) component ordering internally.
 *
 * @param q The engine quaternion to convert.
 * @return A Jolt Physics quaternion with equivalent rotation.
 *
 * @note Ensure the input quaternion is normalized for correct physics behavior.
 *
 * @example
 * @code
 * Math::Quat rotation = glm::angleAxis(PI * 0.5f, Math::Vec3{0, 1, 0});
 * JPH::Quat joltRot = ToJPHQuat(rotation);
 * @endcode
 */
[[nodiscard]] inline JPH::Quat ToJPHQuat(const Math::Quat& q);

/**
 * @brief Converts a Jolt Physics JPH::Vec3 to an engine Math::Vec3.
 *
 * The inverse operation of ToJPHVec3, used when reading data back from
 * the physics simulation.
 *
 * @param v The Jolt Physics vector to convert.
 * @return An engine vector with the same component values.
 *
 * @example
 * @code
 * JPH::Vec3 joltForce = constraint->GetTotalLambdaPosition();
 * Math::Vec3 engineForce = FromJPHVec3(joltForce);
 * @endcode
 */
[[nodiscard]] inline Math::Vec3 FromJPHVec3(const JPH::Vec3& v);

// =============================================================================
// Constraint Creation Functions
// =============================================================================

/**
 * @brief Creates a Jolt HingeConstraint from an ECS HingeConstraintComponent.
 *
 * Constructs and configures a hinge (revolute) joint in the Jolt Physics system
 * based on the parameters specified in the ECS component. The created constraint
 * is automatically added to the physics system.
 *
 * ## Constraint Configuration
 * The function configures:
 * - Hinge axis and normal axis orientation
 * - Angular limits (if HasLimits is true)
 * - Limit spring/damper softness
 * - Motor settings (if MotorEnabled is true)
 *
 * @param physicsSystem Reference to the Jolt PhysicsSystem managing the simulation.
 * @param bodyA The BodyID of the first body (must be valid).
 * @param bodyB The BodyID of the second body (use JPH::BodyID() for world anchor).
 * @param component The HingeConstraintComponent containing configuration data.
 * @return A reference-counted pointer to the created HingeConstraint.
 *
 * @pre bodyA must reference a valid body in physicsSystem.
 * @pre If bodyB is not null, it must reference a valid body in physicsSystem.
 * @post The returned constraint is added to physicsSystem and active.
 *
 * @throws std::runtime_error if constraint creation fails (invalid bodies, etc.).
 *
 * @note The constraint's reference count is managed by the returned Ref<>.
 *       Store this in the component or constraint system to keep it alive.
 *
 * @example
 * @code
 * auto& hingeComp = registry.get<HingeConstraintComponent>(entity);
 * JPH::BodyID bodyA = GetJoltBodyID(hingeComp.BodyA);
 * JPH::BodyID bodyB = GetJoltBodyID(hingeComp.BodyB);
 *
 * auto constraint = JoltConstraintHelpers::CreateHingeConstraint(
 *     physicsWorld.GetPhysicsSystem(),
 *     bodyA, bodyB,
 *     hingeComp
 * );
 *
 * hingeComp.JoltConstraint = constraint.GetPtr();
 * @endcode
 */
[[nodiscard]] JPH::Ref<JPH::HingeConstraint> CreateHingeConstraint(
    JPH::PhysicsSystem& physicsSystem,
    JPH::BodyID bodyA,
    JPH::BodyID bodyB,
    const ECS::HingeConstraintComponent& component);

/**
 * @brief Creates a Jolt SliderConstraint from an ECS SliderConstraintComponent.
 *
 * Constructs and configures a slider (prismatic) joint in the Jolt Physics system
 * based on the parameters specified in the ECS component. The created constraint
 * is automatically added to the physics system.
 *
 * ## Constraint Configuration
 * The function configures:
 * - Slider axis and normal axis orientation
 * - Linear limits (if HasLimits is true)
 * - Limit spring/damper softness
 * - Velocity motor settings (if MotorEnabled is true)
 * - Position motor settings (if PositionMotorEnabled is true)
 *
 * @param physicsSystem Reference to the Jolt PhysicsSystem managing the simulation.
 * @param bodyA The BodyID of the first body (must be valid).
 * @param bodyB The BodyID of the second body (use JPH::BodyID() for world anchor).
 * @param component The SliderConstraintComponent containing configuration data.
 * @return A reference-counted pointer to the created SliderConstraint.
 *
 * @pre bodyA must reference a valid body in physicsSystem.
 * @pre If bodyB is not null, it must reference a valid body in physicsSystem.
 * @post The returned constraint is added to physicsSystem and active.
 *
 * @throws std::runtime_error if constraint creation fails.
 *
 * @note If both MotorEnabled and PositionMotorEnabled are true in the component,
 *       the position motor takes precedence.
 *
 * @example
 * @code
 * auto& sliderComp = registry.get<SliderConstraintComponent>(entity);
 * auto constraint = JoltConstraintHelpers::CreateSliderConstraint(
 *     physics.GetPhysicsSystem(),
 *     bodyAID, bodyBID,
 *     sliderComp
 * );
 * @endcode
 */
[[nodiscard]] JPH::Ref<JPH::SliderConstraint> CreateSliderConstraint(
    JPH::PhysicsSystem& physicsSystem,
    JPH::BodyID bodyA,
    JPH::BodyID bodyB,
    const ECS::SliderConstraintComponent& component);

/**
 * @brief Creates a Jolt DistanceConstraint from an ECS SpringConstraintComponent.
 *
 * Constructs and configures a distance (spring) constraint in the Jolt Physics system.
 * Jolt's DistanceConstraint is used to implement spring-damper behavior between bodies.
 *
 * ## Constraint Configuration
 * The function configures:
 * - Rest length (equilibrium distance)
 * - Min/max length limits
 * - Spring stiffness and damping
 * - Frequency-based parameters (if UseFrequencyDamping is true)
 *
 * @param physicsSystem Reference to the Jolt PhysicsSystem managing the simulation.
 * @param bodyA The BodyID of the first body (must be valid).
 * @param bodyB The BodyID of the second body (use JPH::BodyID() for world anchor).
 * @param component The SpringConstraintComponent containing configuration data.
 * @return A reference-counted pointer to the created DistanceConstraint.
 *
 * @pre bodyA must reference a valid body in physicsSystem.
 * @pre If bodyB is not null, it must reference a valid body in physicsSystem.
 * @post The returned constraint is added to physicsSystem and active.
 *
 * @throws std::runtime_error if constraint creation fails.
 *
 * @note Jolt's DistanceConstraint supports spring-damper dynamics natively,
 *       making it ideal for implementing SpringConstraintComponent behavior.
 *
 * @example
 * @code
 * auto& springComp = registry.get<SpringConstraintComponent>(entity);
 * auto constraint = JoltConstraintHelpers::CreateDistanceConstraint(
 *     physics.GetPhysicsSystem(),
 *     suspensionMountID, wheelID,
 *     springComp
 * );
 * @endcode
 */
[[nodiscard]] JPH::Ref<JPH::DistanceConstraint> CreateDistanceConstraint(
    JPH::PhysicsSystem& physicsSystem,
    JPH::BodyID bodyA,
    JPH::BodyID bodyB,
    const ECS::SpringConstraintComponent& component);

// =============================================================================
// Motor Update Functions
// =============================================================================

/**
 * @brief Updates the motor settings of an existing HingeConstraint.
 *
 * Synchronizes the motor configuration from an ECS component to an existing
 * Jolt HingeConstraint. This allows runtime modification of motor parameters
 * without recreating the constraint.
 *
 * ## Updated Parameters
 * - Motor state (enabled/disabled)
 * - Target angular velocity
 * - Maximum torque limit
 *
 * @param constraint Pointer to the HingeConstraint to update (must not be null).
 * @param component The HingeConstraintComponent containing the new motor settings.
 *
 * @pre constraint must be a valid, non-null pointer to an active HingeConstraint.
 * @post The constraint's motor settings are updated to match the component.
 *
 * @note This function is safe to call every frame for dynamic motor control.
 *       Jolt handles the parameter changes efficiently.
 *
 * @warning Calling with a null constraint will result in undefined behavior.
 *
 * @example
 * @code
 * // Gradually increase motor speed
 * hingeComp.MotorTargetVelocity += deltaTime * 0.5f;
 * JoltConstraintHelpers::UpdateHingeMotor(
 *     static_cast<JPH::HingeConstraint*>(hingeComp.JoltConstraint),
 *     hingeComp
 * );
 * @endcode
 */
void UpdateHingeMotor(
    JPH::HingeConstraint* constraint,
    const ECS::HingeConstraintComponent& component);

/**
 * @brief Updates the motor settings of an existing SliderConstraint.
 *
 * Synchronizes both velocity and position motor configurations from an ECS
 * component to an existing Jolt SliderConstraint. Supports runtime switching
 * between motor modes.
 *
 * ## Updated Parameters
 * - Velocity motor state and settings
 * - Position motor state, target, and spring settings
 * - Maximum force limits
 *
 * @param constraint Pointer to the SliderConstraint to update (must not be null).
 * @param component The SliderConstraintComponent containing the new motor settings.
 *
 * @pre constraint must be a valid, non-null pointer to an active SliderConstraint.
 * @post The constraint's motor settings are updated to match the component.
 *
 * @note If both motor modes are enabled in the component, position motor is used.
 *
 * @example
 * @code
 * // Move elevator to target floor
 * sliderComp.PositionMotorEnabled = true;
 * sliderComp.TargetPosition = floorHeights[targetFloor];
 * JoltConstraintHelpers::UpdateSliderMotor(
 *     static_cast<JPH::SliderConstraint*>(sliderComp.JoltConstraint),
 *     sliderComp
 * );
 * @endcode
 */
void UpdateSliderMotor(
    JPH::SliderConstraint* constraint,
    const ECS::SliderConstraintComponent& component);

/**
 * @brief Updates the spring parameters of an existing DistanceConstraint.
 *
 * Synchronizes spring-damper settings from an ECS component to an existing
 * Jolt DistanceConstraint. Handles both stiffness/damping and frequency/ratio
 * parameterization modes.
 *
 * ## Updated Parameters
 * - Rest length
 * - Min/max length limits
 * - Stiffness and damping (or frequency and damping ratio)
 *
 * @param constraint Pointer to the DistanceConstraint to update (must not be null).
 * @param component The SpringConstraintComponent containing the new spring settings.
 *
 * @pre constraint must be a valid, non-null pointer to an active DistanceConstraint.
 * @post The constraint's spring settings are updated to match the component.
 *
 * @note When UseFrequencyDamping is true, the function converts frequency-based
 *       parameters to stiffness/damping internally.
 *
 * @example
 * @code
 * // Stiffen suspension during high-speed driving
 * springComp.Stiffness = baseStiffness * (1.0f + speed * 0.01f);
 * JoltConstraintHelpers::UpdateSpringParameters(
 *     static_cast<JPH::DistanceConstraint*>(springComp.JoltConstraint),
 *     springComp
 * );
 * @endcode
 */
void UpdateSpringParameters(
    JPH::DistanceConstraint* constraint,
    const ECS::SpringConstraintComponent& component);

// =============================================================================
// Constraint Query Functions
// =============================================================================

/**
 * @brief Retrieves the total constraint force magnitude from a Jolt constraint.
 *
 * Calculates the magnitude of the force being applied by the constraint to maintain
 * its configured behavior. This is useful for implementing breakable constraints
 * and constraint stress visualization.
 *
 * ## Force Interpretation
 * The returned value represents the magnitude of the Lagrange multiplier for
 * positional constraints, scaled to force units. For different constraint types:
 * - HingeConstraint: Force keeping bodies at the hinge pivot
 * - SliderConstraint: Force keeping bodies on the slider axis
 * - DistanceConstraint: Spring/tension force between bodies
 *
 * @param constraint Pointer to any Jolt Constraint (must not be null).
 * @return The magnitude of the constraint force in Newtons.
 *
 * @pre constraint must be a valid, non-null pointer to an active Constraint.
 *
 * @note The accuracy of this value depends on the physics time step. Smaller
 *       time steps generally yield more accurate force readings.
 *
 * @note For breakable constraints, compare this value against BreakForce
 *       to determine if the constraint should break.
 *
 * @example
 * @code
 * float force = JoltConstraintHelpers::GetConstraintForce(
 *     hingeComp.JoltConstraint
 * );
 *
 * if (hingeComp.IsBreakable && force > hingeComp.BreakForce)
 * {
 *     hingeComp.IsBroken = true;
 *     // Remove constraint from physics system
 * }
 * @endcode
 */
[[nodiscard]] float GetConstraintForce(JPH::Constraint* constraint);

/**
 * @brief Retrieves the total constraint torque magnitude from a Jolt constraint.
 *
 * Calculates the magnitude of the torque being applied by the constraint to maintain
 * its configured rotational behavior. Primarily useful for hinge constraints and
 * implementing breakable rotational joints.
 *
 * ## Torque Interpretation
 * The returned value represents the magnitude of the Lagrange multiplier for
 * rotational constraints, scaled to torque units. For different constraint types:
 * - HingeConstraint: Torque maintaining axis alignment + motor torque
 * - SliderConstraint: Torque preventing rotation off-axis
 * - DistanceConstraint: Generally zero (no rotational constraint)
 *
 * @param constraint Pointer to any Jolt Constraint (must not be null).
 * @return The magnitude of the constraint torque in Newton-meters.
 *
 * @pre constraint must be a valid, non-null pointer to an active Constraint.
 *
 * @note For constraints with motors, this includes the motor torque.
 *       Use with BreakTorque thresholds for breakable joint behavior.
 *
 * @example
 * @code
 * float torque = JoltConstraintHelpers::GetConstraintTorque(
 *     hingeComp.JoltConstraint
 * );
 *
 * // Check if excessive torque would break the hinge
 * if (hingeComp.IsBreakable && torque > hingeComp.BreakTorque)
 * {
 *     TriggerBreakEffect(entity, torque);
 *     hingeComp.IsBroken = true;
 * }
 * @endcode
 */
[[nodiscard]] float GetConstraintTorque(JPH::Constraint* constraint);

} // namespace Core::Physics::JoltConstraintHelpers
