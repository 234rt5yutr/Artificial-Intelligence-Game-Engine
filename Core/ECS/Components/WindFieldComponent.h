#pragma once

/**
 * @file WindFieldComponent.h
 * @brief ECS component for wind field simulation
 * 
 * Defines wind field types and behaviors for environmental effects such as
 * foliage animation, particle systems, cloth simulation, and audio occlusion.
 * Supports directional, point, vortex, and turbulent wind patterns with
 * configurable gust and turbulence overlays.
 */

#include "Core/Math/Math.h"
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace Core {
namespace ECS {

    //=========================================================================
    // Wind Field Types
    //=========================================================================

    /**
     * @brief Type of wind field behavior
     */
    enum class WindFieldType : uint32_t {
        Directional = 0,    // Uniform wind in a single direction (global/area)
        Point = 1,          // Wind emanating outward from a point source
        Vortex = 2,         // Swirling wind around an axis
        Turbulent = 3       // Chaotic, noise-based wind patterns
    };

    //=========================================================================
    // Wind Field Component
    //=========================================================================

    /**
     * @brief Component for wind field simulation and sampling
     * 
     * Wind fields can be attached to entities to create localized or global
     * wind effects. Multiple wind fields can overlap and their contributions
     * are typically summed by the wind system.
     * 
     * Usage examples:
     * - Directional: Global wind, fans, air vents
     * - Point: Explosions, air cannons, pressure releases
     * - Vortex: Tornadoes, whirlpools, spinning machinery
     * - Turbulent: Storms, chaotic environments
     */
    struct WindFieldComponent {
        //---------------------------------------------------------------------
        // Core Properties
        //---------------------------------------------------------------------
        
        WindFieldType Type = WindFieldType::Directional;
        
        /** @brief Primary wind direction (normalized for directional, axis for vortex) */
        Math::Vec3 Direction{1.0f, 0.0f, 0.0f};
        
        /** @brief Base wind strength in meters per second */
        float Strength = 5.0f;
        
        /** @brief Influence radius for point/vortex fields (meters) */
        float Radius = 10.0f;
        
        //---------------------------------------------------------------------
        // Turbulence Properties
        //---------------------------------------------------------------------
        
        /** @brief Frequency of turbulence noise sampling */
        float TurbulenceFrequency = 0.5f;
        
        /** @brief Amplitude multiplier for turbulence displacement */
        float TurbulenceAmplitude = 0.3f;
        
        //---------------------------------------------------------------------
        // Gust Properties
        //---------------------------------------------------------------------
        
        /** @brief Frequency of wind gusts (gusts per second) */
        float GustFrequency = 0.2f;
        
        /** @brief Strength multiplier during gusts (1.0 = no change) */
        float GustStrength = 1.5f;
        
        //---------------------------------------------------------------------
        // Falloff Properties
        //---------------------------------------------------------------------
        
        /** @brief Falloff exponent for point/vortex fields (1.0 = linear, 2.0 = quadratic) */
        float FalloffExponent = 1.0f;
        
        //---------------------------------------------------------------------
        // State
        //---------------------------------------------------------------------
        
        /** @brief Whether this wind field is currently active */
        bool IsActive = true;
        
        /** @brief Internal noise offset for turbulence variation between instances */
        float NoiseOffset = 0.0f;

        //=====================================================================
        // Constructors
        //=====================================================================

        WindFieldComponent() = default;

        WindFieldComponent(WindFieldType type, float strength)
            : Type(type), Strength(strength)
        {
        }

        //=====================================================================
        // Wind Sampling Methods
        //=====================================================================

        /**
         * @brief Sample wind velocity at a world position
         * @param worldPosition Position to sample wind at
         * @param entityPosition Position of the wind field entity (for point/vortex)
         * @param time Current simulation time for animation
         * @return Wind velocity vector at the sample point
         */
        Math::Vec3 GetWindAt(const Math::Vec3& worldPosition, 
                             const Math::Vec3& entityPosition, 
                             float time) const 
        {
            if (!IsActive) {
                return Math::Vec3(0.0f);
            }

            Math::Vec3 baseWind(0.0f);
            Math::Vec3 samplePoint = worldPosition - entityPosition;

            switch (Type) {
                case WindFieldType::Directional:
                    baseWind = CalculateDirectionalWind();
                    break;
                case WindFieldType::Point:
                    baseWind = CalculatePointWind(samplePoint);
                    break;
                case WindFieldType::Vortex:
                    baseWind = CalculateVortexWind(samplePoint);
                    break;
                case WindFieldType::Turbulent:
                    baseWind = CalculateDirectionalWind();
                    baseWind += CalculateTurbulence(samplePoint, time);
                    break;
            }

            // Apply turbulence overlay for non-turbulent types
            if (Type != WindFieldType::Turbulent && TurbulenceAmplitude > 0.0f) {
                baseWind += CalculateTurbulence(samplePoint, time);
            }

            // Apply gust modulation
            baseWind = ApplyGust(baseWind, time);

            return baseWind;
        }

        /**
         * @brief Calculate directional wind (uniform direction)
         * @return Wind velocity vector
         */
        Math::Vec3 CalculateDirectionalWind() const {
            return glm::normalize(Direction) * Strength;
        }

        /**
         * @brief Calculate point-source wind (radial outward)
         * @param samplePoint Local-space sample position relative to entity
         * @return Wind velocity vector
         */
        Math::Vec3 CalculatePointWind(const Math::Vec3& samplePoint) const {
            float distance = glm::length(samplePoint);
            
            if (distance < 0.001f || distance > Radius) {
                return Math::Vec3(0.0f);
            }

            // Normalized direction from center to sample point
            Math::Vec3 direction = samplePoint / distance;

            // Calculate falloff (1 at center, 0 at radius)
            float normalizedDist = distance / Radius;
            float falloff = std::pow(1.0f - normalizedDist, FalloffExponent);
            falloff = std::clamp(falloff, 0.0f, 1.0f);

            return direction * Strength * falloff;
        }

        /**
         * @brief Calculate vortex wind (swirling around axis)
         * @param samplePoint Local-space sample position relative to entity
         * @return Wind velocity vector
         */
        Math::Vec3 CalculateVortexWind(const Math::Vec3& samplePoint) const {
            // Project sample point onto plane perpendicular to vortex axis
            Math::Vec3 axis = glm::normalize(Direction);
            Math::Vec3 toPoint = samplePoint;
            
            // Remove component along axis
            float axisComponent = glm::dot(toPoint, axis);
            Math::Vec3 radialVector = toPoint - axis * axisComponent;
            
            float distance = glm::length(radialVector);
            
            if (distance < 0.001f || distance > Radius) {
                return Math::Vec3(0.0f);
            }

            // Tangent direction (perpendicular to both axis and radial)
            Math::Vec3 tangent = glm::normalize(glm::cross(axis, radialVector));

            // Calculate falloff
            float normalizedDist = distance / Radius;
            float falloff = std::pow(1.0f - normalizedDist, FalloffExponent);
            falloff = std::clamp(falloff, 0.0f, 1.0f);

            // Vortex strength peaks between center and edge
            float vortexProfile = normalizedDist * falloff * 2.0f;

            return tangent * Strength * vortexProfile;
        }

        /**
         * @brief Calculate turbulence displacement using noise
         * @param samplePoint Sample position for noise lookup
         * @param time Current time for animation
         * @return Turbulence displacement vector
         */
        Math::Vec3 CalculateTurbulence(const Math::Vec3& samplePoint, float time) const {
            if (TurbulenceAmplitude <= 0.0f) {
                return Math::Vec3(0.0f);
            }

            // Simple pseudo-noise using sine waves at different frequencies
            // In production, replace with proper Perlin/Simplex noise
            float offsetTime = time + NoiseOffset;
            float freq = TurbulenceFrequency;
            
            float noiseX = std::sin(samplePoint.x * freq + offsetTime * 1.3f) *
                           std::cos(samplePoint.z * freq * 0.7f + offsetTime * 0.9f);
            
            float noiseY = std::sin(samplePoint.y * freq * 0.8f + offsetTime * 1.1f) *
                           std::cos(samplePoint.x * freq * 0.5f + offsetTime * 0.7f);
            
            float noiseZ = std::sin(samplePoint.z * freq + offsetTime * 0.8f) *
                           std::cos(samplePoint.y * freq * 0.6f + offsetTime * 1.2f);

            return Math::Vec3(noiseX, noiseY, noiseZ) * TurbulenceAmplitude * Strength;
        }

        /**
         * @brief Apply gust modulation to base wind
         * @param baseWind Wind velocity before gust application
         * @param time Current time for animation
         * @return Wind velocity with gust applied
         */
        Math::Vec3 ApplyGust(const Math::Vec3& baseWind, float time) const {
            if (GustFrequency <= 0.0f || GustStrength <= 1.0f) {
                return baseWind;
            }

            // Gust envelope using smooth step-like function
            float gustPhase = time * GustFrequency + NoiseOffset;
            float gustWave = std::sin(gustPhase * 6.28318f);  // 2*PI
            
            // Convert sine to gust envelope (peaks above 0.5)
            float gustEnvelope = std::max(0.0f, gustWave * 2.0f - 1.0f);
            gustEnvelope = gustEnvelope * gustEnvelope;  // Sharpen peaks
            
            // Interpolate between base strength and gust strength
            float gustMultiplier = 1.0f + gustEnvelope * (GustStrength - 1.0f);

            return baseWind * gustMultiplier;
        }

        //=====================================================================
        // Property Setters (Fluent Interface)
        //=====================================================================

        /**
         * @brief Set wind direction
         * @param dir Direction vector (will be normalized internally)
         * @return Reference to this component for chaining
         */
        WindFieldComponent& SetDirection(const Math::Vec3& dir) {
            Direction = glm::length(dir) > 0.001f ? glm::normalize(dir) : Math::Vec3(1.0f, 0.0f, 0.0f);
            return *this;
        }

        /**
         * @brief Set wind strength
         * @param s Strength in meters per second
         * @return Reference to this component for chaining
         */
        WindFieldComponent& SetStrength(float s) {
            Strength = std::max(0.0f, s);
            return *this;
        }

        /**
         * @brief Set influence radius
         * @param r Radius in meters
         * @return Reference to this component for chaining
         */
        WindFieldComponent& SetRadius(float r) {
            Radius = std::max(0.1f, r);
            return *this;
        }

        /**
         * @brief Set turbulence parameters
         * @param frequency Noise sampling frequency
         * @param amplitude Displacement amplitude multiplier
         * @return Reference to this component for chaining
         */
        WindFieldComponent& SetTurbulence(float frequency, float amplitude) {
            TurbulenceFrequency = std::max(0.0f, frequency);
            TurbulenceAmplitude = std::max(0.0f, amplitude);
            return *this;
        }

        /**
         * @brief Set gust parameters
         * @param frequency Gusts per second
         * @param strength Peak strength multiplier
         * @return Reference to this component for chaining
         */
        WindFieldComponent& SetGust(float frequency, float strength) {
            GustFrequency = std::max(0.0f, frequency);
            GustStrength = std::max(1.0f, strength);
            return *this;
        }

        /**
         * @brief Set falloff exponent for radial fields
         * @param exponent Falloff power (1.0 = linear, 2.0 = quadratic)
         * @return Reference to this component for chaining
         */
        WindFieldComponent& SetFalloffExponent(float exponent) {
            FalloffExponent = std::max(0.1f, exponent);
            return *this;
        }

        /**
         * @brief Set active state
         * @param active Whether wind field is active
         * @return Reference to this component for chaining
         */
        WindFieldComponent& SetActive(bool active) {
            IsActive = active;
            return *this;
        }

        //=====================================================================
        // Static Factory Methods
        //=====================================================================

        /**
         * @brief Create a directional wind field
         * @param direction Wind direction (will be normalized)
         * @param strength Wind speed in meters per second
         * @return Configured WindFieldComponent
         */
        static WindFieldComponent CreateDirectional(const Math::Vec3& direction, float strength) {
            WindFieldComponent component;
            component.Type = WindFieldType::Directional;
            component.Direction = glm::length(direction) > 0.001f ? 
                                  glm::normalize(direction) : Math::Vec3(1.0f, 0.0f, 0.0f);
            component.Strength = strength;
            component.TurbulenceAmplitude = 0.1f;  // Light turbulence by default
            component.GustFrequency = 0.15f;
            component.GustStrength = 1.3f;
            return component;
        }

        /**
         * @brief Create a point source wind field (radial outward)
         * @param strength Wind speed at center in meters per second
         * @param radius Influence radius in meters
         * @return Configured WindFieldComponent
         */
        static WindFieldComponent CreatePointSource(float strength, float radius) {
            WindFieldComponent component;
            component.Type = WindFieldType::Point;
            component.Strength = strength;
            component.Radius = radius;
            component.FalloffExponent = 1.5f;  // Slightly faster than linear falloff
            component.TurbulenceAmplitude = 0.0f;  // No turbulence by default
            component.GustStrength = 1.0f;  // No gusts by default
            return component;
        }

        /**
         * @brief Create a vortex wind field (swirling)
         * @param axis Rotation axis of the vortex
         * @param strength Tangential wind speed in meters per second
         * @param radius Influence radius in meters
         * @return Configured WindFieldComponent
         */
        static WindFieldComponent CreateVortex(const Math::Vec3& axis, float strength, float radius) {
            WindFieldComponent component;
            component.Type = WindFieldType::Vortex;
            component.Direction = glm::length(axis) > 0.001f ? 
                                  glm::normalize(axis) : Math::Vec3(0.0f, 1.0f, 0.0f);
            component.Strength = strength;
            component.Radius = radius;
            component.FalloffExponent = 1.0f;
            component.TurbulenceAmplitude = 0.05f;  // Very light turbulence
            component.GustStrength = 1.0f;
            return component;
        }

        /**
         * @brief Create a turbulent wind field
         * @param direction Primary wind direction
         * @param strength Base wind speed in meters per second
         * @param turbulence Turbulence intensity (0.0 - 1.0 typical)
         * @return Configured WindFieldComponent
         */
        static WindFieldComponent CreateTurbulent(const Math::Vec3& direction, 
                                                   float strength, 
                                                   float turbulence) {
            WindFieldComponent component;
            component.Type = WindFieldType::Turbulent;
            component.Direction = glm::length(direction) > 0.001f ? 
                                  glm::normalize(direction) : Math::Vec3(1.0f, 0.0f, 0.0f);
            component.Strength = strength;
            component.TurbulenceFrequency = 0.8f;
            component.TurbulenceAmplitude = std::clamp(turbulence, 0.0f, 2.0f);
            component.GustFrequency = 0.3f;
            component.GustStrength = 1.8f;
            return component;
        }
    };

} // namespace ECS
} // namespace Core
