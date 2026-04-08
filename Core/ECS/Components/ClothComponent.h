#pragma once

/**
 * @file ClothComponent.h
 * @brief ECS component for cloth simulation using Position-Based Dynamics (PBD)
 * 
 * Implements a particle-based cloth simulation with configurable constraints
 * for stretch, shear, and bend behavior. Supports wind forces, collision detection,
 * and various preset configurations (curtain, flag, cape).
 */

#include "Core/Math/Math.h"
#include <entt/entt.hpp>
#include <vector>
#include <cstdint>

namespace Core {
namespace ECS {

    //=========================================================================
    // Cloth Constraint Types
    //=========================================================================

    /**
     * @brief Type of constraint between cloth vertices
     */
    enum class ClothConstraintType : uint8_t {
        Stretch,    // Distance constraint between adjacent vertices (horizontal/vertical)
        Shear,      // Diagonal constraint for shear resistance
        Bend        // Constraint between vertices separated by one vertex (bending resistance)
    };

    //=========================================================================
    // Cloth Constraint
    //=========================================================================

    /**
     * @brief Represents a distance constraint between two cloth vertices
     * 
     * Constraints are used by the Position-Based Dynamics solver to maintain
     * structural integrity of the cloth simulation.
     */
    struct ClothConstraint {
        uint32_t IndexA = 0;            // First vertex index
        uint32_t IndexB = 0;            // Second vertex index
        float RestLength = 0.0f;        // Target distance between vertices
        float Stiffness = 1.0f;         // Constraint stiffness (0-1, affects convergence)

        ClothConstraint() = default;

        ClothConstraint(uint32_t a, uint32_t b, float restLen, float stiffness = 1.0f)
            : IndexA(a), IndexB(b), RestLength(restLen), Stiffness(stiffness) {}
    };

    //=========================================================================
    // Cloth Component
    //=========================================================================

    /**
     * @brief Component for cloth simulation using Position-Based Dynamics
     * 
     * The cloth is represented as a grid of particles connected by constraints.
     * The solver iteratively satisfies constraints to produce realistic cloth behavior.
     * 
     * Usage:
     * @code
     * auto& cloth = entity.AddComponent<ClothComponent>();
     * cloth.Width = 2.0f;
     * cloth.Height = 3.0f;
     * cloth.WidthSegments = 20;
     * cloth.HeightSegments = 30;
     * cloth.Initialize();
     * cloth.PinTopEdge();
     * @endcode
     */
    struct ClothComponent {
        //=====================================================================
        // Grid Configuration
        //=====================================================================

        uint32_t WidthSegments = 16;        // Number of segments along width
        uint32_t HeightSegments = 16;       // Number of segments along height
        float Width = 1.0f;                 // Physical width in world units
        float Height = 1.0f;                // Physical height in world units

        //=====================================================================
        // Material Properties
        //=====================================================================

        float StretchStiffness = 0.9f;      // Resistance to stretching (0-1)
        float ShearStiffness = 0.9f;        // Resistance to shearing (0-1)
        float BendStiffness = 0.5f;         // Resistance to bending (0-1)
        float Damping = 0.98f;              // Velocity damping factor (0-1)

        //=====================================================================
        // Mass Properties
        //=====================================================================

        float TotalMass = 1.0f;             // Total mass of the cloth in kg

        /**
         * @brief Computes mass per vertex based on total mass and vertex count
         * @return Mass assigned to each vertex
         */
        float GetMassPerVertex() const {
            uint32_t vertexCount = (WidthSegments + 1) * (HeightSegments + 1);
            return vertexCount > 0 ? TotalMass / static_cast<float>(vertexCount) : 0.0f;
        }

        //=====================================================================
        // Solver Configuration
        //=====================================================================

        uint32_t SolverIterations = 4;      // Constraint solver iterations per frame
        uint32_t CollisionIterations = 2;   // Collision resolution iterations per frame
        float TimeSubstep = 1.0f / 120.0f;  // Physics substep time (seconds)

        //=====================================================================
        // External Forces
        //=====================================================================

        Math::Vec3 Gravity{ 0.0f, -9.81f, 0.0f };   // Gravity acceleration
        Math::Vec3 WindVelocity{ 0.0f, 0.0f, 0.0f }; // Wind velocity vector
        float WindDrag = 0.1f;              // Drag coefficient for wind
        float WindLift = 0.1f;              // Lift coefficient for wind

        //=====================================================================
        // Collision Settings
        //=====================================================================

        float CollisionRadius = 0.02f;                          // Radius for vertex-collider detection
        std::vector<entt::entity> CollisionBodies;              // Entities to collide with
        bool SelfCollision = false;                             // Enable self-collision detection

        //=====================================================================
        // Runtime State (Particle Data)
        //=====================================================================

        std::vector<Math::Vec3> Positions;          // Current vertex positions
        std::vector<Math::Vec3> PreviousPositions;  // Previous frame positions (for Verlet)
        std::vector<Math::Vec3> Velocities;         // Vertex velocities
        std::vector<bool> Pinned;                   // Whether each vertex is pinned (immovable)

        //=====================================================================
        // Constraints
        //=====================================================================

        std::vector<ClothConstraint> StretchConstraints;    // Horizontal/vertical constraints
        std::vector<ClothConstraint> ShearConstraints;      // Diagonal constraints
        std::vector<ClothConstraint> BendConstraints;       // Bending constraints

        //=====================================================================
        // Output (for rendering)
        //=====================================================================

        std::vector<Math::Vec3> Normals;    // Computed vertex normals
        bool NeedsMeshUpdate = true;        // Flag indicating mesh needs GPU update

        //=====================================================================
        // State Flags
        //=====================================================================

        bool IsInitialized = false;         // Whether Initialize() has been called
        bool IsPaused = false;              // Pause simulation

        //=====================================================================
        // Methods
        //=====================================================================

        ClothComponent() = default;

        /**
         * @brief Initialize the cloth grid, particles, and constraints
         * 
         * Must be called after setting grid configuration and before simulation.
         * Creates vertex grid and generates all constraint types.
         */
        void Initialize() {
            if (IsInitialized) return;

            uint32_t cols = WidthSegments + 1;
            uint32_t rows = HeightSegments + 1;
            uint32_t vertexCount = cols * rows;

            // Allocate particle arrays
            Positions.resize(vertexCount);
            PreviousPositions.resize(vertexCount);
            Velocities.resize(vertexCount, Math::Vec3(0.0f));
            Pinned.resize(vertexCount, false);
            Normals.resize(vertexCount, Math::Vec3(0.0f, 0.0f, 1.0f));

            // Initialize vertex positions in a grid (XY plane, facing +Z)
            float dx = Width / static_cast<float>(WidthSegments);
            float dy = Height / static_cast<float>(HeightSegments);

            for (uint32_t y = 0; y < rows; ++y) {
                for (uint32_t x = 0; x < cols; ++x) {
                    uint32_t idx = y * cols + x;
                    Positions[idx] = Math::Vec3(
                        x * dx - Width * 0.5f,      // Center horizontally
                        -y * dy + Height * 0.5f,    // Y goes down, center vertically
                        0.0f
                    );
                    PreviousPositions[idx] = Positions[idx];
                }
            }

            // Generate all constraints
            GenerateConstraints();

            IsInitialized = true;
            NeedsMeshUpdate = true;
        }

        /**
         * @brief Pin a vertex at grid position (x, y) to prevent movement
         * @param x Column index (0 to WidthSegments)
         * @param y Row index (0 to HeightSegments)
         */
        void PinVertex(uint32_t x, uint32_t y) {
            uint32_t cols = WidthSegments + 1;
            if (x <= WidthSegments && y <= HeightSegments) {
                Pinned[y * cols + x] = true;
            }
        }

        /**
         * @brief Unpin a vertex at grid position (x, y) to allow movement
         * @param x Column index (0 to WidthSegments)
         * @param y Row index (0 to HeightSegments)
         */
        void UnpinVertex(uint32_t x, uint32_t y) {
            uint32_t cols = WidthSegments + 1;
            if (x <= WidthSegments && y <= HeightSegments) {
                Pinned[y * cols + x] = false;
            }
        }

        /**
         * @brief Pin all vertices along the top edge
         */
        void PinTopEdge() {
            uint32_t cols = WidthSegments + 1;
            for (uint32_t x = 0; x < cols; ++x) {
                Pinned[x] = true;  // Top row is index 0 to cols-1
            }
        }

        /**
         * @brief Pin only the four corners of the cloth
         */
        void PinCorners() {
            uint32_t cols = WidthSegments + 1;
            uint32_t rows = HeightSegments + 1;

            Pinned[0] = true;                               // Top-left
            Pinned[cols - 1] = true;                        // Top-right
            Pinned[(rows - 1) * cols] = true;               // Bottom-left
            Pinned[(rows - 1) * cols + cols - 1] = true;    // Bottom-right
        }

        //=====================================================================
        // Factory Methods
        //=====================================================================

        /**
         * @brief Create a curtain configuration (pinned at top, no wind)
         * @param width Physical width
         * @param height Physical height
         * @param segments Grid resolution
         * @return Configured ClothComponent
         */
        static ClothComponent CreateCurtain(float width = 2.0f, float height = 3.0f,
                                            uint32_t segments = 24) {
            ClothComponent cloth;
            cloth.Width = width;
            cloth.Height = height;
            cloth.WidthSegments = segments;
            cloth.HeightSegments = static_cast<uint32_t>(segments * (height / width));
            cloth.TotalMass = width * height * 0.5f;    // ~0.5 kg per square meter
            cloth.StretchStiffness = 0.95f;
            cloth.ShearStiffness = 0.9f;
            cloth.BendStiffness = 0.3f;                 // Curtains are fairly flexible
            cloth.Damping = 0.98f;
            cloth.Initialize();
            cloth.PinTopEdge();
            return cloth;
        }

        /**
         * @brief Create a flag configuration (pinned on left edge, wind-ready)
         * @param width Physical width
         * @param height Physical height
         * @param segments Grid resolution
         * @return Configured ClothComponent
         */
        static ClothComponent CreateFlag(float width = 2.0f, float height = 1.2f,
                                         uint32_t segments = 20) {
            ClothComponent cloth;
            cloth.Width = width;
            cloth.Height = height;
            cloth.WidthSegments = segments;
            cloth.HeightSegments = static_cast<uint32_t>(segments * (height / width));
            cloth.TotalMass = width * height * 0.2f;    // Lighter material
            cloth.StretchStiffness = 0.98f;             // Flags don't stretch much
            cloth.ShearStiffness = 0.95f;
            cloth.BendStiffness = 0.1f;                 // Very flexible for flapping
            cloth.WindDrag = 0.15f;
            cloth.WindLift = 0.12f;
            cloth.Damping = 0.995f;                     // Less damping for more movement
            cloth.Initialize();

            // Pin left edge (the pole side)
            uint32_t cols = cloth.WidthSegments + 1;
            uint32_t rows = cloth.HeightSegments + 1;
            for (uint32_t y = 0; y < rows; ++y) {
                cloth.Pinned[y * cols] = true;
            }

            return cloth;
        }

        /**
         * @brief Create a cape configuration (pinned at shoulders, character-ready)
         * @param width Physical width (shoulder span)
         * @param height Physical height (cape length)
         * @param segments Grid resolution
         * @return Configured ClothComponent
         */
        static ClothComponent CreateCape(float width = 1.0f, float height = 1.5f,
                                         uint32_t segments = 16) {
            ClothComponent cloth;
            cloth.Width = width;
            cloth.Height = height;
            cloth.WidthSegments = segments;
            cloth.HeightSegments = static_cast<uint32_t>(segments * (height / width));
            cloth.TotalMass = width * height * 0.4f;
            cloth.StretchStiffness = 0.92f;
            cloth.ShearStiffness = 0.88f;
            cloth.BendStiffness = 0.4f;
            cloth.SelfCollision = true;                 // Capes can fold on themselves
            cloth.CollisionRadius = 0.03f;
            cloth.Damping = 0.97f;
            cloth.SolverIterations = 6;                 // More iterations for stability
            cloth.Initialize();

            // Pin shoulder attachment points (top corners with slight inset)
            uint32_t cols = cloth.WidthSegments + 1;
            uint32_t inset = cols / 6;  // Inset from edges

            cloth.PinVertex(inset, 0);
            cloth.PinVertex(inset + 1, 0);
            cloth.PinVertex(cols - 1 - inset, 0);
            cloth.PinVertex(cols - 2 - inset, 0);

            return cloth;
        }

    private:
        //=====================================================================
        // Internal Methods
        //=====================================================================

        /**
         * @brief Generate all constraint types based on grid configuration
         */
        void GenerateConstraints() {
            StretchConstraints.clear();
            ShearConstraints.clear();
            BendConstraints.clear();

            uint32_t cols = WidthSegments + 1;
            uint32_t rows = HeightSegments + 1;

            float dx = Width / static_cast<float>(WidthSegments);
            float dy = Height / static_cast<float>(HeightSegments);
            float diag = std::sqrt(dx * dx + dy * dy);

            // Generate constraints for each vertex
            for (uint32_t y = 0; y < rows; ++y) {
                for (uint32_t x = 0; x < cols; ++x) {
                    uint32_t idx = y * cols + x;

                    // Stretch constraints (horizontal and vertical neighbors)
                    // Right neighbor
                    if (x < WidthSegments) {
                        StretchConstraints.emplace_back(idx, idx + 1, dx, StretchStiffness);
                    }
                    // Bottom neighbor
                    if (y < HeightSegments) {
                        StretchConstraints.emplace_back(idx, idx + cols, dy, StretchStiffness);
                    }

                    // Shear constraints (diagonal neighbors)
                    // Bottom-right diagonal
                    if (x < WidthSegments && y < HeightSegments) {
                        ShearConstraints.emplace_back(idx, idx + cols + 1, diag, ShearStiffness);
                    }
                    // Bottom-left diagonal
                    if (x > 0 && y < HeightSegments) {
                        ShearConstraints.emplace_back(idx, idx + cols - 1, diag, ShearStiffness);
                    }

                    // Bend constraints (skip one vertex)
                    // Two vertices to the right
                    if (x < WidthSegments - 1) {
                        BendConstraints.emplace_back(idx, idx + 2, dx * 2.0f, BendStiffness);
                    }
                    // Two vertices down
                    if (y < HeightSegments - 1) {
                        BendConstraints.emplace_back(idx, idx + cols * 2, dy * 2.0f, BendStiffness);
                    }
                }
            }
        }
    };

} // namespace ECS
} // namespace Core