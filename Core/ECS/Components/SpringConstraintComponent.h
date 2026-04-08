#pragma once

/**
 * @file SpringConstraintComponent.h
 * @brief ECS component for spring (distance) constraints in physics simulation.
 *
 * Provides a spring-damper constraint that maintains a distance relationship between
 * two rigid bodies with configurable elasticity and damping. Supports both traditional
 * stiffness/damping parameterization and frequency/damping-ratio parameterization for
 * intuitive tuning.
 *
 * Common applications include:
 * - Vehicle suspension systems
 * - Bungee cords and elastic ropes
 * - Soft body approximations
 * - Procedural animation connections
 * - Breakable elastic constraints
 *
 * @note This component extends ConstraintBase and integrates with Jolt Physics
 *       through the ConstraintSystem.
 *
 * @see ConstraintBase for common constraint properties
 * @see ConstraintSystem for physics synchronization
 * @see HingeConstraintComponent for rotational constraints
 * @see SliderConstraintComponent for translational constraints
 */

#include "Core/Physics/Constraints/ConstraintTypes.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace Core::ECS {

/**
 * @brief Mathematical constant PI for spring calculations.
 */
inline constexpr float SPRING_PI = 3.14159265358979323846f;

/**
 * @struct SpringConstraintComponent
 * @brief Defines an elastic spring-damper constraint between two bodies.
 *
 * The spring constraint connects two bodies (or one body to the world) with a
 * spring-damper system that applies forces to maintain a target rest length.
 * The spring can be configured with length limits, and offers two parameterization
 * modes for tuning behavior.
 *
 * ## Spring Behavior
 * The spring applies a force according to Hooke's Law:
 *   F_spring = -k * (x - RestLength)
 *   F_damping = -c * v
 * Where k is Stiffness, c is Damping, x is current length, and v is relative velocity.
 *
 * ## Length Limits
 * The spring can be constrained to operate within a length range:
 * - MinLength: Minimum compressed length (spring becomes rigid below this)
 * - MaxLength: Maximum extended length (spring becomes rigid above this)
 *
 * ## Parameterization Modes
 * The constraint supports two ways to specify spring dynamics:
 *
 * ### 1. Traditional Mode (UseFrequencyDamping = false)
 * Specify Stiffness (N/m) and Damping (N·s/m) directly.
 * - Intuitive for physics-based applications
 * - Stiffness: Higher = stiffer spring
 * - Damping: Higher = more energy dissipation
 *
 * ### 2. Frequency Mode (UseFrequencyDamping = true)
 * Specify Frequency (Hz) and DampingRatio (dimensionless).
 * - Intuitive for animation and game feel tuning
 * - Frequency: Oscillation rate when undamped
 * - DampingRatio: 0 = undamped oscillation, 1 = critically damped (no overshoot)
 *
 * The system internally converts frequency-based parameters to stiffness/damping
 * based on the connected body masses.
 *
 * @example Creating a vehicle suspension spring
 * @code
 * auto suspension = SpringConstraintComponent::CreateSuspension(
 *     wheelEntity, chassisEntity,
 *     0.35f,    // 35cm rest length
 *     45000.0f, // 45 kN/m stiffness (typical car)
 *     2500.0f   // 2.5 kN·s/m damping
 * );
 * registry.emplace<SpringConstraintComponent>(wheelEntity, suspension);
 * @endcode
 *
 * @example Creating a bungee cord
 * @code
 * auto bungee = SpringConstraintComponent::CreateBungee(
 *     playerEntity, anchorEntity,
 *     10.0f,    // 10m natural length
 *     800.0f    // 800 N/m stiffness
 * );
 * registry.emplace<SpringConstraintComponent>(playerEntity, bungee);
 * @endcode
 *
 * @example Creating a frequency-tuned spring
 * @code
 * auto spring = SpringConstraintComponent::CreateFrequencyBased(
 *     objectA, objectB,
 *     2.0f,  // 2m rest length
 *     4.0f,  // 4 Hz oscillation frequency
 *     0.7f   // Slightly underdamped for "bouncy" feel
 * );
 * registry.emplace<SpringConstraintComponent>(objectA, spring);
 * @endcode
 */
struct SpringConstraintComponent : public Physics::ConstraintBase
{
    // =========================================================================
    // Spring Length Configuration
    // =========================================================================

    /**
     * @brief Natural (rest) length of the spring in meters.
     *
     * The spring applies no force when at this length. Forces are applied
     * proportional to the deviation from this rest length.
     *
     * @note Must be positive. For zero-length springs, use a very small value.
     */
    float RestLength{ 1.0f };

    /**
     * @brief Minimum allowed length of the spring in meters.
     *
     * When the spring is compressed below this length, it behaves as a rigid
     * constraint preventing further compression. Set to 0 to allow full compression.
     *
     * @note Must be less than or equal to RestLength.
     */
    float MinLength{ 0.0f };

    /**
     * @brief Maximum allowed length of the spring in meters.
     *
     * When the spring is extended beyond this length, it behaves as a rigid
     * constraint preventing further extension. Set to FLT_MAX for unlimited extension.
     *
     * @note Must be greater than or equal to RestLength.
     */
    float MaxLength{ FLT_MAX };

    // =========================================================================
    // Traditional Spring Parameters (Stiffness/Damping)
    // =========================================================================

    /**
     * @brief Spring stiffness constant in Newtons per meter (N/m).
     *
     * Determines how strongly the spring resists displacement from rest length.
     * Higher values create stiffer, more responsive springs.
     *
     * Typical values:
     * - Soft spring (trampoline): 100-500 N/m
     * - Medium spring (bungee): 500-2000 N/m
     * - Stiff spring (car suspension): 20000-80000 N/m
     * - Very stiff (truck suspension): 100000+ N/m
     *
     * @note Only used when UseFrequencyDamping is false.
     */
    float Stiffness{ 1000.0f };

    /**
     * @brief Damping coefficient in Newton-seconds per meter (N·s/m).
     *
     * Controls energy dissipation and oscillation decay. Higher values
     * reduce oscillation and make the spring settle faster.
     *
     * Typical values:
     * - Light damping (bouncy): 10-100 N·s/m
     * - Medium damping: 100-500 N·s/m
     * - Heavy damping (shock absorber): 1000-5000 N·s/m
     *
     * @note Only used when UseFrequencyDamping is false.
     */
    float Damping{ 100.0f };

    // =========================================================================
    // Frequency-Based Parameters (Alternative Mode)
    // =========================================================================

    /**
     * @brief Enables frequency-based spring parameterization.
     *
     * When true, the spring uses Frequency and DampingRatio instead of
     * Stiffness and Damping. This mode is often more intuitive for
     * game designers and animators.
     *
     * The conversion formulas (for a given mass m) are:
     *   Stiffness = m * (2π * Frequency)²
     *   Damping = 2 * m * DampingRatio * (2π * Frequency)
     */
    bool UseFrequencyDamping{ false };

    /**
     * @brief Natural oscillation frequency in Hertz (cycles per second).
     *
     * Determines how quickly the spring oscillates when undamped.
     * Higher frequencies create faster, "snappier" springs.
     *
     * Typical values:
     * - Slow, heavy feel: 1-2 Hz
     * - Medium responsiveness: 3-5 Hz
     * - Quick, responsive: 6-10 Hz
     * - Very snappy: 10-20 Hz
     *
     * @note Only used when UseFrequencyDamping is true.
     */
    float Frequency{ 5.0f };

    /**
     * @brief Damping ratio (dimensionless).
     *
     * Controls how quickly oscillations decay:
     * - 0.0: Undamped - perpetual oscillation
     * - 0.0-1.0: Underdamped - oscillates with decay
     * - 1.0: Critically damped - fastest return without overshoot
     * - >1.0: Overdamped - slow return without oscillation
     *
     * For most game applications, values between 0.3 and 0.8 feel natural.
     * Vehicle suspension typically uses 0.2-0.5 for comfort.
     *
     * @note Only used when UseFrequencyDamping is true.
     */
    float DampingRatio{ 0.5f };

    // =========================================================================
    // Runtime State (Read-Only)
    // =========================================================================

    /**
     * @brief Current length of the spring in meters.
     *
     * Updated by the ConstraintSystem each physics step. Represents the
     * actual distance between the two attachment points.
     *
     * @note Do not modify directly; this is synchronized from Jolt Physics.
     */
    float CurrentLength{ 0.0f };

    /**
     * @brief Current force magnitude being applied by the spring in Newtons.
     *
     * Positive values indicate tension (spring is extended beyond rest length).
     * Negative values indicate compression (spring is compressed below rest length).
     *
     * Updated by the ConstraintSystem each physics step.
     *
     * @note Do not modify directly; this is synchronized from Jolt Physics.
     */
    float CurrentForce{ 0.0f };

    // =========================================================================
    // Constructors
    // =========================================================================

    /**
     * @brief Default constructor creating an unconfigured spring constraint.
     */
    SpringConstraintComponent() = default;

    // =========================================================================
    // Factory Methods
    // =========================================================================

    /**
     * @brief Creates a vehicle suspension spring constraint.
     *
     * Configures a spring suitable for vehicle suspension with typical automotive
     * characteristics. Uses traditional stiffness/damping parameterization for
     * precise control over suspension behavior.
     *
     * @param wheelBody Entity with RigidBodyComponent for the wheel/unsprung mass.
     * @param chassisBody Entity with RigidBodyComponent for the vehicle chassis.
     * @param restLength Natural suspension length in meters (default: 0.3m).
     * @param stiffness Spring rate in N/m (default: 50000 N/m, typical sedan).
     * @param damping Damping coefficient in N·s/m (default: 3000 N·s/m).
     * @return Configured SpringConstraintComponent ready for attachment.
     *
     * @note For realistic suspension, also consider adding appropriate MinLength
     *       (bump stop) and MaxLength (droop limit) after creation.
     *
     * @example
     * @code
     * // Soft sports car suspension
     * auto frontSuspension = SpringConstraintComponent::CreateSuspension(
     *     frontWheelEntity, chassisEntity,
     *     0.28f,     // 28cm rest length
     *     35000.0f,  // Soft spring rate
     *     2000.0f    // Light damping for comfort
     * );
     *
     * // Stiff racing suspension
     * auto rearSuspension = SpringConstraintComponent::CreateSuspension(
     *     rearWheelEntity, chassisEntity,
     *     0.25f,     // 25cm rest length
     *     80000.0f,  // Very stiff
     *     5000.0f    // Heavy damping
     * );
     * @endcode
     */
    [[nodiscard]] static SpringConstraintComponent CreateSuspension(
        entt::entity wheelBody,
        entt::entity chassisBody,
        float restLength = 0.3f,
        float stiffness = 50000.0f,
        float damping = 3000.0f)
    {
        SpringConstraintComponent spring;

        // Body references
        spring.BodyA = wheelBody;
        spring.BodyB = chassisBody;

        // Pivot points at body centers (typically adjusted per-vehicle)
        spring.PivotA = Physics::Math::Vec3{ 0.0f, 0.0f, 0.0f };
        spring.PivotB = Physics::Math::Vec3{ 0.0f, 0.0f, 0.0f };

        // Spring length configuration
        spring.RestLength = restLength;
        spring.MinLength = restLength * 0.3f;  // 30% compression limit (bump stop)
        spring.MaxLength = restLength * 1.5f;  // 150% extension limit (droop)

        // Traditional parameterization for suspension
        spring.UseFrequencyDamping = false;
        spring.Stiffness = stiffness;
        spring.Damping = damping;

        // Initialize runtime state
        spring.CurrentLength = restLength;
        spring.CurrentForce = 0.0f;

        spring.NeedsSync = true;

        return spring;
    }

    /**
     * @brief Creates a bungee cord/elastic rope constraint.
     *
     * Configures a one-sided spring that only applies force when extended beyond
     * its rest length. When compressed, the bungee goes slack and applies no force.
     * Ideal for elastic cords, rope bridges, and grappling hooks.
     *
     * @param objectBody Entity with RigidBodyComponent for the attached object.
     * @param anchorBody Entity with RigidBodyComponent for the anchor point,
     *                   or entt::null to anchor to the world.
     * @param restLength Natural length of the bungee cord in meters.
     * @param stiffness Elasticity of the bungee in N/m (default: 500 N/m).
     * @return Configured SpringConstraintComponent ready for attachment.
     *
     * @note The MinLength is set to 0, allowing full slack. The spring only
     *       engages when CurrentLength > RestLength.
     *
     * @example
     * @code
     * // Bungee jumping cord
     * auto bungeeCord = SpringConstraintComponent::CreateBungee(
     *     jumperEntity, bridgeEntity,
     *     25.0f,    // 25m cord length
     *     1500.0f   // Strong elastic
     * );
     *
     * // Grappling hook rope
     * auto grapple = SpringConstraintComponent::CreateBungee(
     *     playerEntity, entt::null,  // World anchor
     *     15.0f,    // 15m rope
     *     2000.0f   // Stiff rope
     * );
     * grapple.PivotB = worldAnchorPosition;
     * @endcode
     */
    [[nodiscard]] static SpringConstraintComponent CreateBungee(
        entt::entity objectBody,
        entt::entity anchorBody,
        float restLength,
        float stiffness = 500.0f)
    {
        SpringConstraintComponent spring;

        // Body references
        spring.BodyA = objectBody;
        spring.BodyB = anchorBody;

        // Pivot points at body centers
        spring.PivotA = Physics::Math::Vec3{ 0.0f, 0.0f, 0.0f };
        spring.PivotB = Physics::Math::Vec3{ 0.0f, 0.0f, 0.0f };

        // Bungee configuration: no minimum length (goes slack)
        spring.RestLength = restLength;
        spring.MinLength = 0.0f;  // Can go completely slack
        spring.MaxLength = FLT_MAX;  // No maximum stretch limit

        // Traditional parameterization
        spring.UseFrequencyDamping = false;
        spring.Stiffness = stiffness;
        spring.Damping = stiffness * 0.1f;  // Light damping (10% of stiffness)

        // Initialize runtime state
        spring.CurrentLength = restLength;
        spring.CurrentForce = 0.0f;

        spring.NeedsSync = true;

        return spring;
    }

    /**
     * @brief Creates a spring using frequency-based parameterization.
     *
     * Configures a spring using oscillation frequency and damping ratio, which
     * is often more intuitive for game designers. The physics system automatically
     * converts these to stiffness/damping values based on body masses.
     *
     * @param bodyA First entity with RigidBodyComponent.
     * @param bodyB Second entity with RigidBodyComponent,
     *              or entt::null to anchor to the world.
     * @param restLength Natural spring length in meters.
     * @param frequencyHz Oscillation frequency in Hz (default: 5 Hz).
     * @param dampingRatio Damping ratio, 0-1+ range (default: 0.7, slightly underdamped).
     * @return Configured SpringConstraintComponent ready for attachment.
     *
     * @note Frequency-based springs automatically adapt to mass changes,
     *       maintaining consistent "feel" regardless of body masses.
     *
     * @example
     * @code
     * // Bouncy platform spring
     * auto bouncySpring = SpringConstraintComponent::CreateFrequencyBased(
     *     platformEntity, baseEntity,
     *     1.0f,   // 1m rest length
     *     3.0f,   // Slow, bouncy feel
     *     0.3f    // Very bouncy (low damping)
     * );
     *
     * // Critically damped connection (no overshoot)
     * auto smoothSpring = SpringConstraintComponent::CreateFrequencyBased(
     *     followObject, targetObject,
     *     0.5f,   // 0.5m rest length
     *     8.0f,   // Fast response
     *     1.0f    // Critically damped
     * );
     *
     * // Overdamped, sluggish connection
     * auto slowSpring = SpringConstraintComponent::CreateFrequencyBased(
     *     heavyObject, anchorEntity,
     *     2.0f,   // 2m rest length
     *     2.0f,   // Slow oscillation
     *     1.5f    // Overdamped
     * );
     * @endcode
     */
    [[nodiscard]] static SpringConstraintComponent CreateFrequencyBased(
        entt::entity bodyA,
        entt::entity bodyB,
        float restLength,
        float frequencyHz = 5.0f,
        float dampingRatio = 0.7f)
    {
        SpringConstraintComponent spring;

        // Body references
        spring.BodyA = bodyA;
        spring.BodyB = bodyB;

        // Pivot points at body centers
        spring.PivotA = Physics::Math::Vec3{ 0.0f, 0.0f, 0.0f };
        spring.PivotB = Physics::Math::Vec3{ 0.0f, 0.0f, 0.0f };

        // Spring length configuration (standard limits)
        spring.RestLength = restLength;
        spring.MinLength = 0.0f;
        spring.MaxLength = FLT_MAX;

        // Frequency-based parameterization
        spring.UseFrequencyDamping = true;
        spring.Frequency = frequencyHz;
        spring.DampingRatio = dampingRatio;

        // Traditional parameters will be computed by physics system
        // Set reasonable defaults for editor display
        spring.Stiffness = 1000.0f;  // Placeholder
        spring.Damping = 100.0f;     // Placeholder

        // Initialize runtime state
        spring.CurrentLength = restLength;
        spring.CurrentForce = 0.0f;

        spring.NeedsSync = true;

        return spring;
    }

    // =========================================================================
    // Utility Methods
    // =========================================================================

    /**
     * @brief Sets the spring length limits.
     *
     * @param minLen Minimum length (compression limit).
     * @param maxLen Maximum length (extension limit).
     *
     * @note Values are clamped to ensure minLen <= RestLength <= maxLen.
     */
    void SetLengthLimits(float minLen, float maxLen)
    {
        MinLength = std::max(0.0f, minLen);
        MaxLength = std::max(minLen, maxLen);
        NeedsSync = true;
    }

    /**
     * @brief Configures the spring using traditional stiffness/damping parameters.
     *
     * @param stiffnessValue Spring constant in N/m.
     * @param dampingValue Damping coefficient in N·s/m.
     */
    void SetStiffnessAndDamping(float stiffnessValue, float dampingValue)
    {
        UseFrequencyDamping = false;
        Stiffness = std::max(0.0f, stiffnessValue);
        Damping = std::max(0.0f, dampingValue);
        NeedsSync = true;
    }

    /**
     * @brief Configures the spring using frequency-based parameters.
     *
     * @param frequencyHz Natural frequency in Hz.
     * @param ratio Damping ratio (0 = undamped, 1 = critically damped).
     */
    void SetFrequencyAndDampingRatio(float frequencyHz, float ratio)
    {
        UseFrequencyDamping = true;
        Frequency = std::max(0.01f, frequencyHz);  // Minimum 0.01 Hz
        DampingRatio = std::max(0.0f, ratio);
        NeedsSync = true;
    }

    /**
     * @brief Calculates the spring displacement from rest length.
     *
     * @return Positive value if extended, negative if compressed.
     */
    [[nodiscard]] float GetDisplacement() const
    {
        return CurrentLength - RestLength;
    }

    /**
     * @brief Calculates the normalized compression/extension ratio.
     *
     * @return 0.0 at MinLength, 1.0 at RestLength, values > 1.0 when extended.
     */
    [[nodiscard]] float GetNormalizedPosition() const
    {
        if (RestLength <= MinLength)
        {
            return 1.0f;
        }
        return (CurrentLength - MinLength) / (RestLength - MinLength);
    }

    /**
     * @brief Checks if the spring is currently compressed below rest length.
     *
     * @return True if compressed, false otherwise.
     */
    [[nodiscard]] bool IsCompressed() const
    {
        return CurrentLength < RestLength;
    }

    /**
     * @brief Checks if the spring is currently extended beyond rest length.
     *
     * @return True if extended, false otherwise.
     */
    [[nodiscard]] bool IsExtended() const
    {
        return CurrentLength > RestLength;
    }

    /**
     * @brief Checks if the spring has hit its minimum length (compression limit).
     *
     * @return True if at minimum compression, false otherwise.
     */
    [[nodiscard]] bool IsAtMinLength() const
    {
        return CurrentLength <= MinLength;
    }

    /**
     * @brief Checks if the spring has hit its maximum length (extension limit).
     *
     * @return True if at maximum extension, false otherwise.
     */
    [[nodiscard]] bool IsAtMaxLength() const
    {
        return CurrentLength >= MaxLength;
    }

    /**
     * @brief Checks if the spring is currently slack (bungee mode, length < rest).
     *
     * @return True if the spring would be slack in bungee mode.
     */
    [[nodiscard]] bool IsSlack() const
    {
        return CurrentLength < RestLength && MinLength == 0.0f;
    }

    /**
     * @brief Calculates the potential energy stored in the spring.
     *
     * Uses the formula: E = 0.5 * k * x² where k is stiffness and x is displacement.
     *
     * @return Potential energy in Joules.
     *
     * @note Only accurate when UseFrequencyDamping is false. When using
     *       frequency-based parameters, the actual stiffness depends on mass.
     */
    [[nodiscard]] float GetPotentialEnergy() const
    {
        float displacement = GetDisplacement();
        return 0.5f * Stiffness * displacement * displacement;
    }

    /**
     * @brief Estimates effective stiffness from frequency-based parameters.
     *
     * Calculates what the stiffness would be for a given mass using the formula:
     *   k = m * (2π * f)²
     *
     * @param effectiveMass The effective mass to use in calculation.
     * @return Estimated stiffness in N/m.
     */
    [[nodiscard]] float EstimateStiffnessForMass(float effectiveMass) const
    {
        if (!UseFrequencyDamping)
        {
            return Stiffness;
        }
        float omega = 2.0f * SPRING_PI * Frequency;
        return effectiveMass * omega * omega;
    }

    /**
     * @brief Estimates effective damping from frequency-based parameters.
     *
     * Calculates what the damping would be for a given mass using the formula:
     *   c = 2 * m * ζ * (2π * f)
     *
     * @param effectiveMass The effective mass to use in calculation.
     * @return Estimated damping coefficient in N·s/m.
     */
    [[nodiscard]] float EstimateDampingForMass(float effectiveMass) const
    {
        if (!UseFrequencyDamping)
        {
            return Damping;
        }
        float omega = 2.0f * SPRING_PI * Frequency;
        return 2.0f * effectiveMass * DampingRatio * omega;
    }

    /**
     * @brief Calculates the critical damping coefficient for current stiffness.
     *
     * Critical damping is the minimum damping required to prevent oscillation.
     *   c_crit = 2 * sqrt(k * m)
     *
     * @param effectiveMass The effective mass of the system.
     * @return Critical damping coefficient in N·s/m.
     */
    [[nodiscard]] float GetCriticalDamping(float effectiveMass) const
    {
        float k = UseFrequencyDamping ? EstimateStiffnessForMass(effectiveMass) : Stiffness;
        return 2.0f * std::sqrt(k * effectiveMass);
    }

    /**
     * @brief Resets the spring to its rest state.
     *
     * Sets runtime values to their default rest state.
     */
    void Reset()
    {
        CurrentLength = RestLength;
        CurrentForce = 0.0f;
        IsBroken = false;
        NeedsSync = true;
    }
};

} // namespace Core::ECS
