#pragma once

/**
 * @file ConstraintTypes.h
 * @brief Defines constraint types and base structures for Jolt Physics integration.
 *
 * This header provides the foundational constraint abstractions for the physics
 * system, enabling joints and connections between rigid bodies with support for
 * breakable constraints and world anchoring.
 */

#include <cfloat>
#include <cstdint>

// External dependencies
#include <entt/entt.hpp>
#include <glm/glm.hpp>

// Jolt Physics forward declarations
namespace JPH
{
    class Constraint;
}

namespace Core::Physics
{

/**
 * @brief Type alias for 3D vectors using GLM.
 */
namespace Math
{
    using Vec3 = glm::vec3;
}

/**
 * @enum ConstraintType
 * @brief Enumerates the supported physics constraint types.
 *
 * Each type corresponds to a specific Jolt Physics constraint implementation
 * with distinct degrees of freedom and behavioral characteristics.
 */
enum class ConstraintType : uint8_t
{
    /** @brief Allows rotation around a single axis (e.g., door hinges, wheels). */
    Hinge,

    /** @brief Allows translation along a single axis (e.g., pistons, sliding doors). */
    Slider,

    /** @brief Soft constraint with spring-damper dynamics for elastic connections. */
    Spring,

    /** @brief Limits rotation within a conical region (e.g., ragdoll joints). */
    Cone,

    /** @brief Rigidly connects two bodies with no relative movement allowed. */
    Fixed
};

/**
 * @struct ConstraintBase
 * @brief Base structure containing common properties for all constraint types.
 *
 * This structure serves as the foundation for the ECS constraint components,
 * storing body references, attachment points, breakage parameters, and
 * synchronization state with the underlying Jolt Physics constraint.
 *
 * @note When BodyA or BodyB is entt::null, the constraint is anchored to the
 *       world (static anchor point).
 *
 * @example
 * @code
 * ConstraintBase constraint{};
 * constraint.BodyA = playerEntity;
 * constraint.BodyB = entt::null;  // Anchor to world
 * constraint.PivotA = Math::Vec3{0.0f, 1.0f, 0.0f};
 * constraint.PivotB = Math::Vec3{0.0f, 5.0f, 0.0f};  // World position
 * constraint.IsBreakable = true;
 * constraint.BreakForce = 1000.0f;
 * @endcode
 */
struct ConstraintBase
{
    // =========================================================================
    // Body References
    // =========================================================================

    /**
     * @brief First body in the constraint pair.
     * @note Set to entt::null to anchor to the world.
     */
    entt::entity BodyA{ entt::null };

    /**
     * @brief Second body in the constraint pair.
     * @note Set to entt::null to anchor to the world.
     */
    entt::entity BodyB{ entt::null };

    // =========================================================================
    // Attachment Points (Local Space)
    // =========================================================================

    /**
     * @brief Attachment point on BodyA in local space coordinates.
     *
     * If BodyA is entt::null, this represents a world-space position.
     */
    Math::Vec3 PivotA{ 0.0f, 0.0f, 0.0f };

    /**
     * @brief Attachment point on BodyB in local space coordinates.
     *
     * If BodyB is entt::null, this represents a world-space position.
     */
    Math::Vec3 PivotB{ 0.0f, 0.0f, 0.0f };

    // =========================================================================
    // Constraint State
    // =========================================================================

    /**
     * @brief Controls whether the constraint is actively enforced.
     *
     * Disabled constraints remain in the system but do not affect body motion.
     */
    bool IsEnabled{ true };

    /**
     * @brief Indicates whether this constraint can break under force/torque.
     *
     * When true, the constraint will break if BreakForce or BreakTorque
     * thresholds are exceeded during simulation.
     */
    bool IsBreakable{ false };

    /**
     * @brief Force magnitude threshold for breaking the constraint (Newtons).
     *
     * Only applies when IsBreakable is true. Set to FLT_MAX for effectively
     * unbreakable constraints.
     */
    float BreakForce{ FLT_MAX };

    /**
     * @brief Torque magnitude threshold for breaking the constraint (Newton-meters).
     *
     * Only applies when IsBreakable is true. Set to FLT_MAX for effectively
     * unbreakable constraints.
     */
    float BreakTorque{ FLT_MAX };

    /**
     * @brief Runtime state indicating the constraint has been broken.
     *
     * Once set to true, the constraint is no longer enforced and should be
     * cleaned up by the constraint system.
     */
    bool IsBroken{ false };

    // =========================================================================
    // Jolt Physics Integration
    // =========================================================================

    /**
     * @brief Pointer to the underlying Jolt Physics constraint.
     *
     * Managed by the ConstraintSystem. Null until the constraint is
     * instantiated in the physics world.
     *
     * @warning Do not manually delete this pointer; it is ref-counted by Jolt.
     */
    JPH::Constraint* JoltConstraint{ nullptr };

    // =========================================================================
    // Synchronization
    // =========================================================================

    /**
     * @brief Dirty flag indicating the constraint requires synchronization.
     *
     * Set to true when constraint properties are modified and need to be
     * pushed to the Jolt Physics simulation.
     */
    bool NeedsSync{ true };
};

} // namespace Core::Physics
