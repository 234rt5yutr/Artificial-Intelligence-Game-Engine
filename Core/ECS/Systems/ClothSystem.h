#pragma once

// ClothSystem.h
// ECS system for cloth physics simulation using position-based dynamics
// Handles constraint solving, wind forces, collision detection, and renderer synchronization

#include <entt/entt.hpp>

#include <cstdint>
#include <vector>

namespace Core {

// Forward declarations
namespace Math {
    struct Vec3;
} // namespace Math

namespace ECS {

    // Forward declarations
    struct ClothComponent;
    struct ClothCollider;

    // =========================================================================
    // ClothSystem Configuration
    // =========================================================================

    /**
     * @brief Configuration constants for the cloth simulation system.
     */
    namespace ClothSystemConfig {
        /** Maximum number of cloth entities that can be simulated concurrently. */
        constexpr size_t MaxClothEntities = 64;

        /** Fixed timestep for physics integration (120 Hz for stable simulation). */
        constexpr float FixedTimeStep = 1.0f / 120.0f;

        /** Maximum number of substeps per frame to prevent spiral of death. */
        constexpr uint32_t MaxSubSteps = 8;
    } // namespace ClothSystemConfig

    // =========================================================================
    // ClothSystem
    // =========================================================================

    /**
     * @brief ECS system responsible for cloth physics simulation.
     * 
     * The ClothSystem implements position-based dynamics (PBD) for realistic
     * cloth simulation with the following features:
     *   - Fixed timestep integration with accumulator for frame-rate independence
     *   - Distance and bending constraint solving with Gauss-Seidel iteration
     *   - Environmental wind force application with turbulence
     *   - Collision detection against static and dynamic colliders
     *   - Vertex buffer synchronization with the rendering system
     * 
     * The system uses a semi-fixed timestep approach where physics updates
     * occur at a fixed rate (120 Hz) while allowing multiple substeps per
     * frame up to MaxSubSteps to handle variable frame rates without
     * accumulating excessive simulation debt.
     * 
     * Usage:
     * @code
     *     ClothSystem clothSystem(registry);
     *     clothSystem.Initialize();
     *     
     *     // Main game loop
     *     clothSystem.Update(deltaTime);
     *     
     *     // Cleanup on shutdown
     *     clothSystem.Shutdown();
     * @endcode
     * 
     * @note This system is non-copyable and non-movable to ensure single
     *       ownership of registry reference and simulation state.
     * 
     * @see ClothComponent for per-entity cloth configuration and state.
     */
    class ClothSystem
    {
    public:
        // ---------------------------------------------------------------------
        // Constructors & Destructor
        // ---------------------------------------------------------------------

        /**
         * @brief Constructs the cloth system with a reference to the ECS registry.
         * 
         * @param registry Reference to the EnTT registry for entity/component access.
         */
        explicit ClothSystem(entt::registry& registry);

        /**
         * @brief Destructor - ensures proper cleanup of simulation resources.
         */
        ~ClothSystem();

        // Non-copyable
        ClothSystem(const ClothSystem&) = delete;
        ClothSystem& operator=(const ClothSystem&) = delete;

        // Non-movable
        ClothSystem(ClothSystem&&) = delete;
        ClothSystem& operator=(ClothSystem&&) = delete;

        // ---------------------------------------------------------------------
        // Lifecycle Methods
        // ---------------------------------------------------------------------

        /**
         * @brief Initializes the cloth simulation system.
         * 
         * Performs initial setup including:
         *   - Allocating simulation buffers
         *   - Registering component callbacks with the registry
         *   - Loading default collider configurations
         *   - Initializing GPU resources for vertex buffer updates
         * 
         * @note Must be called before the first Update() call.
         */
        void Initialize();

        /**
         * @brief Main update loop for the cloth system.
         * 
         * Processes all entities with ClothComponent using a fixed timestep:
         *   1. Accumulates delta time into the time accumulator
         *   2. Executes simulation substeps at FixedTimeStep intervals
         *   3. Per substep: applies forces, solves constraints, handles collisions
         *   4. Updates vertex buffers and synchronizes with renderer
         * 
         * @param deltaTime Time elapsed since last frame in seconds.
         * 
         * @note Substeps are clamped to MaxSubSteps to prevent spiral of death
         *       when frame rate drops significantly below simulation rate.
         */
        void Update(float deltaTime);

        /**
         * @brief Shuts down the cloth simulation system and releases resources.
         * 
         * Performs cleanup including:
         *   - Releasing GPU vertex buffer resources
         *   - Clearing static collider data
         *   - Unregistering component callbacks
         *   - Resetting internal state
         * 
         * @note Safe to call multiple times; subsequent calls are no-ops.
         */
        void Shutdown();

    private:
        // ---------------------------------------------------------------------
        // Simulation Methods
        // ---------------------------------------------------------------------

        /**
         * @brief Updates cloth simulation for a single entity.
         * 
         * Executes the position-based dynamics solver:
         *   1. Integrates external forces (gravity, wind, damping)
         *   2. Predicts particle positions using Verlet integration
         *   3. Iteratively solves distance and bending constraints
         *   4. Updates particle velocities from position changes
         * 
         * @param entity The entity being simulated.
         * @param cloth Reference to the entity's cloth component.
         * @param deltaTime Fixed simulation timestep in seconds.
         */
        void UpdateClothSimulation(entt::entity entity, 
                                   ClothComponent& cloth, 
                                   float deltaTime);

        /**
         * @brief Computes wind force at a given world position.
         * 
         * Samples the wind field including:
         *   - Base wind direction and strength
         *   - Turbulence and gust modulation
         *   - Spatial variation for natural-looking motion
         * 
         * @param position World-space position to sample wind at.
         * @return Wind force vector in world space (Newtons).
         */
        [[nodiscard]] Math::Vec3 GatherWindForces(const Math::Vec3& position);

        /**
         * @brief Updates GPU vertex buffer with new cloth positions.
         * 
         * Uploads particle positions and recalculated normals to the
         * GPU vertex buffer for rendering. Uses mapped buffer writes
         * or staging buffers depending on hardware capabilities.
         * 
         * @param entity The entity whose vertex buffer to update.
         * @param cloth Reference to the entity's cloth component.
         */
        void UpdateVertexBuffer(entt::entity entity, ClothComponent& cloth);

        /**
         * @brief Detects and resolves collisions between cloth and colliders.
         * 
         * Performs collision detection against:
         *   - Static colliders registered with the system
         *   - Dynamic colliders attached to other entities
         *   - Self-collision (if enabled on the cloth component)
         * 
         * Resolution uses position projection with friction for stable contacts.
         * 
         * @param cloth Reference to the cloth component to process collisions for.
         */
        void DetectAndResolveCollisions(ClothComponent& cloth);

        /**
         * @brief Synchronizes cloth state with the rendering system.
         * 
         * Updates renderer-side state including:
         *   - Bounding box for frustum culling
         *   - Material property updates
         *   - LOD selection based on screen coverage
         * 
         * @param entity The entity to synchronize.
         * @param cloth Reference to the entity's cloth component.
         */
        void SyncWithRenderer(entt::entity entity, ClothComponent& cloth);

        // ---------------------------------------------------------------------
        // Member Variables
        // ---------------------------------------------------------------------

        /** Reference to the ECS registry for entity/component access. */
        entt::registry& m_Registry;

        /** Accumulated time for fixed timestep integration. */
        float m_TimeAccumulator = 0.0f;

        /** Static colliders for cloth collision detection. */
        std::vector<ClothCollider> m_StaticColliders;
    };

} // namespace ECS
} // namespace Core
