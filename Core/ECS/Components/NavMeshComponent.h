#pragma once

#include "Core/Navigation/NavigationConfig.h"
#include <glm/glm.hpp>
#include <cstdint>

namespace Core {
namespace ECS {

    /// @brief Component for navigation mesh contribution
    /// 
    /// Entities with this component contribute to NavMesh generation.
    /// The area type determines traversal cost for pathfinding.
    struct NavMeshComponent {
        /// Area type for this surface (AREA_GROUND, AREA_WATER, etc.)
        uint8_t AreaType = Navigation::AREA_GROUND;

        /// Navigation flags (FLAG_WALK, FLAG_SWIM, etc.)
        uint16_t Flags = Navigation::FLAG_WALK;

        /// Whether this entity should be included in NavMesh
        bool IncludeInNavMesh = true;

        /// Whether this is a walkable surface
        bool Walkable = true;

        /// Cost modifier for traversing this area (1.0 = normal)
        float CostModifier = 1.0f;
    };

    /// @brief Component for off-mesh connections (jumps, ladders, teleports)
    struct OffMeshLinkComponent {
        /// Start position (local to entity)
        glm::vec3 StartOffset = glm::vec3(0.0f);

        /// End position (world space or relative to target entity)
        glm::vec3 EndPosition = glm::vec3(0.0f);

        /// Connection radius
        float Radius = 0.5f;

        /// Whether traversable in both directions
        bool Bidirectional = true;

        /// Area type for the link
        uint8_t AreaType = Navigation::AREA_JUMP;

        /// Navigation flags
        uint16_t Flags = Navigation::FLAG_WALK;

        /// Whether the link is currently active
        bool Enabled = true;

        /// Traversal time in seconds (for animations)
        float TraversalTime = 0.5f;
    };

    /// @brief Component marking entities as navigation obstacles
    struct NavMeshObstacleComponent {
        /// Obstacle shape
        enum class Shape : uint8_t {
            Box,
            Cylinder
        };

        /// Shape type
        Shape ObstacleShape = Shape::Box;

        /// Half extents for box or radius for cylinder
        glm::vec3 Size = glm::vec3(1.0f);

        /// Height (for cylinder)
        float Height = 2.0f;

        /// Whether the obstacle carves the NavMesh
        bool Carves = true;

        /// Carving move threshold (only re-carve if moved more than this)
        float MoveThreshold = 0.1f;

        /// Time to wait before carving after movement stops
        float CarveDelay = 0.2f;

        /// Internal: obstacle handle in TileCache
        uint32_t ObstacleHandle = 0;

        /// Internal: last position for movement tracking
        glm::vec3 LastPosition = glm::vec3(0.0f);
    };

    /// @brief Component marking an entity's influence on NavMesh building
    struct NavMeshModifierComponent {
        /// Override area type within bounds
        bool OverrideAreaType = false;
        uint8_t NewAreaType = Navigation::AREA_GROUND;

        /// Affect area within these bounds (local space)
        glm::vec3 BoundsMin = glm::vec3(-1.0f);
        glm::vec3 BoundsMax = glm::vec3(1.0f);

        /// Cost multiplier for this region
        float CostMultiplier = 1.0f;

        /// Whether to include in calculations
        bool Enabled = true;
    };

} // namespace ECS
} // namespace Core
