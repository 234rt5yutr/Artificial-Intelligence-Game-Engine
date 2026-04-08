#pragma once

/**
 * @file ClothSolver.h
 * @brief Position Based Dynamics (PBD) solver for cloth simulation.
 *
 * This solver implements the core PBD algorithm for real-time cloth simulation,
 * including Verlet integration, constraint projection, and collision handling.
 * 
 * References:
 * - Müller et al., "Position Based Dynamics" (2007)
 * - Macklin et al., "XPBD: Position-Based Simulation of Compliant Constrained Dynamics" (2016)
 */

#include "Core/Math/Math.h"

namespace Core {
namespace Physics {

// Forward declarations
class ClothComponent;

/**
 * @brief Configuration constants for the cloth solver.
 */
namespace ClothSolverConfig {
    /** @brief Minimum distance threshold to prevent division by zero in constraint solving. */
    constexpr float MIN_DISTANCE = 0.001f;
    
    /** @brief Maximum allowable stretch ratio before constraints are clamped. */
    constexpr float MAX_STRETCH_RATIO = 1.5f;
}

/**
 * @class ClothSolver
 * @brief Static utility class implementing Position Based Dynamics for cloth simulation.
 *
 * The ClothSolver provides a complete PBD-based cloth simulation pipeline including:
 * - Verlet integration for time-stepping
 * - Iterative constraint projection for distance constraints
 * - External force application (gravity, wind)
 * - Collision detection and response
 * - Normal computation for rendering
 *
 * Usage:
 * @code
 * ClothSolver::Simulate(clothComponent, deltaTime);
 * @endcode
 */
class ClothSolver {
public:
    ClothSolver() = delete;
    ~ClothSolver() = delete;
    ClothSolver(const ClothSolver&) = delete;
    ClothSolver& operator=(const ClothSolver&) = delete;
    ClothSolver(ClothSolver&&) = delete;
    ClothSolver& operator=(ClothSolver&&) = delete;

    // =========================================================================
    // Main Simulation Entry Point
    // =========================================================================

    /**
     * @brief Main solver entry point - advances the cloth simulation by one time step.
     *
     * Executes the complete PBD simulation pipeline:
     * 1. Apply external forces (gravity)
     * 2. Verlet integration to predict new positions
     * 3. Iterative constraint projection
     * 4. Velocity update and damping
     * 5. Collision handling
     * 6. Normal recomputation
     *
     * @param cloth The cloth component to simulate.
     * @param deltaTime Time step in seconds.
     */
    static void Simulate(ClothComponent& cloth, float deltaTime);

    // =========================================================================
    // Integration
    // =========================================================================

    /**
     * @brief Performs Verlet integration to predict new particle positions.
     *
     * Uses the standard Verlet integration scheme:
     * x_new = x + (x - x_old) + a * dt^2
     *
     * This provides second-order accuracy and implicit velocity handling.
     *
     * @param cloth The cloth component containing particle data.
     * @param deltaTime Time step in seconds.
     */
    static void VerletIntegration(ClothComponent& cloth, float deltaTime);

    // =========================================================================
    // Constraint Solving
    // =========================================================================

    /**
     * @brief Iteratively projects constraints to satisfy distance requirements.
     *
     * Uses Gauss-Seidel iteration to solve all distance constraints.
     * The number of iterations affects simulation stability and stiffness.
     *
     * @param cloth The cloth component containing constraint data.
     */
    static void SolveConstraints(ClothComponent& cloth);

    /**
     * @brief Solves a single distance constraint between two particles.
     *
     * Projects particles along the constraint direction to satisfy the rest length.
     * Corrections are weighted by inverse mass for physically correct behavior.
     *
     * @param p1 Position of the first particle (modified in place).
     * @param p2 Position of the second particle (modified in place).
     * @param restLength The desired distance between particles.
     * @param stiffness Constraint stiffness in range [0, 1].
     * @param invMass1 Inverse mass of first particle (0 for fixed particles).
     * @param invMass2 Inverse mass of second particle (0 for fixed particles).
     */
    static void SolveDistanceConstraint(
        Math::Vec3& p1,
        Math::Vec3& p2,
        float restLength,
        float stiffness,
        float invMass1,
        float invMass2
    );

    // =========================================================================
    // External Forces
    // =========================================================================

    /**
     * @brief Applies gravitational acceleration to all particles.
     *
     * @param cloth The cloth component to apply gravity to.
     * @param deltaTime Time step in seconds.
     */
    static void ApplyGravity(ClothComponent& cloth, float deltaTime);

    /**
     * @brief Applies aerodynamic forces (wind) to the cloth.
     *
     * Computes drag and lift forces based on triangle normals and relative velocity.
     * Uses a simplified aerodynamic model suitable for real-time simulation.
     *
     * @param cloth The cloth component to apply wind forces to.
     * @param windVelocity The wind velocity vector in world space.
     * @param drag Drag coefficient (typically 0.0 - 1.0).
     * @param lift Lift coefficient (typically 0.0 - 1.0).
     */
    static void ApplyWind(
        ClothComponent& cloth,
        Math::Vec3 windVelocity,
        float drag,
        float lift
    );

    /**
     * @brief Applies velocity damping to reduce oscillations.
     *
     * Uses the cloth's internal damping coefficient to attenuate velocities.
     *
     * @param cloth The cloth component to apply damping to.
     */
    static void ApplyDamping(ClothComponent& cloth);

    // =========================================================================
    // Collision Handling
    // =========================================================================

    /**
     * @brief Handles collision response with a sphere collider.
     *
     * Pushes particles outside the sphere boundary and updates velocities
     * to prevent penetration.
     *
     * @param cloth The cloth component to check for collisions.
     * @param center Center of the sphere in world space.
     * @param radius Radius of the sphere.
     */
    static void HandleSphereCollision(
        ClothComponent& cloth,
        Math::Vec3 center,
        float radius
    );

    /**
     * @brief Handles collision response with a capsule collider.
     *
     * A capsule is defined as a line segment with a radius (swept sphere).
     *
     * @param cloth The cloth component to check for collisions.
     * @param p1 First endpoint of the capsule's central axis.
     * @param p2 Second endpoint of the capsule's central axis.
     * @param radius Radius of the capsule.
     */
    static void HandleCapsuleCollision(
        ClothComponent& cloth,
        Math::Vec3 p1,
        Math::Vec3 p2,
        float radius
    );

    /**
     * @brief Handles collision response with an infinite plane.
     *
     * @param cloth The cloth component to check for collisions.
     * @param plane Plane equation (a, b, c, d) where ax + by + cz + d = 0.
     *              The normal is (a, b, c) and should be normalized.
     */
    static void HandlePlaneCollision(ClothComponent& cloth, Math::Vec4 plane);

    // =========================================================================
    // Geometry Utilities
    // =========================================================================

    /**
     * @brief Recomputes vertex normals for the cloth mesh.
     *
     * Calculates smooth vertex normals by averaging adjacent face normals.
     * Required for proper lighting and wind calculations.
     *
     * @param cloth The cloth component whose normals should be updated.
     */
    static void ComputeNormals(ClothComponent& cloth);

    /**
     * @brief Computes the normal vector of a triangle.
     *
     * @param a First vertex of the triangle.
     * @param b Second vertex of the triangle.
     * @param c Third vertex of the triangle.
     * @return The normalized face normal (counter-clockwise winding).
     */
    [[nodiscard]] static Math::Vec3 GetTriangleNormal(
        Math::Vec3 a,
        Math::Vec3 b,
        Math::Vec3 c
    );

    // =========================================================================
    // Diagnostics
    // =========================================================================

    /**
     * @brief Calculates the total mechanical energy of the cloth system.
     *
     * Computes the sum of kinetic energy and potential energy (gravitational
     * and elastic). Useful for debugging and verifying energy conservation.
     *
     * @param cloth The cloth component to analyze.
     * @return Total mechanical energy in joules.
     */
    [[nodiscard]] static float CalculateTotalEnergy(const ClothComponent& cloth);
};

} // namespace Physics
} // namespace Core
