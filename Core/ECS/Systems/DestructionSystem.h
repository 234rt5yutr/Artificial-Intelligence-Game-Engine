#pragma once

/**
 * @file DestructionSystem.h
 * @brief ECS system for processing destructible mesh fracturing and debris management.
 *
 * The DestructionSystem handles the complete lifecycle of destructible entities,
 * from damage accumulation and fracture triggering to debris spawning and
 * regeneration. It integrates with the physics world for impact detection and
 * the debris manager for efficient debris lifecycle management.
 *
 * Key responsibilities:
 * - Processing destruction triggers from damage or impact events
 * - Executing Voronoi fracturing on destructible meshes
 * - Spawning physics-enabled debris from fracture results
 * - Managing chain reaction destruction for explosive scenarios
 * - Handling regeneration of respawnable destructibles
 *
 * ## Performance Considerations
 *
 * The system limits fractures per frame via SetMaxFracturesPerFrame() to prevent
 * frame hitches during large-scale destruction events. Pending fractures are
 * queued and processed over multiple frames as capacity allows.
 *
 * ## Usage Example
 *
 * @code
 * // Create and configure the destruction system
 * DestructionSystem destructionSystem(physicsWorld, debrisManager);
 * destructionSystem.SetChainReactionEnabled(true);
 * destructionSystem.SetMaxFracturesPerFrame(4);
 *
 * // In game loop
 * destructionSystem.Update(scene, deltaTime);
 *
 * // Manual destruction trigger
 * destructionSystem.ApplyImpactDamage(
 *     scene, targetEntity, 1000.0f,
 *     impactPoint, impactDirection
 * );
 *
 * // Explosive chain reaction
 * destructionSystem.TriggerChainReaction(
 *     scene, explosionCenter, 10.0f, 5000.0f
 * );
 * @endcode
 *
 * @see Core::ECS::DestructibleMeshComponent for per-entity destruction settings
 * @see Core::Physics::VoronoiFracturer for the fracturing algorithm
 * @see Core::Physics::DebrisManager for debris lifecycle management
 */

#include "Core/ECS/Components/DestructibleMeshComponent.h"
#include "Core/Physics/Destruction/VoronoiFracturer.h"
#include "Core/Math/Math.h"

#include <entt/entity/entity.hpp>

#include <cstdint>
#include <queue>
#include <vector>

// Forward declarations
namespace Core {

class Scene;

namespace Physics {
    class PhysicsWorld;
    class DebrisManager;
} // namespace Physics

} // namespace Core

namespace Core {
namespace ECS {

/**
 * @brief ECS system managing destructible mesh fracturing and debris spawning.
 *
 * DestructionSystem processes entities with DestructibleMeshComponent, handling
 * destruction triggers, fracturing, and debris management. The system integrates
 * tightly with the physics world for impact-based destruction and chain reactions.
 *
 * ## System Architecture
 *
 * The destruction pipeline follows these stages:
 *
 * 1. **Trigger Detection**: Scan for entities with DestructionTriggered flag set
 * 2. **Queue Management**: Add triggered entities to the pending fracture queue
 * 3. **Fracture Execution**: Process up to MaxFracturesPerFrame pending fractures
 * 4. **Debris Spawning**: Create physics-enabled debris from fracture results
 * 5. **Regeneration**: Update regeneration timers and restore destructibles
 *
 * ## Thread Safety
 *
 * This system is NOT thread-safe. All operations should be performed on the
 * main thread. The system may be parallelized internally for fracture
 * computation in future versions.
 *
 * @note Non-copyable and non-movable to ensure single ownership of the
 *       destruction pipeline and prevent accidental reference invalidation.
 */
class DestructionSystem
{
public:
    // =========================================================================
    // Constructors and Destructor
    // =========================================================================

    /**
     * @brief Constructs a DestructionSystem with required dependencies.
     *
     * Initializes the system with references to the physics world and debris
     * manager. These references must remain valid for the lifetime of the
     * DestructionSystem.
     *
     * @param physicsWorld  Reference to the physics simulation world.
     * @param debrisManager Reference to the debris lifecycle manager.
     *
     * @note Both parameters are stored as references; ensure their lifetime
     *       exceeds that of this DestructionSystem instance.
     */
    DestructionSystem(Physics::PhysicsWorld& physicsWorld,
                      Physics::DebrisManager& debrisManager);

    /**
     * @brief Default destructor.
     */
    ~DestructionSystem() = default;

    // =========================================================================
    // Non-Copyable, Non-Movable
    // =========================================================================

    DestructionSystem(const DestructionSystem&) = delete;
    DestructionSystem& operator=(const DestructionSystem&) = delete;
    DestructionSystem(DestructionSystem&&) = delete;
    DestructionSystem& operator=(DestructionSystem&&) = delete;

    // =========================================================================
    // Core Update Loop
    // =========================================================================

    /**
     * @brief Main update function, called once per frame.
     *
     * Executes the complete destruction pipeline:
     * 1. Process destruction triggers to queue pending fractures
     * 2. Execute up to MaxFracturesPerFrame pending fractures
     * 3. Update regeneration timers for destroyed entities
     *
     * @param scene     The scene containing destructible entities.
     * @param deltaTime Time elapsed since the last frame in seconds.
     *
     * @note This should be called after physics simulation but before rendering.
     * @note Fractures exceeding the per-frame limit are queued for later frames.
     */
    void Update(Scene& scene, float deltaTime);

    // =========================================================================
    // Destruction Processing
    // =========================================================================

    /**
     * @brief Scans for and processes newly triggered destructions.
     *
     * Iterates through all entities with DestructibleMeshComponent, checking
     * for the DestructionTriggered flag. Triggered entities are transitioned
     * to the Fracturing state and added to the pending fracture queue.
     *
     * @param scene The scene containing destructible entities.
     *
     * @note Called internally by Update(), but may be called manually for
     *       immediate trigger processing outside the normal update cycle.
     */
    void ProcessDestructionTriggers(Scene& scene);

    /**
     * @brief Fractures a specific entity and spawns debris.
     *
     * Immediately fractures the target entity using Voronoi tessellation,
     * bypassing the queue system. The original entity's mesh is hidden or
     * destroyed, and debris pieces are spawned with physics bodies.
     *
     * @param scene  The scene containing the entity.
     * @param entity The entity to fracture.
     *
     * @pre Entity must have DestructibleMeshComponent attached.
     * @pre Entity should be in Intact or Fracturing state.
     *
     * @note This bypasses MaxFracturesPerFrame and executes immediately.
     * @note Use for scripted destruction where immediate response is required.
     */
    void FractureEntity(Scene& scene, entt::entity entity);

    /**
     * @brief Applies impact damage to a destructible entity.
     *
     * Calculates damage based on the impact force and applies it to the
     * entity's DestructibleMeshComponent. If damage exceeds the threshold
     * or force exceeds ImpactForceThreshold, destruction is triggered.
     *
     * @param scene     The scene containing the entity.
     * @param entity    The entity to damage.
     * @param force     Magnitude of the impact force in Newtons.
     * @param point     World-space position of the impact.
     * @param direction Normalized direction of the incoming impact.
     *
     * @note The direction parameter affects fracture pattern when using
     *       radial fracturing modes.
     * @note If chain reactions are enabled and destruction triggers,
     *       nearby entities may also be affected.
     */
    void ApplyImpactDamage(Scene& scene,
                           entt::entity entity,
                           float force,
                           const Math::Vec3& point,
                           const Math::Vec3& direction);

    /**
     * @brief Triggers destruction of all destructibles within a radius.
     *
     * Simulates an explosion or shockwave by finding all destructible
     * entities within the specified radius and applying force-based
     * damage. Force attenuates with distance from the center.
     *
     * @param scene  The scene containing destructible entities.
     * @param center World-space center of the chain reaction.
     * @param radius Maximum distance from center to affect entities.
     * @param force  Peak force at the center point in Newtons.
     *
     * @note Force falloff follows inverse-square law: F = force / (1 + d²)
     *       where d is the distance from center.
     * @note Only affects entities with DestructibleMeshComponent in Intact state.
     * @note If ChainReactionEnabled is false, this method has no effect.
     */
    void TriggerChainReaction(Scene& scene,
                              const Math::Vec3& center,
                              float radius,
                              float force);

    // =========================================================================
    // Regeneration
    // =========================================================================

    /**
     * @brief Updates regeneration timers and restores eligible destructibles.
     *
     * Processes all destructible entities in the Destroyed or Regenerating
     * state, updating their regeneration timers. When regeneration completes,
     * entities are restored to their intact state and their debris is cleaned up.
     *
     * @param scene     The scene containing destructible entities.
     * @param deltaTime Time elapsed since the last frame in seconds.
     *
     * @note Only entities with CanRegenerate == true are processed.
     * @note Called internally by Update(), but may be called separately
     *       if fine-grained control over the update order is needed.
     */
    void ProcessRegeneration(Scene& scene, float deltaTime);

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Returns the total count of destructions processed.
     *
     * Tracks the cumulative number of entities that have been fractured
     * since the system was created or last reset.
     *
     * @return Total destruction count.
     */
    [[nodiscard]] uint32_t GetDestructionCount() const noexcept
    {
        return m_DestructionCount;
    }

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Enables or disables chain reaction destruction.
     *
     * When enabled, destruction of one entity can trigger destruction of
     * nearby entities through force propagation. This creates realistic
     * cascading destruction effects.
     *
     * @param enabled True to enable chain reactions, false to disable.
     *
     * @note Chain reactions can significantly impact performance during
     *       large-scale destruction; consider disabling for dense scenes.
     */
    void SetChainReactionEnabled(bool enabled) noexcept
    {
        m_ChainReactionEnabled = enabled;
    }

    /**
     * @brief Sets the maximum number of fractures processed per frame.
     *
     * Limits the number of pending fractures executed each frame to prevent
     * frame rate spikes during large-scale destruction events. Excess
     * fractures are queued and processed in subsequent frames.
     *
     * @param maxFractures Maximum fractures per frame (minimum: 1).
     *
     * @note Lower values provide smoother frame times but may delay
     *       destruction visual feedback in intense scenarios.
     * @note Values of 0 are clamped to 1 to ensure forward progress.
     */
    void SetMaxFracturesPerFrame(uint32_t maxFractures) noexcept
    {
        m_MaxFracturesPerFrame = (maxFractures > 0) ? maxFractures : 1;
    }

private:
    // =========================================================================
    // Private Helper Methods
    // =========================================================================

    /**
     * @brief Executes the Voronoi fracture algorithm on a destructible mesh.
     *
     * Performs the actual mesh fracturing using the component's configured
     * fracture settings. Returns a FractureResult containing all generated
     * debris cells.
     *
     * @param scene          The scene for accessing related components.
     * @param destructible   The component containing fracture configuration.
     * @param worldTransform World transform matrix of the entity being fractured.
     * @return FractureResult containing generated cells and status.
     *
     * @note Uses pre-computed fracture if available in the component.
     * @note May return an empty result if the mesh is invalid or too small.
     */
    [[nodiscard]] Physics::FractureResult PerformFracture(
        Scene& scene,
        DestructibleMeshComponent& destructible,
        const Math::Mat4& worldTransform);

    /**
     * @brief Spawns debris entities from a fracture result.
     *
     * Creates new entities for each cell in the fracture result, setting up
     * mesh components, physics bodies, and debris tracking. Initial velocities
     * are computed based on the impact parameters and debris settings.
     *
     * @param scene          The scene to spawn debris entities into.
     * @param fractureResult The fracture result containing cell geometry.
     * @param worldTransform World transform to apply to debris positions.
     * @param velocity       Base velocity to apply to spawned debris.
     *
     * @note Debris entities are registered with the DebrisManager for
     *       lifecycle tracking and cleanup.
     */
    void SpawnDebrisFromFracture(
        Scene& scene,
        Physics::FractureResult& fractureResult,
        const Math::Mat4& worldTransform,
        const Math::Vec3& velocity);

    /**
     * @brief Finds all destructible entities within a radius.
     *
     * Spatial query for destructible entities near a world-space position.
     * Used for chain reaction detection and area-of-effect destruction.
     *
     * @param scene  The scene to search.
     * @param center World-space center of the search sphere.
     * @param radius Search radius in world units.
     * @return Vector of entity handles for destructibles within range.
     *
     * @note Only returns entities with DestructibleMeshComponent.
     * @note Only returns entities in Intact state (not already destroyed).
     */
    [[nodiscard]] std::vector<entt::entity> FindNearbyDestructibles(
        Scene& scene,
        const Math::Vec3& center,
        float radius);

    // =========================================================================
    // Member Variables
    // =========================================================================

    /** @brief Reference to the physics simulation world. */
    Physics::PhysicsWorld& m_PhysicsWorld;

    /** @brief Reference to the debris lifecycle manager. */
    Physics::DebrisManager& m_DebrisManager;

    /** @brief Whether chain reactions propagate between destructibles. */
    bool m_ChainReactionEnabled{ true };

    /** @brief Maximum number of fractures to process per frame. */
    uint32_t m_MaxFracturesPerFrame{ 4 };

    /** @brief Queue of entities awaiting fracture processing. */
    std::queue<entt::entity> m_PendingFractures;

    /** @brief Cumulative count of processed destructions. */
    uint32_t m_DestructionCount{ 0 };
};

} // namespace ECS
} // namespace Core
