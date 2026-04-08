#pragma once

/**
 * @file DebrisManager.h
 * @brief Manages debris lifecycle, pooling, and cleanup for physics destruction systems.
 * 
 * The DebrisManager is responsible for spawning debris fragments from fracture
 * operations, managing their lifetime, and efficiently pooling entities for reuse.
 * It implements object pooling to minimize allocation overhead during destruction
 * events and provides automatic cleanup of expired debris.
 * 
 * Key responsibilities:
 * - Spawning debris from FractureResult data with physics properties
 * - Managing debris lifetime with configurable fade-out effects
 * - Object pooling for efficient entity reuse
 * - Enforcing maximum concurrent debris limits
 * - Automatic cleanup of expired debris entities
 * 
 * @note This class is implemented as a singleton to ensure centralized debris
 *       management and consistent pooling behavior across the application.
 * 
 * @see Core::Physics::VoronoiFracturer for generating fracture data
 * @see Core::Physics::PhysicsWorld for physics simulation
 */

#include "Core/Physics/Destruction/VoronoiFracturer.h"
#include "Core/Math/Math.h"

#include <entt/entt.hpp>

#include <cstdint>
#include <optional>
#include <queue>
#include <vector>

namespace Core {

// Forward declarations
class Scene;

namespace Physics {

// Forward declarations
class PhysicsWorld;

/**
 * @brief Configuration settings for spawning debris pieces.
 * 
 * Controls the physical and visual properties of spawned debris, including
 * lifetime, physics behavior, and fade-out effects.
 */
struct DebrisSpawnSettings
{
    /** 
     * @brief Base lifetime in seconds before debris begins fading.
     * @note Actual lifetime is multiplied by GlobalLifetimeScale.
     */
    float BaseLifetime{ 5.0f };
    
    /**
     * @brief Random variance applied to base lifetime (0.0 to 1.0).
     * @note Final lifetime = BaseLifetime * (1.0 +/- LifetimeVariance * random)
     */
    float LifetimeVariance{ 0.2f };
    
    /**
     * @brief Duration in seconds for the fade-out effect before removal.
     * @note Set to 0.0 for instant removal without fading.
     */
    float FadeDuration{ 1.0f };
    
    /**
     * @brief Density used to calculate mass from volume (kg/m³).
     * @note Common values: concrete ~2400, wood ~600, steel ~7850
     */
    float Density{ 1000.0f };
    
    /**
     * @brief Coefficient of restitution (bounciness) for debris physics.
     * @note Range 0.0 (no bounce) to 1.0 (perfect bounce).
     */
    float Restitution{ 0.3f };
    
    /**
     * @brief Friction coefficient for debris physics interactions.
     * @note Range 0.0 (frictionless) to 1.0+ (high friction).
     */
    float Friction{ 0.5f };
    
    /**
     * @brief Linear velocity damping applied to debris over time.
     * @note Higher values cause debris to slow down faster.
     */
    float LinearDamping{ 0.1f };
    
    /**
     * @brief Angular velocity damping applied to debris over time.
     * @note Higher values cause debris to stop spinning faster.
     */
    float AngularDamping{ 0.1f };
    
    /**
     * @brief Whether to inherit velocity from the source object.
     * @note When true, adds source velocity to impact-based velocity.
     */
    bool InheritSourceVelocity{ true };
    
    /**
     * @brief Multiplier for velocity inherited from the source.
     * @note Only applies when InheritSourceVelocity is true.
     */
    float InheritedVelocityScale{ 0.5f };
    
    /**
     * @brief Random spread applied to debris ejection velocity.
     * @note Higher values create more chaotic debris patterns.
     */
    float VelocitySpread{ 0.3f };
    
    /**
     * @brief Minimum velocity magnitude for spawned debris.
     * @note Ensures debris has enough energy to move visibly.
     */
    float MinEjectionSpeed{ 1.0f };
    
    /**
     * @brief Maximum velocity magnitude for spawned debris.
     * @note Caps debris speed to prevent physics instability.
     */
    float MaxEjectionSpeed{ 20.0f };
    
    /**
     * @brief Whether debris should cast shadows.
     * @note Disabling can improve rendering performance.
     */
    bool CastShadows{ false };
    
    /**
     * @brief Physics layer/collision group for debris.
     * @note Use to control what debris collides with.
     */
    uint32_t CollisionLayer{ 0 };
    
    /**
     * @brief Physics collision mask for debris interactions.
     * @note Bitfield controlling which layers debris can collide with.
     */
    uint32_t CollisionMask{ 0xFFFFFFFF };
};

/**
 * @brief Internal tracking structure for active debris entities.
 * 
 * Maintains per-debris state required for lifetime management and
 * fade-out effects. Used internally by DebrisManager.
 */
struct DebrisEntry
{
    /** @brief The ECS entity handle for this debris piece. */
    entt::entity Entity{ entt::null };
    
    /** @brief Remaining lifetime in seconds before fade begins. */
    float RemainingLifetime{ 0.0f };
    
    /** @brief Current progress through the fade-out effect (0.0 to 1.0). */
    float FadeProgress{ 0.0f };
    
    /** @brief Whether this debris is currently in the fading state. */
    bool IsFading{ false };
};

/**
 * @brief Singleton manager for debris lifecycle, pooling, and cleanup.
 * 
 * DebrisManager provides centralized control over all debris entities in the
 * physics destruction system. It handles the complete lifecycle from spawning
 * to cleanup, with object pooling for performance optimization.
 * 
 * ## Features
 * 
 * - **Object Pooling**: Reuses debris entities to minimize allocation overhead
 * - **Lifetime Management**: Automatic tracking and cleanup of expired debris
 * - **Fade Effects**: Smooth visual fade-out before debris removal
 * - **Concurrent Limits**: Enforces maximum debris count for performance
 * - **Global Scaling**: Adjustable lifetime scaling for difficulty/performance
 * 
 * ## Usage Example
 * 
 * @code
 * // Get the singleton instance
 * auto& debrisManager = DebrisManager::Get();
 * 
 * // Configure limits
 * debrisManager.SetMaxConcurrentDebris(500);
 * debrisManager.SetGlobalLifetimeScale(0.5f);  // Half lifetime for performance
 * 
 * // Spawn debris from a fracture result
 * DebrisSpawnSettings settings;
 * settings.BaseLifetime = 3.0f;
 * settings.Density = 2400.0f;  // Concrete
 * 
 * auto debris = debrisManager.SpawnDebris(
 *     scene, physicsWorld, fractureResult,
 *     objectTransform, impactVelocity, settings
 * );
 * 
 * // Update in game loop
 * debrisManager.Update(deltaTime);
 * 
 * // Clear all debris on level transition
 * debrisManager.ClearAllDebris();
 * @endcode
 * 
 * ## Thread Safety
 * 
 * This class is NOT thread-safe. All operations should be performed on the
 * main thread or with external synchronization.
 * 
 * @note Maximum debris limit is enforced by removing oldest debris when exceeded.
 */
class DebrisManager
{
public:
    // =========================================================================
    // Singleton Access
    // =========================================================================
    
    /**
     * @brief Retrieves the singleton instance of the DebrisManager.
     * 
     * Creates the instance on first call using static initialization.
     * Thread-safe for the initial creation due to C++11 static initialization
     * guarantees, but subsequent usage is not thread-safe.
     * 
     * @return Reference to the global DebrisManager instance.
     */
    [[nodiscard]] static DebrisManager& Get();
    
    // Prevent copying and moving
    DebrisManager(const DebrisManager&) = delete;
    DebrisManager& operator=(const DebrisManager&) = delete;
    DebrisManager(DebrisManager&&) = delete;
    DebrisManager& operator=(DebrisManager&&) = delete;
    
    // =========================================================================
    // Debris Spawning
    // =========================================================================
    
    /**
     * @brief Spawns debris entities from a complete fracture result.
     * 
     * Creates debris entities for all cells in the fracture result, applying
     * the specified spawn settings. Physics bodies are created and integrated
     * into the physics world, and velocities are computed based on impact
     * direction and settings.
     * 
     * @param scene             The scene to spawn entities into.
     * @param physicsWorld      The physics world for rigid body creation.
     * @param fractureResult    The fracture data containing cell geometry.
     * @param worldTransform    World transform of the original object.
     * @param impactVelocity    Velocity at the point of impact.
     * @param settings          Configuration for debris properties.
     * @return Vector of spawned entity handles.
     * 
     * @note If spawning would exceed MaxConcurrentDebris, oldest debris
     *       entities are removed to make room for new ones.
     * @note Empty or invalid cells in fractureResult are skipped.
     */
    [[nodiscard]] std::vector<entt::entity> SpawnDebris(
        Scene& scene,
        PhysicsWorld& physicsWorld,
        FractureResult& fractureResult,
        const Math::Mat4& worldTransform,
        const Math::Vec3& impactVelocity,
        const DebrisSpawnSettings& settings);
    
    /**
     * @brief Spawns a single debris piece from a Voronoi cell.
     * 
     * Creates a single debris entity with the specified properties. This is
     * the lower-level API for custom debris spawning scenarios where full
     * control over individual pieces is needed.
     * 
     * @param scene             The scene to spawn the entity into.
     * @param physicsWorld      The physics world for rigid body creation.
     * @param cell              The Voronoi cell geometry for this piece.
     * @param worldTransform    World transform to apply to the cell.
     * @param velocity          Initial velocity for the debris piece.
     * @param lifetime          Lifetime in seconds before fade begins.
     * @return Entity handle for the spawned debris, or entt::null on failure.
     * 
     * @note Attempts to reuse pooled entities when available.
     * @note Does not check MaxConcurrentDebris; use SpawnDebris for that.
     */
    [[nodiscard]] entt::entity SpawnDebrisPiece(
        Scene& scene,
        PhysicsWorld& physicsWorld,
        VoronoiCell& cell,
        const Math::Mat4& worldTransform,
        const Math::Vec3& velocity,
        float lifetime);
    
    // =========================================================================
    // Lifecycle Management
    // =========================================================================
    
    /**
     * @brief Updates all active debris, processing lifetime and fade effects.
     * 
     * This method should be called once per frame to:
     * - Decrement remaining lifetime for all active debris
     * - Transition debris to fading state when lifetime expires
     * - Progress fade effects and update visual properties
     * - Remove fully faded debris and return entities to pool
     * 
     * @param deltaTime     Time elapsed since last update in seconds.
     * 
     * @note Debris in the fading state will have their alpha/opacity updated
     *       based on FadeProgress for visual fade-out effects.
     */
    void Update(float deltaTime);
    
    /**
     * @brief Immediately removes all active debris and clears the pool.
     * 
     * Destroys all debris entities currently active or pooled. This should
     * be called when transitioning levels or when a complete debris reset
     * is required.
     * 
     * @note This operation invalidates all previously returned entity handles.
     * @note The pool is completely emptied; new debris will require allocation.
     */
    void ClearAllDebris();
    
    // =========================================================================
    // Statistics and Configuration
    // =========================================================================
    
    /**
     * @brief Returns the count of currently active debris entities.
     * 
     * Active debris are those that have been spawned and not yet removed.
     * This includes debris in both normal and fading states.
     * 
     * @return Number of active debris entities.
     */
    [[nodiscard]] size_t GetActiveDebrisCount() const;
    
    /**
     * @brief Returns the count of pooled (available for reuse) debris entities.
     * 
     * Pooled entities have been returned after fading and are available for
     * immediate reuse without allocation.
     * 
     * @return Number of pooled debris entities.
     */
    [[nodiscard]] size_t GetPooledDebrisCount() const;
    
    /**
     * @brief Sets the maximum number of concurrent debris entities allowed.
     * 
     * When spawning would exceed this limit, the oldest active debris are
     * forcibly removed to make room. This prevents performance degradation
     * from excessive debris accumulation.
     * 
     * @param maxDebris     Maximum concurrent debris count (minimum: 1).
     * 
     * @note If current active count exceeds the new limit, excess debris
     *       are immediately removed.
     */
    void SetMaxConcurrentDebris(uint32_t maxDebris);
    
    /**
     * @brief Sets a global multiplier for all debris lifetimes.
     * 
     * This scale is applied to all debris lifetimes, allowing easy adjustment
     * of debris persistence for performance tuning or gameplay effects.
     * 
     * @param scale     Lifetime multiplier (0.0 = instant, 1.0 = normal, 2.0 = double).
     * 
     * @note Affects both new debris and currently active debris.
     * @note Values less than 0.1 are clamped to 0.1 to prevent instant removal.
     */
    void SetGlobalLifetimeScale(float scale);
    
private:
    // =========================================================================
    // Private Constructor (Singleton)
    // =========================================================================
    
    /**
     * @brief Private constructor for singleton pattern.
     */
    DebrisManager() = default;
    
    /**
     * @brief Destructor cleans up any remaining debris.
     */
    ~DebrisManager() = default;
    
    // =========================================================================
    // Pool Management
    // =========================================================================
    
    /**
     * @brief Returns a debris entity to the pool for reuse.
     * 
     * Deactivates the entity's physics and rendering components and adds
     * it to the pool queue for later reuse.
     * 
     * @param entity    The entity to return to the pool.
     */
    void ReturnToPool(entt::entity entity);
    
    /**
     * @brief Retrieves an entity from the pool if available.
     * 
     * @return An entity from the pool, or std::nullopt if the pool is empty.
     */
    [[nodiscard]] std::optional<entt::entity> GetFromPool();
    
    /**
     * @brief Removes all debris that have completed their fade-out.
     * 
     * Iterates through active debris and returns fully faded entities to
     * the pool, removing their entries from the active list.
     */
    void CleanupExpiredDebris();
    
    // =========================================================================
    // Member Variables
    // =========================================================================
    
    /** @brief List of currently active debris with tracking data. */
    std::vector<DebrisEntry> m_ActiveDebris;
    
    /** @brief Queue of pooled entities available for reuse. */
    std::queue<entt::entity> m_DebrisPool;
    
    /** @brief Maximum number of concurrent debris entities. */
    uint32_t m_MaxDebris{ 1000 };
    
    /** @brief Global lifetime scale multiplier. */
    float m_GlobalLifetimeScale{ 1.0f };
    
    /** @brief Cached scene pointer for cleanup operations. */
    Scene* m_Scene{ nullptr };
    
    /** @brief Cached physics world pointer for physics cleanup. */
    PhysicsWorld* m_PhysicsWorld{ nullptr };
};

} // namespace Physics
} // namespace Core
