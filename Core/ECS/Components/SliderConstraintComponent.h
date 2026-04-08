#pragma once

/**
 * @file SliderConstraintComponent.h
 * @brief ECS component for slider (prismatic) constraints in physics simulation.
 *
 * Provides a prismatic joint that allows translation along a single axis between two
 * rigid bodies. Supports linear limits, spring-damper limit softening, velocity motors,
 * and position motors for applications such as pistons, sliding doors, drawers,
 * elevators, and telescoping mechanisms.
 *
 * @note This component extends ConstraintBase and integrates with Jolt Physics
 *       through the ConstraintSystem.
 *
 * @see ConstraintBase for common constraint properties
 * @see ConstraintSystem for physics synchronization
 * @see HingeConstraintComponent for rotational constraints
 */

#include "Core/Physics/Constraints/ConstraintTypes.h"

#include <cmath>

namespace Core::ECS {

/**
 * @struct SliderConstraintComponent
 * @brief Defines a translational constraint allowing one degree of freedom along a slider axis.
 *
 * The slider constraint connects two bodies (or one body to the world) and permits
 * movement only along the specified slider axis. The constraint can be configured
 * with linear limits, soft limit springs, a velocity motor for constant-speed motion,
 * or a position motor for targeting specific positions.
 *
 * ## Coordinate System
 * - SliderAxis: The axis of translation, specified in BodyA's local space.
 * - NormalAxis: A reference axis perpendicular to the slider axis, used to lock
 *               orientation and prevent rotation. Must be orthogonal to SliderAxis.
 *
 * ## Motor Modes
 * The constraint supports two mutually exclusive motor modes:
 * 1. **Velocity Motor** (MotorEnabled): Maintains a constant target velocity.
 *    Ideal for conveyor belts, constant-speed actuators.
 * 2. **Position Motor** (PositionMotorEnabled): Drives to a target position using
 *    spring-damper dynamics. Ideal for precise positioning, servo mechanisms.
 *
 * @note If both MotorEnabled and PositionMotorEnabled are true, position motor takes precedence.
 *
 * ## Limit Softness
 * LimitSpring and LimitDamping control how "hard" the limits feel:
 * - High spring + low damping = bouncy limits
 * - Low spring + high damping = soft, cushioned limits
 * - Set both to 0 for perfectly rigid limits
 *
 * @example Creating a piston constraint
 * @code
 * auto piston = SliderConstraintComponent::CreatePiston(
 *     pistonEntity, cylinderEntity,
 *     Math::Vec3{0.0f, 1.0f, 0.0f},  // Vertical piston axis
 *     0.0f, 0.5f                      // 0 to 0.5 meter stroke
 * );
 * registry.emplace<SliderConstraintComponent>(pistonEntity, piston);
 * @endcode
 *
 * @example Creating a motorized elevator
 * @code
 * auto elevator = SliderConstraintComponent::CreateElevator(
 *     platformEntity, shaftEntity,
 *     0.0f, 30.0f,  // Ground floor to 30 meters
 *     2.5f          // 2.5 m/s travel speed
 * );
 * elevator.GoToPosition(15.0f);  // Go to floor at 15 meters
 * registry.emplace<SliderConstraintComponent>(platformEntity, elevator);
 * @endcode
 */
struct SliderConstraintComponent : public Physics::ConstraintBase
{
    // =========================================================================
    // Slider Axis Configuration
    // =========================================================================

    /**
     * @brief The axis of translation in BodyA's local coordinate space.
     *
     * Movement is permitted only along this axis. The default is the X-axis,
     * suitable for horizontal sliding mechanisms like drawers.
     *
     * @note Must be a normalized vector for correct physics behavior.
     */
    Physics::Math::Vec3 SliderAxis{ 1.0f, 0.0f, 0.0f };

    /**
     * @brief Reference axis perpendicular to SliderAxis for orientation lock.
     *
     * Used to prevent rotation around axes other than the slider axis.
     * This axis helps maintain the orientation relationship between the
     * two connected bodies.
     *
     * @note Must be orthogonal to SliderAxis and normalized.
     */
    Physics::Math::Vec3 NormalAxis{ 0.0f, 1.0f, 0.0f };

    // =========================================================================
    // Linear Limits
    // =========================================================================

    /**
     * @brief Enables linear limit enforcement.
     *
     * When true, translation is constrained between MinPosition and MaxPosition.
     * When false, unlimited translation is permitted along the slider axis.
     */
    bool HasLimits{ false };

    /**
     * @brief Minimum translation position in meters.
     *
     * Measured from the initial constraint position along SliderAxis.
     * Negative values indicate movement in the negative axis direction.
     * Only enforced when HasLimits is true.
     *
     * @note Default of -1.0m allows 1 meter of travel in the negative direction.
     */
    float MinPosition{ -1.0f };

    /**
     * @brief Maximum translation position in meters.
     *
     * Measured from the initial constraint position along SliderAxis.
     * Positive values indicate movement in the positive axis direction.
     * Only enforced when HasLimits is true.
     *
     * @note Default of 1.0m allows 1 meter of travel in the positive direction.
     */
    float MaxPosition{ 1.0f };

    /**
     * @brief Spring stiffness coefficient for soft linear limits (N/m).
     *
     * Controls how strongly the constraint resists exceeding limits. Higher values
     * create stiffer, more rigid-feeling limits. Set to 0 for rigid limits.
     *
     * @note Only affects behavior when HasLimits is true.
     */
    float LimitSpring{ 0.0f };

    /**
     * @brief Damping coefficient for soft linear limits (N·s/m).
     *
     * Reduces oscillation when the slider reaches its limits. Higher values
     * create more damped, less bouncy limit behavior.
     *
     * @note Only affects behavior when HasLimits is true.
     */
    float LimitDamping{ 0.0f };

    // =========================================================================
    // Velocity Motor Configuration
    // =========================================================================

    /**
     * @brief Enables the linear velocity motor.
     *
     * When true, the constraint actively applies force to achieve
     * MotorTargetVelocity, up to MotorMaxForce.
     *
     * @note Mutually exclusive with PositionMotorEnabled. If both are enabled,
     *       position motor takes precedence.
     */
    bool MotorEnabled{ false };

    /**
     * @brief Target linear velocity for the motor in meters per second.
     *
     * Positive values move in the positive direction along SliderAxis.
     * Only used when MotorEnabled is true and PositionMotorEnabled is false.
     */
    float MotorTargetVelocity{ 0.0f };

    /**
     * @brief Maximum force the velocity motor can apply in Newtons.
     *
     * Limits the force the motor exerts. Higher values allow the motor to
     * overcome greater resistance and accelerate heavier loads.
     * Only used when MotorEnabled is true.
     *
     * @note Default of 10000 N is suitable for most game scenarios including
     *       heavy machinery and industrial equipment.
     */
    float MotorMaxForce{ 10000.0f };

    // =========================================================================
    // Position Motor Configuration
    // =========================================================================

    /**
     * @brief Enables the position-targeting motor.
     *
     * When true, the constraint uses spring-damper dynamics to drive toward
     * TargetPosition. This provides servo-like positioning behavior.
     *
     * @note Takes precedence over velocity motor if both are enabled.
     */
    bool PositionMotorEnabled{ false };

    /**
     * @brief Target position for the position motor in meters.
     *
     * The slider will be driven toward this position using spring-damper
     * dynamics controlled by PositionSpring and PositionDamping.
     * Only used when PositionMotorEnabled is true.
     */
    float TargetPosition{ 0.0f };

    /**
     * @brief Spring stiffness for position motor (N/m).
     *
     * Controls how aggressively the motor drives toward TargetPosition.
     * Higher values result in faster, stiffer response but may cause
     * overshoot if damping is too low.
     *
     * @note Only affects behavior when PositionMotorEnabled is true.
     */
    float PositionSpring{ 1000.0f };

    /**
     * @brief Damping coefficient for position motor (N·s/m).
     *
     * Controls oscillation damping when approaching TargetPosition.
     * Higher values reduce overshoot but slow the response.
     * Critical damping ≈ 2 * sqrt(mass * PositionSpring).
     *
     * @note Only affects behavior when PositionMotorEnabled is true.
     */
    float PositionDamping{ 100.0f };

    // =========================================================================
    // Runtime State
    // =========================================================================

    /**
     * @brief Current translation position in meters (read-only runtime state).
     *
     * Updated by the ConstraintSystem each physics step. Represents the
     * current displacement from the initial position along SliderAxis.
     *
     * @note Do not modify directly; this is synchronized from Jolt Physics.
     */
    float CurrentPosition{ 0.0f };

    // =========================================================================
    // Constructors
    // =========================================================================

    /**
     * @brief Default constructor creating an unconfigured slider constraint.
     */
    SliderConstraintComponent() = default;

    // =========================================================================
    // Factory Methods
    // =========================================================================

    /**
     * @brief Creates a piston-style slider constraint with linear limits.
     *
     * Configures a slider suitable for hydraulic/pneumatic pistons that translate
     * between minimum and maximum extents. The piston body slides relative to
     * the cylinder body along the specified axis.
     *
     * @param pistonBody Entity with RigidBodyComponent for the piston rod.
     * @param cylinderBody Entity with RigidBodyComponent for the cylinder housing,
     *                     or entt::null to anchor to the world.
     * @param axis The axis of translation in piston body's local space (must be normalized).
     * @param minExtent Minimum extension position in meters (typically 0 or negative).
     * @param maxExtent Maximum extension position in meters.
     * @return Configured SliderConstraintComponent ready for attachment.
     *
     * @example
     * @code
     * // Create a vertical hydraulic piston with 0.8m stroke
     * auto piston = SliderConstraintComponent::CreatePiston(
     *     rodEntity, cylinderEntity,
     *     Math::Vec3{0.0f, 1.0f, 0.0f},  // Y-axis (vertical)
     *     0.0f, 0.8f                      // 0 to 0.8 meter stroke
     * );
     *
     * // Create a horizontal pneumatic actuator
     * auto actuator = SliderConstraintComponent::CreatePiston(
     *     shaftEntity, housingEntity,
     *     Math::Vec3{1.0f, 0.0f, 0.0f},  // X-axis (horizontal)
     *     -0.2f, 0.2f                     // +/- 0.2 meter stroke
     * );
     * @endcode
     */
    [[nodiscard]] static SliderConstraintComponent CreatePiston(
        entt::entity pistonBody,
        entt::entity cylinderBody,
        const Physics::Math::Vec3& axis,
        float minExtent,
        float maxExtent)
    {
        SliderConstraintComponent slider;

        // Body references
        slider.BodyA = pistonBody;
        slider.BodyB = cylinderBody;

        // Pivot points at body origins (adjust as needed for specific geometry)
        slider.PivotA = Physics::Math::Vec3{ 0.0f, 0.0f, 0.0f };
        slider.PivotB = Physics::Math::Vec3{ 0.0f, 0.0f, 0.0f };

        // Configure slider axis
        slider.SliderAxis = axis;

        // Compute a perpendicular normal axis
        // Use cross product with a non-parallel vector
        if (std::abs(axis.x) < 0.9f)
        {
            slider.NormalAxis = glm::normalize(glm::cross(axis, Physics::Math::Vec3{ 1.0f, 0.0f, 0.0f }));
        }
        else
        {
            slider.NormalAxis = glm::normalize(glm::cross(axis, Physics::Math::Vec3{ 0.0f, 1.0f, 0.0f }));
        }

        // Linear limits for piston stroke
        slider.HasLimits = true;
        slider.MinPosition = minExtent;
        slider.MaxPosition = maxExtent;

        // Moderate limit softness for mechanical feel
        slider.LimitSpring = 5000.0f;
        slider.LimitDamping = 500.0f;

        // No motor by default
        slider.MotorEnabled = false;
        slider.PositionMotorEnabled = false;

        slider.NeedsSync = true;

        return slider;
    }

    /**
     * @brief Creates a drawer-style slider constraint for furniture mechanics.
     *
     * Configures a slider suitable for drawers, sliding shelves, or similar
     * mechanisms that pull out from a container. The drawer slides along the
     * negative Z-axis (pull toward user) with limits from closed to fully extended.
     *
     * @param drawerBody Entity with RigidBodyComponent for the drawer.
     * @param cabinetBody Entity with RigidBodyComponent for the cabinet/container,
     *                    or entt::null to anchor to the world.
     * @param maxPullOut Maximum pull-out distance in meters (default: 0.5m).
     * @return Configured SliderConstraintComponent ready for attachment.
     *
     * @example
     * @code
     * // Create a standard desk drawer
     * auto drawer = SliderConstraintComponent::CreateDrawer(
     *     drawerEntity, deskEntity,
     *     0.45f  // 45cm pull-out
     * );
     *
     * // Create a filing cabinet drawer with deep extension
     * auto fileDrawer = SliderConstraintComponent::CreateDrawer(
     *     fileDrawerEntity, cabinetEntity,
     *     0.7f  // 70cm full extension
     * );
     * @endcode
     */
    [[nodiscard]] static SliderConstraintComponent CreateDrawer(
        entt::entity drawerBody,
        entt::entity cabinetBody,
        float maxPullOut = 0.5f)
    {
        SliderConstraintComponent slider;

        // Body references
        slider.BodyA = drawerBody;
        slider.BodyB = cabinetBody;

        // Pivot at drawer back (where slides would attach)
        slider.PivotA = Physics::Math::Vec3{ 0.0f, 0.0f, 0.0f };
        slider.PivotB = Physics::Math::Vec3{ 0.0f, 0.0f, 0.0f };

        // Drawer pulls out along negative Z-axis (toward user in typical setup)
        slider.SliderAxis = Physics::Math::Vec3{ 0.0f, 0.0f, -1.0f };
        slider.NormalAxis = Physics::Math::Vec3{ 0.0f, 1.0f, 0.0f };

        // Limits: closed (0) to fully extended
        slider.HasLimits = true;
        slider.MinPosition = 0.0f;
        slider.MaxPosition = maxPullOut;

        // Soft stops for natural drawer feel
        slider.LimitSpring = 200.0f;
        slider.LimitDamping = 50.0f;

        // No motor (user-operated)
        slider.MotorEnabled = false;
        slider.PositionMotorEnabled = false;

        slider.NeedsSync = true;

        return slider;
    }

    /**
     * @brief Creates an elevator-style slider constraint with position motor.
     *
     * Configures a vertical slider with a position motor for elevator platforms,
     * lifts, and vertical transport mechanisms. The platform moves along the
     * Y-axis between floor levels at a controlled speed.
     *
     * @param platformBody Entity with RigidBodyComponent for the elevator platform.
     * @param shaftBody Entity with RigidBodyComponent for the elevator shaft,
     *                  or entt::null to anchor to the world.
     * @param bottomFloor Height of the lowest floor in meters.
     * @param topFloor Height of the highest floor in meters.
     * @param speed Maximum travel speed in meters per second (default: 2.0 m/s).
     * @return Configured SliderConstraintComponent ready for attachment.
     *
     * @note Use GoToPosition() to command the elevator to specific floors.
     *
     * @example
     * @code
     * // Create a 3-story elevator (0m, 4m, 8m floors)
     * auto elevator = SliderConstraintComponent::CreateElevator(
     *     platformEntity, shaftEntity,
     *     0.0f, 8.0f,  // Ground to top floor
     *     3.0f         // 3 m/s (fast elevator)
     * );
     *
     * // Send to second floor
     * elevator.GoToPosition(4.0f);
     *
     * // Create a slow freight elevator
     * auto freight = SliderConstraintComponent::CreateElevator(
     *     freightPlatformEntity, entt::null,
     *     -5.0f, 20.0f,  // Basement to 5th floor
     *     1.0f           // 1 m/s (slow, heavy-duty)
     * );
     * @endcode
     */
    [[nodiscard]] static SliderConstraintComponent CreateElevator(
        entt::entity platformBody,
        entt::entity shaftBody,
        float bottomFloor,
        float topFloor,
        float speed = 2.0f)
    {
        SliderConstraintComponent slider;

        // Body references
        slider.BodyA = platformBody;
        slider.BodyB = shaftBody;

        // Pivot at platform center
        slider.PivotA = Physics::Math::Vec3{ 0.0f, 0.0f, 0.0f };
        slider.PivotB = Physics::Math::Vec3{ 0.0f, 0.0f, 0.0f };

        // Vertical movement axis (Y-up)
        slider.SliderAxis = Physics::Math::Vec3{ 0.0f, 1.0f, 0.0f };
        slider.NormalAxis = Physics::Math::Vec3{ 1.0f, 0.0f, 0.0f };

        // Floor limits
        slider.HasLimits = true;
        slider.MinPosition = bottomFloor;
        slider.MaxPosition = topFloor;

        // Hard stops at floor limits (safety)
        slider.LimitSpring = 0.0f;
        slider.LimitDamping = 0.0f;

        // Enable position motor for floor targeting
        slider.PositionMotorEnabled = true;
        slider.TargetPosition = bottomFloor;  // Start at bottom floor

        // Calculate spring/damping for desired speed response
        // Using critically damped system approximation
        // For an elevator, we want smooth acceleration/deceleration
        const float effectiveMass = 1000.0f;  // Assume ~1000kg platform for tuning
        slider.PositionSpring = effectiveMass * speed * speed;
        slider.PositionDamping = 2.0f * std::sqrt(effectiveMass * slider.PositionSpring);

        // Also configure velocity motor as backup/alternative
        slider.MotorEnabled = false;
        slider.MotorTargetVelocity = speed;
        slider.MotorMaxForce = 50000.0f;  // 50kN for heavy platforms

        slider.NeedsSync = true;

        return slider;
    }

    // =========================================================================
    // Utility Methods
    // =========================================================================

    /**
     * @brief Sets the linear limits and enables limit enforcement.
     *
     * @param minPos Minimum position in meters.
     * @param maxPos Maximum position in meters.
     * @param spring Optional spring stiffness for soft limits (N/m).
     * @param damping Optional damping for soft limits (N·s/m).
     */
    void SetLimits(float minPos, float maxPos, float spring = 0.0f, float damping = 0.0f)
    {
        HasLimits = true;
        MinPosition = minPos;
        MaxPosition = maxPos;
        LimitSpring = spring;
        LimitDamping = damping;
        NeedsSync = true;
    }

    /**
     * @brief Disables linear limit enforcement.
     */
    void ClearLimits()
    {
        HasLimits = false;
        NeedsSync = true;
    }

    /**
     * @brief Configures and enables the velocity motor.
     *
     * Disables position motor if it was enabled (velocity and position motors
     * are mutually exclusive).
     *
     * @param targetVelocity Desired linear velocity in m/s.
     * @param maxForce Maximum force the motor can apply in Newtons.
     */
    void SetVelocityMotor(float targetVelocity, float maxForce = 10000.0f)
    {
        PositionMotorEnabled = false;  // Mutually exclusive
        MotorEnabled = true;
        MotorTargetVelocity = targetVelocity;
        MotorMaxForce = maxForce;
        NeedsSync = true;
    }

    /**
     * @brief Configures and enables the position motor.
     *
     * Disables velocity motor if it was enabled (velocity and position motors
     * are mutually exclusive).
     *
     * @param targetPos Target position in meters.
     * @param spring Spring stiffness in N/m (default: 1000).
     * @param damping Damping coefficient in N·s/m (default: 100).
     */
    void SetPositionMotor(float targetPos, float spring = 1000.0f, float damping = 100.0f)
    {
        MotorEnabled = false;  // Mutually exclusive
        PositionMotorEnabled = true;
        TargetPosition = targetPos;
        PositionSpring = spring;
        PositionDamping = damping;
        NeedsSync = true;
    }

    /**
     * @brief Commands the slider to move to a target position.
     *
     * Enables the position motor and sets the target. For velocity-controlled
     * motion, use SetVelocityMotor() instead.
     *
     * @param position Target position in meters.
     */
    void GoToPosition(float position)
    {
        PositionMotorEnabled = true;
        TargetPosition = position;

        // Clamp to limits if enabled
        if (HasLimits)
        {
            if (TargetPosition < MinPosition) TargetPosition = MinPosition;
            if (TargetPosition > MaxPosition) TargetPosition = MaxPosition;
        }

        NeedsSync = true;
    }

    /**
     * @brief Disables all motors.
     */
    void DisableMotors()
    {
        MotorEnabled = false;
        PositionMotorEnabled = false;
        NeedsSync = true;
    }

    /**
     * @brief Checks if the slider is currently at or beyond its minimum limit.
     *
     * @return True if at minimum limit, false otherwise.
     */
    [[nodiscard]] bool IsAtMinLimit() const
    {
        return HasLimits && (CurrentPosition <= MinPosition);
    }

    /**
     * @brief Checks if the slider is currently at or beyond its maximum limit.
     *
     * @return True if at maximum limit, false otherwise.
     */
    [[nodiscard]] bool IsAtMaxLimit() const
    {
        return HasLimits && (CurrentPosition >= MaxPosition);
    }

    /**
     * @brief Gets the normalized position as a ratio between limits.
     *
     * @return Value between 0.0 (at MinPosition) and 1.0 (at MaxPosition),
     *         or 0.5 if limits are not enabled or range is zero.
     */
    [[nodiscard]] float GetNormalizedPosition() const
    {
        if (!HasLimits)
        {
            return 0.5f;
        }

        const float range = MaxPosition - MinPosition;
        if (range <= 0.0f)
        {
            return 0.5f;
        }

        const float normalized = (CurrentPosition - MinPosition) / range;

        // Clamp to [0, 1]
        if (normalized < 0.0f) return 0.0f;
        if (normalized > 1.0f) return 1.0f;
        return normalized;
    }

    /**
     * @brief Calculates the distance from current position to target.
     *
     * Useful for determining if the slider has reached its target position.
     *
     * @return Absolute distance in meters from CurrentPosition to TargetPosition.
     */
    [[nodiscard]] float GetDistanceToTarget() const
    {
        return std::abs(TargetPosition - CurrentPosition);
    }

    /**
     * @brief Checks if the slider has approximately reached its target position.
     *
     * @param tolerance Position tolerance in meters (default: 1cm).
     * @return True if within tolerance of target position.
     */
    [[nodiscard]] bool HasReachedTarget(float tolerance = 0.01f) const
    {
        return GetDistanceToTarget() <= tolerance;
    }
};

} // namespace Core::ECS