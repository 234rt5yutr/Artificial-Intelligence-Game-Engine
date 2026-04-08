#pragma once

/**
 * @file DestructibleMeshComponent.h
 * @brief ECS component for meshes that can be fractured and destroyed at runtime.
 *
 * This component integrates with the Voronoi fracturing system to enable
 * dynamic destruction of meshes based on impact forces, accumulated damage,
 * or scripted events. Supports both pre-computed and runtime fracturing.
 *
 * Features:
 * - Impact-based destruction with force thresholds
 * - Health-based damage accumulation system
 * - Configurable debris spawning with pooling support
 * - Optional regeneration for respawning destructibles
 * - Pre-computed fracture caching for performance
 *
 * @see Core::Physics::VoronoiFracturer for the underlying fracturing algorithm
 * @see Core::ECS::DestructionSystem for runtime destruction processing
 */

#include "Core/Physics/Destruction/VoronoiFracturer.h"
#include "Core/Math/Math.h"

#include <entt/entity/entity.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Forward declarations
namespace Core {
namespace Renderer {
    class Mesh;
} // namespace Renderer
} // namespace Core

namespace Core {
namespace ECS {

// =============================================================================
// Enumerations
// =============================================================================

/**
 * @brief Current state of a destructible mesh in its lifecycle.
 *
 * The state machine transitions are:
 * - Intact -> Fracturing (on destruction trigger)
 * - Fracturing -> Destroyed (when fracture completes)
 * - Destroyed -> Regenerating (if CanRegenerate is true)
 * - Regenerating -> Intact (when regeneration completes)
 */
enum class DestructionState : uint8_t
{
    /** @brief Mesh is whole and undamaged, ready to receive damage. */
    Intact,

    /** @brief Mesh is actively being fractured (transitional state). */
    Fracturing,

    /** @brief Mesh has been destroyed and replaced with debris. */
    Destroyed,

    /** @brief Mesh is regenerating back to its intact state. */
    Regenerating
};

// =============================================================================
// Configuration Structures
// =============================================================================

/**
 * @brief Configuration for debris piece spawning and lifecycle management.
 *
 * Controls how debris pieces behave after destruction, including physics
 * properties, visual effects, and memory management through object pooling.
 *
 * @code
 * DebrisSpawnSettings settings;
 * settings.InitialVelocityScale = 2.0f;  // More explosive
 * settings.LifetimeMax = 10.0f;          // Shorter cleanup
 * settings.UsePooling = true;            // Reuse debris entities
 * @endcode
 */
struct DebrisSpawnSettings
{
    // =========================================================================
    // Physics Properties
    // =========================================================================

    /**
     * @brief Multiplier applied to debris initial linear velocity.
     * @note Higher values create more explosive destruction effects.
     */
    float InitialVelocityScale{ 1.0f };

    /**
     * @brief Multiplier applied to debris initial angular velocity.
     * @note Higher values cause debris to tumble more aggressively.
     */
    float AngularVelocityScale{ 1.0f };

    // =========================================================================
    // Lifetime Management
    // =========================================================================

    /**
     * @brief Minimum lifetime in seconds before debris begins despawning.
     * @note Actual lifetime is randomized between LifetimeMin and LifetimeMax.
     */
    float LifetimeMin{ 5.0f };

    /**
     * @brief Maximum lifetime in seconds before debris begins despawning.
     * @note Set equal to LifetimeMin for consistent debris duration.
     */
    float LifetimeMax{ 15.0f };

    /**
     * @brief Whether debris should fade out visually before despawning.
     * @note When false, debris disappears instantly at end of lifetime.
     */
    bool FadeOnDespawn{ true };

    /**
     * @brief Duration in seconds for the fade-out effect.
     * @note Only used when FadeOnDespawn is true.
     */
    float FadeDuration{ 1.0f };

    // =========================================================================
    // Level of Detail
    // =========================================================================

    /**
     * @brief Distance at which debris uses highest detail level.
     * @note Debris closer than this distance renders at full quality.
     */
    float LodDistanceNear{ 10.0f };

    /**
     * @brief Distance at which debris uses lowest detail level or culls.
     * @note Debris beyond this distance may be simplified or hidden.
     */
    float LodDistanceFar{ 50.0f };

    // =========================================================================
    // Object Pooling
    // =========================================================================

    /**
     * @brief Whether to use object pooling for debris entities.
     * @note Pooling significantly reduces allocation overhead during destruction.
     */
    bool UsePooling{ true };

    /**
     * @brief Maximum number of debris entities to keep in the pool.
     * @note Excess debris beyond this count will be destroyed immediately.
     */
    uint32_t MaxPoolSize{ 1000 };
};

// =============================================================================
// Main Component
// =============================================================================

/**
 * @brief ECS component enabling runtime mesh destruction and fracturing.
 *
 * Attach this component to entities with MeshComponent to make them
 * destructible. The component tracks damage, manages fracture state,
 * and handles debris spawning when destruction is triggered.
 *
 * ## Usage Patterns
 *
 * ### Impact-Based Destruction
 * @code
 * auto component = DestructibleMeshComponent::CreateExplosive("barrel.mesh");
 * component.ImpactForceThreshold = 500.0f;  // Sensitive to impacts
 * @endcode
 *
 * ### Health-Based Destruction
 * @code
 * auto component = DestructibleMeshComponent::CreateHealthBased("crate.mesh", 100.0f);
 * component.ApplyDamage(25.0f);  // 25% damage
 * component.ApplyDamage(75.0f);  // Triggers destruction
 * @endcode
 *
 * ### Pre-Computed Fracturing
 * @code
 * auto component = DestructibleMeshComponent::CreateBasic("pillar.mesh");
 * component.PreFractured = true;
 * component.FractureSettings.SeedCount = 16;
 * // System will pre-compute fracture at load time
 * @endcode
 *
 * @note This component should be used alongside MeshComponent and optionally
 *       ColliderComponent for physics-based destruction triggers.
 */
struct DestructibleMeshComponent
{
    // =========================================================================
    // Mesh References
    // =========================================================================

    /**
     * @brief Asset path to the mesh resource.
     * @note Used for loading and identifying the mesh asset.
     */
    std::string MeshPath;

    /**
     * @brief Shared pointer to the original intact mesh.
     * @note Retained for regeneration and reference during fracturing.
     */
    std::shared_ptr<Renderer::Mesh> OriginalMesh;

    // =========================================================================
    // Fracture Configuration
    // =========================================================================

    /**
     * @brief Settings controlling the Voronoi fracturing algorithm.
     * @see Core::Physics::FractureSettings for all available options.
     */
    Physics::FractureSettings FractureSettings;

    /**
     * @brief Whether to pre-compute fracture geometry at load time.
     * @note Pre-fracturing trades memory for faster runtime destruction.
     */
    bool PreFractured{ false };

    /**
     * @brief Cached pre-computed fracture result.
     * @note Populated during initialization if PreFractured is true.
     */
    std::shared_ptr<Physics::FractureResult> PrecomputedFracture;

    // =========================================================================
    // Damage System
    // =========================================================================

    /**
     * @brief Minimum impact force required to trigger destruction.
     * @note Set to 0 to disable impact-based destruction.
     * @note Force is measured in Newtons (mass * acceleration).
     */
    float ImpactForceThreshold{ 1000.0f };

    /**
     * @brief Maximum health points before destruction.
     * @note Set to 0 or negative to disable health-based destruction.
     */
    float HealthPoints{ 100.0f };

    /**
     * @brief Current accumulated damage.
     * @note Destruction triggers when CurrentDamage >= HealthPoints.
     */
    float CurrentDamage{ 0.0f };

    /**
     * @brief Whether to destroy the entity when health reaches zero.
     * @note If false, entity remains with Destroyed state for manual handling.
     */
    bool DestroyOnDeath{ true };

    // =========================================================================
    // Debris Configuration
    // =========================================================================

    /**
     * @brief Settings for debris spawning and lifecycle.
     * @see DebrisSpawnSettings for all configuration options.
     */
    DebrisSpawnSettings DebrisSettings;

    // =========================================================================
    // Runtime State
    // =========================================================================

    /**
     * @brief Current destruction state of the mesh.
     * @note Managed by DestructionSystem; avoid modifying directly.
     */
    DestructionState State{ DestructionState::Intact };

    /**
     * @brief Entity handles of all spawned debris pieces.
     * @note Used for tracking and cleanup of debris entities.
     */
    std::vector<entt::entity> SpawnedDebris;

    // =========================================================================
    // Regeneration
    // =========================================================================

    /**
     * @brief Whether this destructible can regenerate after destruction.
     * @note Useful for respawning destructibles in gameplay scenarios.
     */
    bool CanRegenerate{ false };

    /**
     * @brief Delay in seconds before regeneration begins after destruction.
     * @note Only used when CanRegenerate is true.
     */
    float RegenerationDelay{ 5.0f };

    /**
     * @brief Internal timer tracking regeneration progress.
     * @note Managed by DestructionSystem; do not modify directly.
     */
    float RegenerationTimer{ 0.0f };

    // =========================================================================
    // Destruction Event Data
    // =========================================================================

    /**
     * @brief Flag indicating destruction has been triggered this frame.
     * @note Reset by DestructionSystem after processing.
     */
    bool DestructionTriggered{ false };

    /**
     * @brief World-space position of the impact that triggered destruction.
     * @note Used for radial fracture patterns and debris velocity calculation.
     */
    Math::Vec3 ImpactPoint{ 0.0f, 0.0f, 0.0f };

    /**
     * @brief Velocity of the impacting object at the moment of destruction.
     * @note Used to impart realistic momentum to debris pieces.
     */
    Math::Vec3 ImpactVelocity{ 0.0f, 0.0f, 0.0f };

    // =========================================================================
    // Constructors
    // =========================================================================

    DestructibleMeshComponent() = default;

    explicit DestructibleMeshComponent(const std::string& meshPath)
        : MeshPath(meshPath)
    {
    }

    DestructibleMeshComponent(const std::string& meshPath,
                              std::shared_ptr<Renderer::Mesh> mesh)
        : MeshPath(meshPath)
        , OriginalMesh(std::move(mesh))
    {
    }

    // =========================================================================
    // Methods
    // =========================================================================

    /**
     * @brief Applies damage to the destructible mesh.
     *
     * Accumulates damage and triggers destruction if total damage
     * exceeds HealthPoints. Does nothing if already destroyed.
     *
     * @param amount    Amount of damage to apply (positive values only).
     * @return true if this damage triggered destruction, false otherwise.
     *
     * @note Negative damage values are clamped to zero.
     * @note Does not trigger destruction if HealthPoints <= 0 (disabled).
     */
    bool ApplyDamage(float amount)
    {
        if (State != DestructionState::Intact || amount <= 0.0f)
        {
            return false;
        }

        CurrentDamage += amount;

        if (HealthPoints > 0.0f && CurrentDamage >= HealthPoints)
        {
            DestructionTriggered = true;
            return true;
        }

        return false;
    }

    /**
     * @brief Triggers immediate destruction at the specified impact location.
     *
     * Forces destruction regardless of current damage or health state.
     * Sets up impact data for radial fracturing and debris velocity.
     *
     * @param point     World-space position of the impact.
     * @param velocity  Velocity of the impacting object.
     *
     * @note Does nothing if mesh is already destroyed or regenerating.
     * @note Use for scripted destruction or instant-kill scenarios.
     */
    void TriggerDestruction(const Math::Vec3& point, const Math::Vec3& velocity)
    {
        if (State != DestructionState::Intact)
        {
            return;
        }

        ImpactPoint = point;
        ImpactVelocity = velocity;
        DestructionTriggered = true;
    }

    /**
     * @brief Checks if the mesh is currently intact and can receive damage.
     * @return true if State is Intact, false otherwise.
     */
    [[nodiscard]] bool IsIntact() const
    {
        return State == DestructionState::Intact;
    }

    /**
     * @brief Checks if the mesh has been destroyed.
     * @return true if State is Destroyed, false otherwise.
     */
    [[nodiscard]] bool IsDestroyed() const
    {
        return State == DestructionState::Destroyed;
    }

    /**
     * @brief Gets the current health percentage (0.0 to 1.0).
     * @return Remaining health as a fraction, or 1.0 if health system disabled.
     */
    [[nodiscard]] float GetHealthPercentage() const
    {
        if (HealthPoints <= 0.0f)
        {
            return 1.0f;
        }
        return glm::clamp(1.0f - (CurrentDamage / HealthPoints), 0.0f, 1.0f);
    }

    /**
     * @brief Resets the destructible to its initial intact state.
     *
     * Clears damage, resets state, and prepares for destruction again.
     * Does not automatically despawn existing debris.
     *
     * @note Called internally by DestructionSystem during regeneration.
     * @note SpawnedDebris should be cleaned up separately before calling.
     */
    void Reset()
    {
        State = DestructionState::Intact;
        CurrentDamage = 0.0f;
        DestructionTriggered = false;
        ImpactPoint = Math::Vec3{ 0.0f, 0.0f, 0.0f };
        ImpactVelocity = Math::Vec3{ 0.0f, 0.0f, 0.0f };
        RegenerationTimer = 0.0f;
        SpawnedDebris.clear();
    }

    // =========================================================================
    // Factory Methods
    // =========================================================================

    /**
     * @brief Creates a basic destructible mesh with default settings.
     *
     * Suitable for generic destructible objects with balanced settings.
     *
     * @param meshPath  Path to the mesh asset.
     * @return Configured DestructibleMeshComponent.
     */
    [[nodiscard]] static DestructibleMeshComponent CreateBasic(
        const std::string& meshPath)
    {
        DestructibleMeshComponent component(meshPath);
        component.FractureSettings.SeedCount = 8;
        component.FractureSettings.Pattern = Physics::FractureSeedPattern::Uniform;
        return component;
    }

    /**
     * @brief Creates an explosive destructible with radial fracturing.
     *
     * Optimized for objects that should shatter dramatically on impact,
     * with debris flying outward from the impact point.
     *
     * @param meshPath  Path to the mesh asset.
     * @param seedCount Number of fracture pieces (default: 16).
     * @return Configured DestructibleMeshComponent.
     */
    [[nodiscard]] static DestructibleMeshComponent CreateExplosive(
        const std::string& meshPath,
        uint32_t seedCount = 16)
    {
        DestructibleMeshComponent component(meshPath);
        component.FractureSettings.SeedCount = seedCount;
        component.FractureSettings.Pattern = Physics::FractureSeedPattern::Radial;
        component.FractureSettings.ImpactRadius = 2.0f;
        component.DebrisSettings.InitialVelocityScale = 2.5f;
        component.DebrisSettings.AngularVelocityScale = 3.0f;
        component.ImpactForceThreshold = 500.0f;
        return component;
    }

    /**
     * @brief Creates a health-based destructible for damage accumulation.
     *
     * Designed for objects that should withstand multiple hits before
     * breaking, with configurable health pool.
     *
     * @param meshPath      Path to the mesh asset.
     * @param maxHealth     Maximum health points (default: 100.0).
     * @param canRegenerate Whether the object can regenerate (default: false).
     * @return Configured DestructibleMeshComponent.
     */
    [[nodiscard]] static DestructibleMeshComponent CreateHealthBased(
        const std::string& meshPath,
        float maxHealth = 100.0f,
        bool canRegenerate = false)
    {
        DestructibleMeshComponent component(meshPath);
        component.HealthPoints = maxHealth;
        component.ImpactForceThreshold = 0.0f; // Disable impact destruction
        component.CanRegenerate = canRegenerate;
        component.RegenerationDelay = 10.0f;
        component.FractureSettings.SeedCount = 12;
        component.FractureSettings.Pattern = Physics::FractureSeedPattern::Uniform;
        return component;
    }
};

} // namespace ECS
} // namespace Core
