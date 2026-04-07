#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace Core {
namespace Navigation {

    // =========================================================================
    // Area Type Constants
    // =========================================================================
    
    /// Area types define the movement cost and rules for different terrain
    constexpr uint8_t AREA_GROUND = 0;      ///< Normal walkable ground
    constexpr uint8_t AREA_WATER = 1;       ///< Shallow water (wadeable)
    constexpr uint8_t AREA_ROAD = 2;        ///< Fast travel paths
    constexpr uint8_t AREA_GRASS = 3;       ///< Slightly slower movement
    constexpr uint8_t AREA_DOOR = 4;        ///< Doors (can be blocked)
    constexpr uint8_t AREA_JUMP = 5;        ///< Jump-required traversal
    constexpr uint8_t AREA_DISABLED = 63;   ///< Non-walkable (max area type)

    // =========================================================================
    // Navigation Flags
    // =========================================================================

    /// Flags for filtering which areas an agent can traverse
    constexpr uint16_t FLAG_WALK = 0x01;    ///< Can walk on ground
    constexpr uint16_t FLAG_SWIM = 0x02;    ///< Can swim in water
    constexpr uint16_t FLAG_DOOR = 0x04;    ///< Can open doors
    constexpr uint16_t FLAG_JUMP = 0x08;    ///< Can use jump links
    constexpr uint16_t FLAG_FLY = 0x10;     ///< Can fly (ignores NavMesh)
    constexpr uint16_t FLAG_ALL = 0xFFFF;   ///< All abilities enabled

    // =========================================================================
    // Area Cost Multipliers
    // =========================================================================

    /// Default cost multipliers for each area type
    constexpr float COST_GROUND = 1.0f;
    constexpr float COST_WATER = 10.0f;
    constexpr float COST_ROAD = 0.5f;
    constexpr float COST_GRASS = 1.2f;
    constexpr float COST_DOOR = 1.5f;
    constexpr float COST_JUMP = 2.0f;

    // =========================================================================
    // NavMesh Configuration
    // =========================================================================

    /// Configuration for NavMesh generation
    struct NavMeshConfig {
        // Voxelization parameters
        float CellSize = 0.3f;              ///< XZ voxel size (smaller = more detail)
        float CellHeight = 0.2f;            ///< Y voxel size

        // Agent parameters
        float AgentHeight = 2.0f;           ///< Default agent height
        float AgentRadius = 0.6f;           ///< Default agent radius
        float AgentMaxClimb = 0.4f;         ///< Max step height
        float AgentMaxSlope = 45.0f;        ///< Max walkable slope (degrees)

        // Tile parameters (for large worlds)
        float TileSize = 48.0f;             ///< Tile size in world units
        uint32_t MaxTiles = 1024;           ///< Maximum tile count
        uint32_t MaxPolysPerTile = 4096;    ///< Max polygons per tile

        // Region parameters
        float RegionMinSize = 8.0f;         ///< Min region area
        float RegionMergeSize = 20.0f;      ///< Region merge threshold

        // Edge parameters
        float EdgeMaxLen = 12.0f;           ///< Max edge length
        float EdgeMaxError = 1.3f;          ///< Simplification error

        // Detail mesh parameters
        float DetailSampleDist = 6.0f;      ///< Detail mesh sample distance
        float DetailSampleMaxError = 1.0f;  ///< Detail mesh max error
        bool BuildDetailMesh = true;        ///< Generate height detail

        // Filtering options
        bool FilterLedgeSpans = true;       ///< Remove ledge spans
        bool FilterWalkableLowHeightSpans = true;
        bool FilterLowHangingObstacles = true;

        // Debug options
        bool EnableDebugDraw = false;       ///< Enable debug visualization

        /// Get default configuration for a standard humanoid agent
        static NavMeshConfig Default() { return NavMeshConfig{}; }

        /// Get configuration for smaller agents (e.g., children, small creatures)
        static NavMeshConfig Small() {
            NavMeshConfig config;
            config.AgentHeight = 1.2f;
            config.AgentRadius = 0.3f;
            config.AgentMaxClimb = 0.25f;
            return config;
        }

        /// Get configuration for larger agents (e.g., vehicles, large creatures)
        static NavMeshConfig Large() {
            NavMeshConfig config;
            config.AgentHeight = 4.0f;
            config.AgentRadius = 1.5f;
            config.AgentMaxClimb = 0.8f;
            config.TileSize = 96.0f;
            return config;
        }
    };

    // =========================================================================
    // NavMesh Build Result
    // =========================================================================

    /// Result of NavMesh build operation
    struct NavMeshBuildResult {
        bool Success = false;               ///< Whether build succeeded
        std::string ErrorMessage;           ///< Error message if failed
        uint32_t TilesBuilt = 0;            ///< Number of tiles built
        uint32_t PolygonCount = 0;          ///< Total polygon count
        float BuildTimeMs = 0.0f;           ///< Build time in milliseconds
        size_t MemoryUsedBytes = 0;         ///< Memory used by NavMesh
    };

    // =========================================================================
    // Path Query Result
    // =========================================================================

    /// Result of a path query
    struct PathQueryResult {
        bool Found = false;                 ///< Whether a path was found
        std::vector<glm::vec3> Path;        ///< Smoothed path points
        float PathLength = 0.0f;            ///< Total path length
        uint32_t PolygonCount = 0;          ///< Number of polygons traversed
    };

    // =========================================================================
    // Crowd Configuration
    // =========================================================================

    /// Configuration for crowd simulation
    struct CrowdConfig {
        uint32_t MaxAgents = 256;                       ///< Maximum simultaneous agents
        float MaxAgentRadius = 2.0f;                    ///< Largest agent radius
        float UpdateDelta = 1.0f / 60.0f;               ///< Target update rate
        bool UseParallelUpdate = true;                  ///< Use JobSystem for updates
        uint32_t VelocitySampleCount = 6;               ///< RVO velocity samples
        float PathQueryTimeSlice = 2.0f;                ///< Max ms per frame for path queries
        float ObstacleAvoidanceTimeHorizon = 2.5f;      ///< Seconds to look ahead
        float ObstacleAvoidanceTimeHorizonObst = 1.5f;  ///< For static obstacles
        float CollisionQueryRange = 12.0f;              ///< Neighbor query range
    };

    // =========================================================================
    // Agent State
    // =========================================================================

    /// State of a navigation agent
    enum class AgentState : uint8_t {
        Idle,           ///< Not moving
        Moving,         ///< Following path
        Arriving,       ///< Slowing down near goal
        Stuck,          ///< Cannot make progress
        OffMesh,        ///< Traversing off-mesh connection
        Waiting         ///< At patrol waypoint
    };

    /// Path following mode
    enum class PathFollowMode : uint8_t {
        Strict,         ///< Follow path exactly
        Smooth,         ///< Smooth corners
        Optimistic      ///< Cut corners when possible
    };

    /// Avoidance quality level
    enum class AvoidanceQuality : uint8_t {
        None = 0,       ///< No avoidance
        Low = 1,        ///< 4 sample directions
        Medium = 2,     ///< 8 sample directions
        High = 3        ///< 16 sample directions
    };

    // =========================================================================
    // Off-Mesh Connection
    // =========================================================================

    /// Off-mesh connection (jump link, ladder, teleporter)
    struct OffMeshConnection {
        glm::vec3 StartPos{0, 0, 0};    ///< Start position
        glm::vec3 EndPos{0, 0, 0};      ///< End position
        float Radius = 0.6f;             ///< Connection radius
        bool Bidirectional = false;      ///< Can traverse both ways
        uint8_t AreaType = AREA_JUMP;    ///< Area type for this connection
        uint16_t Flags = FLAG_JUMP;      ///< Required flags
        uint32_t ConnectionId = 0;       ///< Unique ID
    };

    // =========================================================================
    // Patrol Route
    // =========================================================================

    /// Single waypoint in a patrol route
    struct PatrolWaypoint {
        glm::vec3 Position{0, 0, 0};    ///< Waypoint position
        float WaitTime = 0.0f;           ///< Seconds to wait at waypoint
        std::string Action;              ///< Optional action to trigger
    };

    /// Patrol route for an agent
    struct PatrolRoute {
        std::string RouteId;                        ///< Unique route identifier
        std::vector<PatrolWaypoint> Waypoints;      ///< Waypoint sequence
        bool Loop = true;                           ///< Loop back to start
        bool PingPong = false;                      ///< Reverse at end instead of looping
        uint32_t CurrentWaypointIndex = 0;          ///< Current waypoint
        float WaitTimer = 0.0f;                     ///< Current wait time remaining
    };

    // =========================================================================
    // NavMesh Statistics
    // =========================================================================

    /// Statistics about the NavMesh
    struct NavMeshStats {
        uint32_t TileCount = 0;         ///< Number of tiles
        uint32_t PolygonCount = 0;      ///< Total polygons
        uint32_t VertexCount = 0;       ///< Total vertices
        size_t MemoryUsedBytes = 0;     ///< Memory usage
        float LastRebuildTimeMs = 0.0f; ///< Last rebuild duration
    };

    /// Statistics about the crowd system
    struct CrowdStats {
        uint32_t ActiveAgentCount = 0;      ///< Number of active agents
        uint32_t MaxActiveAgents = 0;       ///< Peak agent count
        float LastUpdateTimeMs = 0.0f;      ///< Last update duration
        uint32_t PathQueriesThisFrame = 0;  ///< Path queries this frame
        uint32_t VelocityUpdatesThisFrame = 0; ///< Velocity updates this frame
    };

} // namespace Navigation
} // namespace Core
