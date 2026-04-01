#pragma once

#include "Core/Math/Math.h"
#include <string>
#include <cstdint>

namespace Core {
namespace ECS {

    /**
     * @brief Type of foliage for different rendering/behavior strategies
     */
    enum class FoliageType : uint32_t {
        Grass = 0,      // High density, simple billboards or cross-planes
        Shrub = 1,      // Medium density, small meshes
        Tree = 2,       // Low density, complex meshes with LOD
        Flower = 3,     // Medium density, colorful vegetation
        Rock = 4        // Static, no wind animation
    };

    /**
     * @brief Range structure for min/max values
     */
    struct FloatRange {
        float Min = 0.0f;
        float Max = 1.0f;

        FloatRange() = default;
        FloatRange(float min, float max) : Min(min), Max(max) {}

        float Lerp(float t) const { return Min + (Max - Min) * t; }
        float Clamp(float value) const { 
            return value < Min ? Min : (value > Max ? Max : value); 
        }
        bool Contains(float value) const { return value >= Min && value <= Max; }
    };

    /**
     * @brief Component for foliage scattering and rendering
     * 
     * Defines placement rules, visual properties, and animation parameters
     * for GPU-driven foliage instancing.
     */
    struct FoliageComponent {
        // Foliage classification
        FoliageType Type = FoliageType::Grass;

        // Density and distribution
        float Density = 10.0f;              // Instances per square meter
        FloatRange ScaleRange{0.8f, 1.2f};  // Random scale variation
        float RotationVariation = 360.0f;   // Random rotation in degrees

        // Wind animation parameters
        float WindStrength = 0.5f;          // Wind displacement amplitude
        float WindFrequency = 1.0f;         // Wind oscillation speed

        // Culling and LOD
        float CullDistance = 100.0f;        // Max distance for rendering
        float LODDistance1 = 25.0f;         // First LOD transition
        float LODDistance2 = 50.0f;         // Second LOD transition

        // Terrain alignment
        bool TerrainAligned = true;         // Align to terrain surface normal
        FloatRange HeightRange{0.0f, 1000.0f};  // Valid placement height range
        FloatRange SlopeRange{0.0f, 45.0f};     // Valid slope range (degrees)

        // Visual properties
        Math::Vec3 ColorVariation{0.1f, 0.15f, 0.1f};  // Random color tint range
        float AlphaCutoff = 0.5f;           // Alpha test threshold

        // Asset references
        std::string MeshPath;               // Path to foliage mesh asset
        std::string MaterialPath;           // Path to foliage material

        // Scatter region (local space bounds)
        Math::Vec3 ScatterMin{-50.0f, 0.0f, -50.0f};
        Math::Vec3 ScatterMax{50.0f, 0.0f, 50.0f};

        // Runtime state
        bool Enabled = true;
        bool NeedsRescatter = true;         // Flag to trigger GPU scatter

        FoliageComponent() = default;

        FoliageComponent(FoliageType type, float density)
            : Type(type), Density(density) 
        {
            // Set sensible defaults based on type
            switch (type) {
                case FoliageType::Grass:
                    Density = density;
                    ScaleRange = {0.8f, 1.2f};
                    CullDistance = 50.0f;
                    WindStrength = 0.3f;
                    break;
                case FoliageType::Shrub:
                    Density = density;
                    ScaleRange = {0.7f, 1.3f};
                    CullDistance = 75.0f;
                    WindStrength = 0.2f;
                    break;
                case FoliageType::Tree:
                    Density = density;
                    ScaleRange = {0.8f, 1.5f};
                    CullDistance = 200.0f;
                    WindStrength = 0.1f;
                    break;
                case FoliageType::Flower:
                    Density = density;
                    ScaleRange = {0.6f, 1.0f};
                    CullDistance = 40.0f;
                    WindStrength = 0.4f;
                    break;
                case FoliageType::Rock:
                    Density = density;
                    ScaleRange = {0.5f, 2.0f};
                    CullDistance = 150.0f;
                    WindStrength = 0.0f;  // Rocks don't animate
                    break;
            }
        }

        /**
         * @brief Calculate approximate instance count for a given area
         */
        uint32_t EstimateInstanceCount() const {
            float width = ScatterMax.x - ScatterMin.x;
            float depth = ScatterMax.z - ScatterMin.z;
            float area = width * depth;
            return static_cast<uint32_t>(area * Density);
        }

        /**
         * @brief Check if a point is valid for placement
         */
        bool IsValidPlacement(float height, float slopeDegrees) const {
            return HeightRange.Contains(height) && SlopeRange.Contains(slopeDegrees);
        }
    };

} // namespace ECS
} // namespace Core
