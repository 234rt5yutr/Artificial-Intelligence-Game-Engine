#pragma once

#include "NavigationConfig.h"
#include "NavMeshBuilder.h"
#include <memory>
#include <mutex>
#include <future>
#include <set>
#include <glm/glm.hpp>

// Forward declarations
struct dtNavMesh;
struct dtNavMeshQuery;
struct dtQueryFilter;

namespace Core {

namespace ECS {
    class Scene;
}

namespace Navigation {

    /// @brief Singleton managing all NavMesh operations
    /// 
    /// NavMeshManager handles:
    /// - NavMesh building and lifecycle
    /// - Path queries
    /// - Tile management
    /// - Off-mesh connections
    /// - Debug visualization
    class NavMeshManager {
    public:
        static NavMeshManager& Get();

        // =====================================================================
        // Lifecycle
        // =====================================================================

        /// @brief Initialize the NavMesh system
        /// @param config NavMesh configuration
        /// @return True if initialization succeeded
        bool Initialize(const NavMeshConfig& config = NavMeshConfig{});

        /// @brief Shutdown the NavMesh system
        void Shutdown();

        /// @brief Check if the system is initialized
        bool IsInitialized() const { return m_Initialized; }

        // =====================================================================
        // NavMesh Building
        // =====================================================================

        /// @brief Build NavMesh from scene geometry
        /// @param scene Scene to build from
        /// @return Build result
        NavMeshBuildResult BuildFromScene(ECS::Scene* scene);

        /// @brief Build NavMesh from collider data
        /// @param colliders Collider data array
        /// @return Build result
        NavMeshBuildResult BuildFromColliders(const std::vector<ColliderData>& colliders);

        /// @brief Rebuild a single tile
        /// @param tileX Tile X coordinate
        /// @param tileZ Tile Z coordinate
        /// @return Build result
        NavMeshBuildResult RebuildTile(int32_t tileX, int32_t tileZ);

        /// @brief Build NavMesh asynchronously
        /// @param scene Scene to build from
        /// @return Future for build result
        std::future<NavMeshBuildResult> BuildFromSceneAsync(ECS::Scene* scene);

        // =====================================================================
        // NavMesh Access
        // =====================================================================

        /// @brief Get the NavMesh (internal use)
        dtNavMesh* GetNavMesh();

        /// @brief Get the NavMesh query object
        dtNavMeshQuery* GetNavMeshQuery();

        /// @brief Check if NavMesh data exists
        bool HasNavMeshData() const;

        // =====================================================================
        // Path Queries
        // =====================================================================

        /// @brief Find a path between two points
        /// @param start Start position
        /// @param end End position
        /// @param filter Optional query filter
        /// @return Path query result
        PathQueryResult FindPath(const glm::vec3& start, const glm::vec3& end,
                                 const dtQueryFilter* filter = nullptr);

        /// @brief Find the nearest point on the NavMesh
        /// @param position Query position
        /// @param extents Search extents
        /// @return Nearest point on NavMesh
        glm::vec3 FindNearestPoint(const glm::vec3& position,
                                    const glm::vec3& extents = glm::vec3(2.0f, 4.0f, 2.0f));

        /// @brief Check if a point is on the NavMesh
        /// @param position Query position
        /// @param extents Search extents
        /// @return True if point is on NavMesh
        bool IsPointOnNavMesh(const glm::vec3& position,
                              const glm::vec3& extents = glm::vec3(0.5f, 2.0f, 0.5f));

        /// @brief Get distance from point to NavMesh
        /// @param position Query position
        /// @return Distance to NavMesh (0 if on mesh)
        float GetDistanceToNavMesh(const glm::vec3& position);

        /// @brief Raycast on NavMesh
        /// @param start Ray start
        /// @param end Ray end
        /// @param hitPoint Output hit point
        /// @param hitNormal Output hit normal
        /// @return True if ray hits NavMesh
        bool Raycast(const glm::vec3& start, const glm::vec3& end,
                     glm::vec3& hitPoint, glm::vec3& hitNormal);

        // =====================================================================
        // Off-Mesh Connections
        // =====================================================================

        /// @brief Add an off-mesh connection
        /// @param start Start position
        /// @param end End position
        /// @param radius Connection radius
        /// @param bidirectional Allow traversal both ways
        /// @param areaType Area type for connection
        /// @return Connection ID
        uint32_t AddOffMeshConnection(const glm::vec3& start, const glm::vec3& end,
                                       float radius, bool bidirectional = false,
                                       uint8_t areaType = AREA_JUMP);

        /// @brief Remove an off-mesh connection
        /// @param connectionId Connection ID to remove
        void RemoveOffMeshConnection(uint32_t connectionId);

        /// @brief Clear all off-mesh connections
        void ClearOffMeshConnections();

        // =====================================================================
        // Tile Management
        // =====================================================================

        /// @brief Get tile coordinates for a position
        /// @param position World position
        /// @param outX Output tile X
        /// @param outZ Output tile Z
        void GetTileAt(const glm::vec3& position, int32_t& outX, int32_t& outZ);

        /// @brief Mark a tile as dirty (needs rebuild)
        /// @param tileX Tile X coordinate
        /// @param tileZ Tile Z coordinate
        void MarkTileDirty(int32_t tileX, int32_t tileZ);

        /// @brief Mark all tiles in radius as dirty
        /// @param center Center position
        /// @param radius Radius
        void MarkTilesDirtyInRadius(const glm::vec3& center, float radius);

        /// @brief Rebuild all dirty tiles
        void RebuildDirtyTiles();

        /// @brief Check if any tiles are dirty
        bool HasDirtyTiles() const;

        // =====================================================================
        // Configuration
        // =====================================================================

        /// @brief Get current configuration
        const NavMeshConfig& GetConfig() const { return m_Config; }

        /// @brief Set default query filter
        /// @param filter Query filter
        void SetQueryFilter(const dtQueryFilter& filter);

        // =====================================================================
        // Debug Visualization
        // =====================================================================

        /// @brief Enable/disable debug drawing
        /// @param enabled Enable flag
        void SetDebugDrawEnabled(bool enabled) { m_DebugDrawEnabled = enabled; }

        /// @brief Check if debug drawing is enabled
        bool IsDebugDrawEnabled() const { return m_DebugDrawEnabled; }

        // =====================================================================
        // Statistics
        // =====================================================================

        /// @brief Get NavMesh statistics
        NavMeshStats GetStats() const;

    private:
        NavMeshManager() = default;
        ~NavMeshManager() = default;

        // Delete copy/move
        NavMeshManager(const NavMeshManager&) = delete;
        NavMeshManager& operator=(const NavMeshManager&) = delete;

        /// @brief Initialize query filter with default costs
        void InitializeQueryFilter();

    private:
        bool m_Initialized = false;
        NavMeshConfig m_Config;

        dtNavMesh* m_NavMesh = nullptr;
        dtNavMeshQuery* m_NavMeshQuery = nullptr;
        std::unique_ptr<NavMeshBuilder> m_Builder;
        std::unique_ptr<dtQueryFilter> m_DefaultFilter;

        std::set<std::pair<int32_t, int32_t>> m_DirtyTiles;
        std::vector<OffMeshConnection> m_OffMeshConnections;
        uint32_t m_NextConnectionId = 1;

        mutable std::mutex m_Mutex;
        bool m_DebugDrawEnabled = false;

        // Scene reference for async rebuild
        ECS::Scene* m_CurrentScene = nullptr;
    };

} // namespace Navigation
} // namespace Core
