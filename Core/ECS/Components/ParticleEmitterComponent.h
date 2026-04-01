#pragma once

/**
 * @file ParticleEmitterComponent.h
 * @brief ECS component for particle emission configuration
 * 
 * Defines a full-featured particle emitter component that works with the
 * GPU particle system. Supports multiple emitter shapes, color/size gradients,
 * and comprehensive emission parameters.
 */

#include "Core/Math/Math.h"
#include "Core/Renderer/Particles/TextureAtlas.h"
#include <vector>
#include <cstdint>
#include <algorithm>

namespace Core {
namespace ECS {

    //=========================================================================
    // Emitter Shape Types
    //=========================================================================

    /**
     * @brief Shape of the particle emission volume
     */
    enum class EmitterShape : uint32_t {
        Point = 0,      // Emit from a single point
        Sphere = 1,     // Emit from surface or volume of sphere
        Box = 2,        // Emit from surface or volume of box
        Cone = 3,       // Emit in a cone direction
        Ring = 4        // Emit from a ring (torus cross-section)
    };

    //=========================================================================
    // Simulation Space
    //=========================================================================

    /**
     * @brief Space in which particles are simulated
     */
    enum class ParticleSimulationSpace : uint32_t {
        World = 0,      // Particles move in world space (independent of emitter)
        Local = 1       // Particles move relative to emitter transform
    };

    //=========================================================================
    // Color Gradient
    //=========================================================================

    /**
     * @brief Represents a color gradient over particle lifetime
     * 
     * Color keys are defined at normalized time values (0-1) where:
     * - 0.0 = particle birth
     * - 1.0 = particle death
     */
    struct ColorGradient {
        std::vector<std::pair<float, Math::Vec4>> ColorKeys;

        ColorGradient() {
            // Default: white to transparent white
            ColorKeys.emplace_back(0.0f, Math::Vec4(1.0f, 1.0f, 1.0f, 1.0f));
            ColorKeys.emplace_back(1.0f, Math::Vec4(1.0f, 1.0f, 1.0f, 0.0f));
        }

        ColorGradient(const Math::Vec4& startColor, const Math::Vec4& endColor) {
            ColorKeys.emplace_back(0.0f, startColor);
            ColorKeys.emplace_back(1.0f, endColor);
        }

        /**
         * @brief Add a color key to the gradient
         * @param time Normalized time (0-1)
         * @param color RGBA color value
         */
        void AddKey(float time, const Math::Vec4& color) {
            time = std::clamp(time, 0.0f, 1.0f);
            
            // Insert in sorted order
            auto it = std::lower_bound(ColorKeys.begin(), ColorKeys.end(), time,
                [](const auto& key, float t) { return key.first < t; });
            ColorKeys.insert(it, std::make_pair(time, color));
        }

        /**
         * @brief Sample the gradient at a given normalized time
         * @param t Normalized time (0-1), clamped internally
         * @return Interpolated RGBA color
         */
        Math::Vec4 Sample(float t) const {
            if (ColorKeys.empty()) {
                return Math::Vec4(1.0f);
            }
            
            t = std::clamp(t, 0.0f, 1.0f);
            
            if (ColorKeys.size() == 1 || t <= ColorKeys.front().first) {
                return ColorKeys.front().second;
            }
            
            if (t >= ColorKeys.back().first) {
                return ColorKeys.back().second;
            }
            
            // Find surrounding keys and interpolate
            for (size_t i = 0; i < ColorKeys.size() - 1; ++i) {
                if (t >= ColorKeys[i].first && t <= ColorKeys[i + 1].first) {
                    float localT = (t - ColorKeys[i].first) / 
                                   (ColorKeys[i + 1].first - ColorKeys[i].first);
                    return glm::mix(ColorKeys[i].second, ColorKeys[i + 1].second, localT);
                }
            }
            
            return ColorKeys.back().second;
        }

        /**
         * @brief Get start color (at t=0)
         */
        Math::Vec4 GetStartColor() const {
            return Sample(0.0f);
        }

        /**
         * @brief Get end color (at t=1)
         */
        Math::Vec4 GetEndColor() const {
            return Sample(1.0f);
        }

        /**
         * @brief Clear all keys and reset to default
         */
        void Reset() {
            ColorKeys.clear();
            ColorKeys.emplace_back(0.0f, Math::Vec4(1.0f, 1.0f, 1.0f, 1.0f));
            ColorKeys.emplace_back(1.0f, Math::Vec4(1.0f, 1.0f, 1.0f, 0.0f));
        }
    };

    //=========================================================================
    // Size Over Lifetime
    //=========================================================================

    /**
     * @brief Defines particle size variation over lifetime with randomization
     */
    struct SizeOverLifetime {
        float StartSizeMin = 0.1f;      // Minimum size at birth
        float StartSizeMax = 0.5f;      // Maximum size at birth
        float EndSizeMin = 0.0f;        // Minimum size at death
        float EndSizeMax = 0.2f;        // Maximum size at death

        SizeOverLifetime() = default;

        SizeOverLifetime(float startMin, float startMax, float endMin, float endMax)
            : StartSizeMin(startMin)
            , StartSizeMax(startMax)
            , EndSizeMin(endMin)
            , EndSizeMax(endMax)
        {}

        /**
         * @brief Sample size at a given normalized age with randomization
         * @param normalizedAge Normalized age (0=birth, 1=death)
         * @param random01 Random value [0,1] for size variation
         * @return Interpolated size value
         */
        float Sample(float normalizedAge, float random01) const {
            normalizedAge = std::clamp(normalizedAge, 0.0f, 1.0f);
            random01 = std::clamp(random01, 0.0f, 1.0f);
            
            float startSize = StartSizeMin + (StartSizeMax - StartSizeMin) * random01;
            float endSize = EndSizeMin + (EndSizeMax - EndSizeMin) * random01;
            
            return startSize + (endSize - startSize) * normalizedAge;
        }

        /**
         * @brief Get the average start size
         */
        float GetAverageStartSize() const {
            return (StartSizeMin + StartSizeMax) * 0.5f;
        }

        /**
         * @brief Get the average end size
         */
        float GetAverageEndSize() const {
            return (EndSizeMin + EndSizeMax) * 0.5f;
        }
    };

    //=========================================================================
    // Velocity Range
    //=========================================================================

    /**
     * @brief Defines velocity parameters for particle emission
     */
    struct VelocityRange {
        float SpeedMin = 1.0f;          // Minimum initial speed
        float SpeedMax = 5.0f;          // Maximum initial speed
        Math::Vec3 Direction{0.0f, 1.0f, 0.0f};  // Primary emission direction
        float ConeAngle = 30.0f;        // Spread angle in degrees (0 = focused, 180 = hemisphere)

        VelocityRange() = default;

        VelocityRange(float minSpeed, float maxSpeed, const Math::Vec3& dir, float angle)
            : SpeedMin(minSpeed)
            , SpeedMax(maxSpeed)
            , Direction(dir)
            , ConeAngle(angle)
        {}

        /**
         * @brief Get cone angle in radians
         */
        float GetConeAngleRadians() const {
            return glm::radians(ConeAngle);
        }
    };

    //=========================================================================
    // Rotation Parameters
    //=========================================================================

    /**
     * @brief Defines particle rotation behavior
     */
    struct ParticleRotation {
        float InitialRotationMin = 0.0f;    // Minimum initial rotation (degrees)
        float InitialRotationMax = 360.0f;  // Maximum initial rotation (degrees)
        float AngularVelocityMin = 0.0f;    // Minimum rotation speed (degrees/sec)
        float AngularVelocityMax = 0.0f;    // Maximum rotation speed (degrees/sec)

        ParticleRotation() = default;

        ParticleRotation(float rotMin, float rotMax, float velMin, float velMax)
            : InitialRotationMin(rotMin)
            , InitialRotationMax(rotMax)
            , AngularVelocityMin(velMin)
            , AngularVelocityMax(velMax)
        {}

        /**
         * @brief Get initial rotation range in radians
         */
        Math::Vec2 GetInitialRotationRadians() const {
            return Math::Vec2(glm::radians(InitialRotationMin), 
                              glm::radians(InitialRotationMax));
        }

        /**
         * @brief Get angular velocity range in radians/sec
         */
        Math::Vec2 GetAngularVelocityRadians() const {
            return Math::Vec2(glm::radians(AngularVelocityMin), 
                              glm::radians(AngularVelocityMax));
        }
    };

    //=========================================================================
    // Emitter Shape Parameters
    //=========================================================================

    /**
     * @brief Parameters specific to each emitter shape type
     */
    struct EmitterShapeParams {
        // Sphere/Ring
        float Radius = 1.0f;                // Radius for sphere, ring
        float RadiusThickness = 1.0f;       // 0 = surface only, 1 = full volume
        
        // Box
        Math::Vec3 BoxDimensions{1.0f, 1.0f, 1.0f};  // Half-extents of box
        
        // Cone
        float ConeAngle = 25.0f;            // Cone angle in degrees
        float ConeRadius = 1.0f;            // Radius at cone base
        float ConeLength = 5.0f;            // Length of cone
        
        // Ring
        float RingRadius = 0.5f;            // Radius of ring tube (torus minor radius)

        EmitterShapeParams() = default;
    };

    //=========================================================================
    // Burst Configuration
    //=========================================================================

    /**
     * @brief Configuration for particle bursts
     */
    struct BurstConfig {
        bool Enabled = false;           // Enable burst emission
        float Time = 0.0f;              // Time offset for first burst (relative to start)
        uint32_t Count = 10;            // Particles per burst
        uint32_t Cycles = 1;            // Number of burst cycles (0 = infinite)
        float Interval = 1.0f;          // Time between bursts
        float Probability = 1.0f;       // Probability of burst occurring (0-1)

        BurstConfig() = default;

        BurstConfig(uint32_t count, float interval, uint32_t cycles = 0)
            : Enabled(true)
            , Count(count)
            , Cycles(cycles)
            , Interval(interval)
        {}
    };

    //=========================================================================
    // Particle Emitter Component
    //=========================================================================

    /**
     * @brief Full-featured particle emitter component for the ECS
     * 
     * This component defines all parameters for a particle emitter and is
     * designed to work with the GPU-based ParticleSystem. When attached to
     * an entity with a TransformComponent, particles will be emitted from
     * the entity's world position.
     * 
     * Usage:
     * @code
     * ParticleEmitterComponent emitter;
     * emitter.EmissionRate = 100.0f;
     * emitter.Lifetime = {1.0f, 3.0f};
     * emitter.ColorOverTime = ColorGradient(
     *     Math::Vec4(1.0f, 0.5f, 0.0f, 1.0f),  // Orange start
     *     Math::Vec4(1.0f, 0.0f, 0.0f, 0.0f)   // Red fade out
     * );
     * entity.AddComponent(emitter);
     * @endcode
     */
    struct ParticleEmitterComponent {
        //---------------------------------------------------------------------
        // Enable/Playback State
        //---------------------------------------------------------------------
        
        bool Enabled = true;            // Master enable flag
        bool Playing = true;            // Is currently emitting
        bool Looping = true;            // Loop after duration ends
        bool PreWarm = false;           // Spawn particles immediately on start
        float Duration = 5.0f;          // Duration for non-looping emitters (seconds)

        //---------------------------------------------------------------------
        // Emission Parameters
        //---------------------------------------------------------------------
        
        float EmissionRate = 100.0f;    // Particles per second
        uint32_t MaxParticles = 1000;   // Maximum alive particles from this emitter
        BurstConfig Burst;              // Burst emission configuration

        //---------------------------------------------------------------------
        // Lifetime
        //---------------------------------------------------------------------
        
        Math::Vec2 Lifetime{1.0f, 3.0f};  // Min/max particle lifetime (seconds)

        //---------------------------------------------------------------------
        // Shape
        //---------------------------------------------------------------------
        
        EmitterShape Shape = EmitterShape::Cone;
        EmitterShapeParams ShapeParams;
        
        //---------------------------------------------------------------------
        // Simulation
        //---------------------------------------------------------------------
        
        ParticleSimulationSpace SimulationSpace = ParticleSimulationSpace::World;
        Math::Vec3 Gravity{0.0f, -9.81f, 0.0f};  // Local gravity override
        bool UseGlobalGravity = true;            // Use system gravity instead
        float GravityModifier = 1.0f;            // Multiplier for gravity effect
        float Drag = 0.0f;                       // Air resistance (0-1)

        //---------------------------------------------------------------------
        // Velocity
        //---------------------------------------------------------------------
        
        VelocityRange Velocity;
        bool InheritVelocity = false;   // Inherit emitter velocity
        float InheritVelocityMultiplier = 0.0f;

        //---------------------------------------------------------------------
        // Color Over Lifetime
        //---------------------------------------------------------------------
        
        ColorGradient ColorOverTime;

        //---------------------------------------------------------------------
        // Size Over Lifetime
        //---------------------------------------------------------------------
        
        SizeOverLifetime SizeOverTime;
        bool UniformSize = true;        // Use same size for width/height
        
        //---------------------------------------------------------------------
        // Rotation
        //---------------------------------------------------------------------
        
        ParticleRotation Rotation;
        bool AlignToDirection = false;  // Rotate sprite to face velocity direction

        //---------------------------------------------------------------------
        // Texture Atlas / Sprite Sheet Animation
        //---------------------------------------------------------------------
        
        /**
         * @brief Full texture atlas configuration for animated particles
         * 
         * Supports multiple animation modes:
         * - OverLifetime: Frame advances from 0 to frameCount based on particle age
         * - BySpeed: Frame changes based on particle velocity
         * - Random: Each particle gets a random frame at spawn
         * - Fixed: All particles use the same frame
         * - RealTime: Animation plays based on elapsed time
         * - PingPong: Animation oscillates back and forth
         */
        Core::Renderer::Particles::TextureAtlas Atlas;
        
        /**
         * @brief Legacy texture atlas index (for backward compatibility)
         * @deprecated Use Atlas.FixedFrameIndex instead
         */
        uint32_t TextureAtlasIndex = 0;
        
        /**
         * @brief Legacy atlas grid rows (for backward compatibility)
         * @deprecated Use Atlas.Rows instead
         */
        uint32_t TextureAtlasRows = 1;
        
        /**
         * @brief Legacy atlas grid columns (for backward compatibility)
         * @deprecated Use Atlas.Columns instead
         */
        uint32_t TextureAtlasCols = 1;
        
        /**
         * @brief Legacy animate over lifetime flag (for backward compatibility)
         * @deprecated Use Atlas.AnimationMode = AtlasAnimationMode::OverLifetime instead
         */
        bool AnimateTextureOverLifetime = false;
        
        /**
         * @brief Legacy texture animation speed (for backward compatibility)
         * @deprecated Use Atlas.FrameRate instead
         */
        float TextureAnimationSpeed = 1.0f;
        
        /**
         * @brief Random frame offset variance for visual diversity
         * 
         * When > 0, adds a random offset to the starting frame for each particle.
         * This creates variety when multiple particles use the same animation.
         */
        uint32_t RandomFrameOffset = 0;
        
        int32_t RenderLayer = 0;        // Rendering sort layer
        float SortingFudge = 0.0f;      // Depth sorting bias

        //---------------------------------------------------------------------
        // Runtime State (managed by particle system)
        //---------------------------------------------------------------------
        
        uint32_t EmitterHandle = UINT32_MAX;  // Handle from ParticleSystem
        float ElapsedTime = 0.0f;             // Time since emission started
        float EmitAccumulator = 0.0f;         // Fractional particle accumulator
        uint32_t BurstCycleCount = 0;         // Current burst cycle
        float BurstTimer = 0.0f;              // Time until next burst
        bool WasPreWarmed = false;            // Pre-warm already applied

        //---------------------------------------------------------------------
        // Constructors
        //---------------------------------------------------------------------
        
        ParticleEmitterComponent() = default;

        /**
         * @brief Create a simple emitter with basic parameters
         */
        ParticleEmitterComponent(float rate, float lifetimeMin, float lifetimeMax)
            : EmissionRate(rate)
            , Lifetime(lifetimeMin, lifetimeMax)
        {}

        //---------------------------------------------------------------------
        // Factory Methods
        //---------------------------------------------------------------------

        /**
         * @brief Create a fire particle effect
         */
        static ParticleEmitterComponent CreateFire(float intensity = 1.0f) {
            ParticleEmitterComponent emitter;
            emitter.EmissionRate = 50.0f * intensity;
            emitter.MaxParticles = 500;
            emitter.Lifetime = {0.5f, 1.5f};
            
            emitter.Shape = EmitterShape::Cone;
            emitter.ShapeParams.ConeAngle = 15.0f;
            emitter.ShapeParams.ConeRadius = 0.5f;
            
            emitter.Velocity.SpeedMin = 2.0f;
            emitter.Velocity.SpeedMax = 4.0f;
            emitter.Velocity.Direction = {0.0f, 1.0f, 0.0f};
            emitter.Velocity.ConeAngle = 20.0f;
            
            emitter.Gravity = {0.0f, 1.0f, 0.0f};  // Upward buoyancy
            emitter.UseGlobalGravity = false;
            
            emitter.ColorOverTime.ColorKeys.clear();
            emitter.ColorOverTime.AddKey(0.0f, {1.0f, 0.8f, 0.2f, 1.0f});  // Yellow
            emitter.ColorOverTime.AddKey(0.3f, {1.0f, 0.4f, 0.1f, 1.0f});  // Orange
            emitter.ColorOverTime.AddKey(0.7f, {0.8f, 0.1f, 0.0f, 0.8f});  // Red
            emitter.ColorOverTime.AddKey(1.0f, {0.2f, 0.0f, 0.0f, 0.0f});  // Dark red fade
            
            emitter.SizeOverTime = {0.3f, 0.5f, 0.8f, 1.2f};
            
            return emitter;
        }

        /**
         * @brief Create a smoke particle effect
         */
        static ParticleEmitterComponent CreateSmoke(float intensity = 1.0f) {
            ParticleEmitterComponent emitter;
            emitter.EmissionRate = 20.0f * intensity;
            emitter.MaxParticles = 200;
            emitter.Lifetime = {2.0f, 4.0f};
            
            emitter.Shape = EmitterShape::Sphere;
            emitter.ShapeParams.Radius = 0.3f;
            
            emitter.Velocity.SpeedMin = 0.5f;
            emitter.Velocity.SpeedMax = 1.5f;
            emitter.Velocity.Direction = {0.0f, 1.0f, 0.0f};
            emitter.Velocity.ConeAngle = 30.0f;
            
            emitter.Gravity = {0.0f, 0.5f, 0.0f};
            emitter.UseGlobalGravity = false;
            emitter.Drag = 0.1f;
            
            emitter.ColorOverTime.ColorKeys.clear();
            emitter.ColorOverTime.AddKey(0.0f, {0.4f, 0.4f, 0.4f, 0.6f});
            emitter.ColorOverTime.AddKey(0.5f, {0.5f, 0.5f, 0.5f, 0.4f});
            emitter.ColorOverTime.AddKey(1.0f, {0.6f, 0.6f, 0.6f, 0.0f});
            
            emitter.SizeOverTime = {0.5f, 0.8f, 2.0f, 3.0f};
            
            emitter.Rotation.AngularVelocityMin = -30.0f;
            emitter.Rotation.AngularVelocityMax = 30.0f;
            
            return emitter;
        }

        /**
         * @brief Create a sparks/debris particle effect
         */
        static ParticleEmitterComponent CreateSparks(float intensity = 1.0f) {
            ParticleEmitterComponent emitter;
            emitter.EmissionRate = 0.0f;  // Burst only
            emitter.MaxParticles = 100;
            emitter.Lifetime = {0.3f, 0.8f};
            emitter.Looping = false;
            
            emitter.Burst.Enabled = true;
            emitter.Burst.Count = static_cast<uint32_t>(30 * intensity);
            emitter.Burst.Cycles = 1;
            
            emitter.Shape = EmitterShape::Point;
            
            emitter.Velocity.SpeedMin = 5.0f;
            emitter.Velocity.SpeedMax = 15.0f;
            emitter.Velocity.ConeAngle = 180.0f;  // All directions
            
            emitter.GravityModifier = 1.0f;
            
            emitter.ColorOverTime.ColorKeys.clear();
            emitter.ColorOverTime.AddKey(0.0f, {1.0f, 0.9f, 0.5f, 1.0f});  // Bright yellow
            emitter.ColorOverTime.AddKey(0.5f, {1.0f, 0.5f, 0.1f, 1.0f});  // Orange
            emitter.ColorOverTime.AddKey(1.0f, {0.5f, 0.1f, 0.0f, 0.0f});  // Red fade
            
            emitter.SizeOverTime = {0.05f, 0.1f, 0.01f, 0.02f};
            
            emitter.Drag = 0.05f;
            
            return emitter;
        }

        /**
         * @brief Create a dust/explosion burst effect
         */
        static ParticleEmitterComponent CreateExplosion(float radius = 2.0f) {
            ParticleEmitterComponent emitter;
            emitter.EmissionRate = 0.0f;
            emitter.MaxParticles = 200;
            emitter.Lifetime = {0.5f, 1.5f};
            emitter.Looping = false;
            
            emitter.Burst.Enabled = true;
            emitter.Burst.Count = 100;
            emitter.Burst.Cycles = 1;
            
            emitter.Shape = EmitterShape::Sphere;
            emitter.ShapeParams.Radius = radius * 0.2f;
            
            emitter.Velocity.SpeedMin = radius * 3.0f;
            emitter.Velocity.SpeedMax = radius * 8.0f;
            emitter.Velocity.ConeAngle = 180.0f;
            
            emitter.GravityModifier = 0.5f;
            emitter.Drag = 0.2f;
            
            emitter.ColorOverTime.ColorKeys.clear();
            emitter.ColorOverTime.AddKey(0.0f, {1.0f, 0.8f, 0.4f, 1.0f});
            emitter.ColorOverTime.AddKey(0.2f, {1.0f, 0.4f, 0.1f, 0.9f});
            emitter.ColorOverTime.AddKey(0.5f, {0.3f, 0.3f, 0.3f, 0.6f});
            emitter.ColorOverTime.AddKey(1.0f, {0.2f, 0.2f, 0.2f, 0.0f});
            
            emitter.SizeOverTime = {0.5f, 1.0f, 2.0f, 3.5f};
            
            return emitter;
        }

        /**
         * @brief Create a rain particle effect
         */
        static ParticleEmitterComponent CreateRain(float intensity = 1.0f) {
            ParticleEmitterComponent emitter;
            emitter.EmissionRate = 200.0f * intensity;
            emitter.MaxParticles = 2000;
            emitter.Lifetime = {1.0f, 2.0f};
            
            emitter.Shape = EmitterShape::Box;
            emitter.ShapeParams.BoxDimensions = {20.0f, 0.1f, 20.0f};
            
            emitter.Velocity.SpeedMin = 10.0f;
            emitter.Velocity.SpeedMax = 15.0f;
            emitter.Velocity.Direction = {0.1f, -1.0f, 0.0f};
            emitter.Velocity.ConeAngle = 5.0f;
            
            emitter.GravityModifier = 0.0f;  // Constant velocity
            
            emitter.ColorOverTime.ColorKeys.clear();
            emitter.ColorOverTime.AddKey(0.0f, {0.7f, 0.8f, 0.9f, 0.6f});
            emitter.ColorOverTime.AddKey(1.0f, {0.7f, 0.8f, 0.9f, 0.3f});
            
            emitter.SizeOverTime = {0.02f, 0.04f, 0.02f, 0.04f};
            emitter.UniformSize = false;  // Elongated droplets
            
            emitter.AlignToDirection = true;
            
            return emitter;
        }

        /**
         * @brief Create a magic/sparkle particle effect
         */
        static ParticleEmitterComponent CreateMagic(const Math::Vec3& color = {0.5f, 0.8f, 1.0f}) {
            ParticleEmitterComponent emitter;
            emitter.EmissionRate = 30.0f;
            emitter.MaxParticles = 100;
            emitter.Lifetime = {0.5f, 1.5f};
            
            emitter.Shape = EmitterShape::Sphere;
            emitter.ShapeParams.Radius = 0.5f;
            emitter.ShapeParams.RadiusThickness = 0.0f;  // Surface only
            
            emitter.Velocity.SpeedMin = 0.2f;
            emitter.Velocity.SpeedMax = 0.5f;
            emitter.Velocity.ConeAngle = 180.0f;
            
            emitter.Gravity = {0.0f, 0.5f, 0.0f};
            emitter.UseGlobalGravity = false;
            
            emitter.ColorOverTime.ColorKeys.clear();
            emitter.ColorOverTime.AddKey(0.0f, Math::Vec4(color, 0.0f));
            emitter.ColorOverTime.AddKey(0.2f, Math::Vec4(color * 1.2f, 1.0f));
            emitter.ColorOverTime.AddKey(0.8f, Math::Vec4(color, 0.8f));
            emitter.ColorOverTime.AddKey(1.0f, Math::Vec4(color * 0.5f, 0.0f));
            
            emitter.SizeOverTime = {0.1f, 0.2f, 0.05f, 0.1f};
            
            return emitter;
        }

        //---------------------------------------------------------------------
        // Helper Methods
        //---------------------------------------------------------------------

        /**
         * @brief Reset runtime state for replay
         */
        void Reset() {
            ElapsedTime = 0.0f;
            EmitAccumulator = 0.0f;
            BurstCycleCount = 0;
            BurstTimer = Burst.Time;
            WasPreWarmed = false;
            Playing = true;
        }

        /**
         * @brief Check if emitter has finished (non-looping only)
         */
        bool IsFinished() const {
            if (Looping) return false;
            return ElapsedTime >= Duration + Lifetime.y;  // Duration + max lifetime
        }

        /**
         * @brief Check if emitter is currently active
         */
        bool IsActive() const {
            return Enabled && Playing && !IsFinished();
        }

        /**
         * @brief Get emission rate considering burst mode
         */
        float GetEffectiveEmissionRate() const {
            return Playing ? EmissionRate : 0.0f;
        }

        /**
         * @brief Check if emitter has a valid system handle
         */
        bool HasSystemHandle() const {
            return EmitterHandle != UINT32_MAX;
        }

        /**
         * @brief Calculate estimated particle count for memory planning
         */
        uint32_t EstimateParticleCount() const {
            float avgLifetime = (Lifetime.x + Lifetime.y) * 0.5f;
            uint32_t continuous = static_cast<uint32_t>(EmissionRate * avgLifetime);
            uint32_t burst = Burst.Enabled ? Burst.Count : 0;
            return std::min(continuous + burst, MaxParticles);
        }
    };

} // namespace ECS
} // namespace Core
