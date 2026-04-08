#pragma once

/**
 * @file HingeConstraintComponent.h
 * @brief ECS component for hinge (revolute) constraints in physics simulation.
 *
 * Provides a hinge joint that allows rotation around a single axis between two
 * rigid bodies. Supports angular limits, spring-damper limit softening, and
 * motorized rotation for applications such as doors, wheels, propellers, and
 * motorized joints.
 *
 * @note This component extends ConstraintBase and integrates with Jolt Physics
 *       through the ConstraintSystem.
 *
 * @see ConstraintBase for common constraint properties
 * @see ConstraintSystem for physics synchronization
 */

#include "Core/Physics/Constraints/ConstraintTypes.h"

#include <cmath>

namespace Core::ECS {

/**
 * @brief Mathematical constant PI as a constexpr for compile-time evaluation.
 */
inline constexpr float PI = 3.14159265358979323846f;

/**
 * @struct HingeConstraintComponent
 * @brief Defines a rotational constraint allowing one degree of freedom around a hinge axis.
 *
 * The hinge constraint connects two bodies (or one body to the world) and permits
 * rotation only around the specified hinge axis. The constraint can be configured
 * with angular limits, soft limit springs, and a motor for powered rotation.
 *
 * ## Coordinate System
 * - HingeAxis: The axis of rotation, specified in BodyA's local space.
 * - NormalAxis: A reference axis perpendicular to the hinge axis, used for angle measurement.
 *               Must be orthogonal to HingeAxis.
 *
 * ## Motor Behavior
 * When MotorEnabled is true, the constraint applies torque to reach MotorTargetVelocity.
 * The motor respects MotorMaxTorque as an upper bound on applied torque.
 *
 * ## Limit Softness
 * LimitSpring and LimitDamping control how "hard" the limits feel:
 * - High spring + low damping = bouncy limits
 * - Low spring + high damping = soft, mushy limits
 * - Set both to 0 for perfectly rigid limits
 *
 * @example Creating a door constraint
 * @code
 * auto doorHinge = HingeConstraintComponent::CreateDoor(
 *     doorEntity, frameEntity,
 *     Math::Vec3{-0.5f, 0.0f, 0.0f},  // Pivot at door edge
 *     PI * 0.5f                        // Opens 90 degrees
 * );
 * registry.emplace<HingeConstraintComponent>(doorEntity, doorHinge);
 * @endcode
 *
 * @example Creating a motorized rotating platform
 * @code
 * auto motor = HingeConstraintComponent::CreateMotorized(
 *     platformEntity, entt::null,
 *     Math::Vec3{0.0f},               // Center pivot
 *     0.5f,                            // 0.5 rad/s rotation
 *     500.0f                           // 500 N·m max torque
 * );
 * registry.emplace<HingeConstraintComponent>(platformEntity, motor);
 * @endcode
 */
struct HingeConstraintComponent : public Physics::ConstraintBase
{
    // =========================================================================
    // Hinge Axis Configuration
    // =========================================================================

    /**
     * @brief The axis of rotation in BodyA's local coordinate space.
     *
     * Rotation is permitted only around this axis. The default is the Y-axis,
     * suitable for vertical hinges like doors.
     *
     * @note Must be a normalized vector for correct physics behavior.
     */
    Physics::Math::Vec3 HingeAxis{ 0.0f, 1.0f, 0.0f };

    /**
     * @brief Reference axis perpendicular to HingeAxis for angle measurement.
     *
     * Used to compute CurrentAngle and evaluate angular limits. This axis
     * defines the "zero angle" reference direction.
     *
     * @note Must be orthogonal to HingeAxis and normalized.
     */
    Physics::Math::Vec3 NormalAxis{ 1.0f, 0.0f, 0.0f };

    // =========================================================================
    // Angular Limits
    // =========================================================================

    /**
     * @brief Enables angular limit enforcement.
     *
     * When true, rotation is constrained between MinAngle and MaxAngle.
     * When false, unlimited rotation is permitted.
     */
    bool HasLimits{ false };

    /**
     * @brief Minimum rotation angle in radians.
     *
     * Measured from the NormalAxis reference. Only enforced when HasLimits is true.
     * Default allows full rotation (-π).
     */
    float MinAngle{ -PI };

    /**
     * @brief Maximum rotation angle in radians.
     *
     * Measured from the NormalAxis reference. Only enforced when HasLimits is true.
     * Default allows full rotation (+π).
     */
    float MaxAngle{ PI };

    /**
     * @brief Spring stiffness coefficient for soft angular limits (N·m/rad).
     *
     * Controls how strongly the constraint resists exceeding limits. Higher values
     * create stiffer, more rigid-feeling limits. Set to 0 for rigid limits.
     *
     * @note Only affects behavior when HasLimits is true.
     */
    float LimitSpring{ 0.0f };

    /**
     * @brief Damping coefficient for soft angular limits (N·m·s/rad).
     *
     * Reduces oscillation when the hinge reaches its limits. Higher values
     * create more damped, less bouncy limit behavior.
     *
     * @note Only affects behavior when HasLimits is true.
     */
    float LimitDamping{ 0.0f };

    // =========================================================================
    // Motor Configuration
    // =========================================================================

    /**
     * @brief Enables the angular velocity motor.
     *
     * When true, the constraint actively applies torque to achieve
     * MotorTargetVelocity, up to MotorMaxTorque.
     */
    bool MotorEnabled{ false };

    /**
     * @brief Target angular velocity for the motor in radians per second.
     *
     * Positive values rotate in the positive direction around HingeAxis
     * (right-hand rule). Only used when MotorEnabled is true.
     */
    float MotorTargetVelocity{ 0.0f };

    /**
     * @brief Maximum torque the motor can apply in Newton-meters.
     *
     * Limits the force the motor exerts. Higher values allow the motor to
     * overcome greater resistance. Only used when MotorEnabled is true.
     *
     * @note Default of 1000 N·m is suitable for most game scenarios.
     */
    float MotorMaxTorque{ 1000.0f };

    // =========================================================================
    // Runtime State
    // =========================================================================

    /**
     * @brief Current rotation angle in radians (read-only runtime state).
     *
     * Updated by the ConstraintSystem each physics step. Represents the
     * current angle measured from the NormalAxis reference around HingeAxis.
     *
     * @note Do not modify directly; this is synchronized from Jolt Physics.
     */
    float CurrentAngle{ 0.0f };

    // =========================================================================
    // Constructors
    // =========================================================================

    /**
     * @brief Default constructor creating an unconfigured hinge constraint.
     */
    HingeConstraintComponent() = default;

    // =========================================================================
    // Factory Methods
    // =========================================================================

    /**
     * @brief Creates a door-style hinge constraint with angular limits.
     *
     * Configures a hinge suitable for doors that swing open to a maximum angle
     * and cannot swing past the closed position. The hinge axis is vertical (Y-up).
     *
     * @param doorBody Entity with RigidBodyComponent for the door.
     * @param frameBody Entity with RigidBodyComponent for the door frame,
     *                  or entt::null to anchor to the world.
     * @param hingePivot Local position of the hinge point on the door body
     *                   (typically at the edge where hinges would be).
     * @param maxOpenAngle Maximum opening angle in radians (default: π/2 = 90°).
     * @return Configured HingeConstraintComponent ready for attachment.
     *
     * @example
     * @code
     * // Create a door that opens 120 degrees
     * auto hinge = HingeConstraintComponent::CreateDoor(
     *     doorEntity, frameEntity,
     *     Math::Vec3{-0.45f, 0.0f, 0.0f},
     *     PI * (2.0f / 3.0f)
     * );
     * @endcode
     */
    [[nodiscard]] static HingeConstraintComponent CreateDoor(
        entt::entity doorBody,
        entt::entity frameBody,
        const Physics::Math::Vec3& hingePivot,
        float maxOpenAngle = PI * 0.5f)
    {
        HingeConstraintComponent hinge;

        // Body references
        hinge.BodyA = doorBody;
        hinge.BodyB = frameBody;

        // Pivot points (door edge, same position on frame)
        hinge.PivotA = hingePivot;
        hinge.PivotB = hingePivot;

        // Vertical hinge axis (Y-up coordinate system)
        hinge.HingeAxis = Physics::Math::Vec3{ 0.0f, 1.0f, 0.0f };
        hinge.NormalAxis = Physics::Math::Vec3{ 1.0f, 0.0f, 0.0f };

        // Angular limits: closed (0) to fully open
        hinge.HasLimits = true;
        hinge.MinAngle = 0.0f;
        hinge.MaxAngle = maxOpenAngle;

        // Soft limit stops for natural door feel
        hinge.LimitSpring = 100.0f;
        hinge.LimitDamping = 10.0f;

        // No motor for standard doors
        hinge.MotorEnabled = false;

        hinge.NeedsSync = true;

        return hinge;
    }

    /**
     * @brief Creates a wheel-style hinge constraint with free rotation.
     *
     * Configures a hinge for wheels that can rotate freely without limits.
     * The hinge axis is horizontal (X-axis), perpendicular to forward motion.
     *
     * @param wheelBody Entity with RigidBodyComponent for the wheel.
     * @param chassisBody Entity with RigidBodyComponent for the vehicle chassis.
     * @param wheelPivot Local position where the wheel attaches to the chassis
     *                   (wheel center point).
     * @return Configured HingeConstraintComponent ready for attachment.
     *
     * @note For powered wheels, use CreateMotorized() instead or enable the
     *       motor after creation.
     *
     * @example
     * @code
     * // Create front-left wheel attachment
     * auto wheel = HingeConstraintComponent::CreateWheel(
     *     wheelEntity, chassisEntity,
     *     Math::Vec3{-1.0f, -0.3f, 1.5f}  // Left side, below chassis, front
     * );
     * @endcode
     */
    [[nodiscard]] static HingeConstraintComponent CreateWheel(
        entt::entity wheelBody,
        entt::entity chassisBody,
        const Physics::Math::Vec3& wheelPivot)
    {
        HingeConstraintComponent hinge;

        // Body references
        hinge.BodyA = wheelBody;
        hinge.BodyB = chassisBody;

        // Pivot at wheel center
        hinge.PivotA = Physics::Math::Vec3{ 0.0f, 0.0f, 0.0f };  // Wheel center
        hinge.PivotB = wheelPivot;  // Attachment point on chassis

        // Horizontal rotation axis (wheel spins around X-axis)
        hinge.HingeAxis = Physics::Math::Vec3{ 1.0f, 0.0f, 0.0f };
        hinge.NormalAxis = Physics::Math::Vec3{ 0.0f, 1.0f, 0.0f };

        // No angular limits for wheels
        hinge.HasLimits = false;

        // No motor by default (enable for driven wheels)
        hinge.MotorEnabled = false;

        hinge.NeedsSync = true;

        return hinge;
    }

    /**
     * @brief Creates a motorized hinge constraint for powered rotation.
     *
     * Configures a hinge with an active motor that maintains a target angular
     * velocity. Suitable for rotating platforms, fans, propellers, and powered
     * mechanisms.
     *
     * @param rotatingBody Entity with RigidBodyComponent that will rotate.
     * @param anchorBody Entity with RigidBodyComponent to anchor against,
     *                   or entt::null to anchor to the world.
     * @param pivot Local position of the rotation center on the rotating body.
     * @param speed Target angular velocity in radians per second.
     * @param torque Maximum motor torque in Newton-meters (default: 1000 N·m).
     * @return Configured HingeConstraintComponent ready for attachment.
     *
     * @example
     * @code
     * // Create a fan rotating at 10 rad/s (~95 RPM)
     * auto fan = HingeConstraintComponent::CreateMotorized(
     *     fanBladeEntity, fanHousingEntity,
     *     Math::Vec3{0.0f},
     *     10.0f,
     *     200.0f  // Light motor
     * );
     *
     * // Create a heavy industrial motor
     * auto motor = HingeConstraintComponent::CreateMotorized(
     *     armatureEntity, entt::null,
     *     Math::Vec3{0.0f},
     *     2.0f,
     *     5000.0f  // High torque motor
     * );
     * @endcode
     */
    [[nodiscard]] static HingeConstraintComponent CreateMotorized(
        entt::entity rotatingBody,
        entt::entity anchorBody,
        const Physics::Math::Vec3& pivot,
        float speed,
        float torque = 1000.0f)
    {
        HingeConstraintComponent hinge;

        // Body references
        hinge.BodyA = rotatingBody;
        hinge.BodyB = anchorBody;

        // Pivot at rotation center
        hinge.PivotA = pivot;
        hinge.PivotB = pivot;

        // Default vertical rotation axis
        hinge.HingeAxis = Physics::Math::Vec3{ 0.0f, 1.0f, 0.0f };
        hinge.NormalAxis = Physics::Math::Vec3{ 1.0f, 0.0f, 0.0f };

        // No angular limits for continuous rotation
        hinge.HasLimits = false;

        // Enable motor with specified parameters
        hinge.MotorEnabled = true;
        hinge.MotorTargetVelocity = speed;
        hinge.MotorMaxTorque = torque;

        hinge.NeedsSync = true;

        return hinge;
    }

    // =========================================================================
    // Utility Methods
    // =========================================================================

    /**
     * @brief Sets the angular limits and enables limit enforcement.
     *
     * @param minAngle Minimum angle in radians.
     * @param maxAngle Maximum angle in radians.
     * @param spring Optional spring stiffness for soft limits.
     * @param damping Optional damping for soft limits.
     */
    void SetLimits(float minAngle, float maxAngle, float spring = 0.0f, float damping = 0.0f)
    {
        HasLimits = true;
        MinAngle = minAngle;
        MaxAngle = maxAngle;
        LimitSpring = spring;
        LimitDamping = damping;
        NeedsSync = true;
    }

    /**
     * @brief Disables angular limit enforcement.
     */
    void ClearLimits()
    {
        HasLimits = false;
        NeedsSync = true;
    }

    /**
     * @brief Configures and enables the motor.
     *
     * @param targetVelocity Desired angular velocity in rad/s.
     * @param maxTorque Maximum torque the motor can apply.
     */
    void SetMotor(float targetVelocity, float maxTorque = 1000.0f)
    {
        MotorEnabled = true;
        MotorTargetVelocity = targetVelocity;
        MotorMaxTorque = maxTorque;
        NeedsSync = true;
    }

    /**
     * @brief Disables the motor.
     */
    void DisableMotor()
    {
        MotorEnabled = false;
        NeedsSync = true;
    }

    /**
     * @brief Gets the current angle normalized to [-π, π] range.
     *
     * @return Current angle in radians, normalized.
     */
    [[nodiscard]] float GetNormalizedAngle() const
    {
        float angle = CurrentAngle;
        while (angle > PI) angle -= 2.0f * PI;
        while (angle < -PI) angle += 2.0f * PI;
        return angle;
    }

    /**
     * @brief Checks if the hinge is currently at or beyond its minimum limit.
     *
     * @return True if at minimum limit, false otherwise.
     */
    [[nodiscard]] bool IsAtMinLimit() const
    {
        return HasLimits && (CurrentAngle <= MinAngle);
    }

    /**
     * @brief Checks if the hinge is currently at or beyond its maximum limit.
     *
     * @return True if at maximum limit, false otherwise.
     */
    [[nodiscard]] bool IsAtMaxLimit() const
    {
        return HasLimits && (CurrentAngle >= MaxAngle);
    }
};

} // namespace Core::ECS
