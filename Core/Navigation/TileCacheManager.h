#pragma once

#include "NavigationConfig.h"
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <set>
#include <cstdint>

// Forward declarations
struct dtTileCache;
struct dtTileCacheAlloc;
struct dtTileCacheCompressor;
struct dtTileCacheMeshProcess;
struct dtNavMesh;
struct dtNavMeshQuery;

namespace Core {
namespace Navigation {

    /// @brief Result of obstacle operations
    struct ObstacleResult {
        bool Success = false;
        uint32_t ObstacleRef = 0;
        std::string ErrorMessage;
    };

    /// @brief Dynamic NavMesh manager using DetourTileCache
    /// 
    /// Supports:
    /// - Dynamic obstacle addition/removal
    /// - Efficient tile updates
    /// - Obstacle carving
    class TileCacheManager {
    public:
        TileCacheManager();
        ~TileCacheManager();

        // =====================================================================
        // Lifecycle
        // =====================================================================

        /// @brief Initialize with configuration
        /// @param config NavMesh configuration
        /// @param tileCountX Number of tiles in X
        /// @param tileCountZ Number of tiles in Z
        /// @param boundsMin World bounds min
        /// @param boundsMax World bounds max
        /// @return True if successful
        bool Initialize(const NavMeshConfig& config,
                        int32_t tileCountX, int32_t tileCountZ,
                        const glm::vec3& boundsMin, const glm::vec3& boundsMax);

        /// @brief Shutdown and release resources
        void Shutdown();

        /// @brief Check if initialized
        bool IsInitialized() const { return m_Initialized; }

        // =====================================================================
        // Obstacle Management
        // =====================================================================

        /// @brief Add a box obstacle
        /// @param center Obstacle center
        /// @param halfExtents Half extents
        /// @param yaw Rotation around Y axis (radians)
        /// @return Obstacle result with handle
        ObstacleResult AddBoxObstacle(const glm::vec3& center,
                                       const glm::vec3& halfExtents,
                                       float yaw = 0.0f);

        /// @brief Add a cylinder obstacle
        /// @param center Obstacle center (base)
        /// @param radius Obstacle radius
        /// @param height Obstacle height
        /// @return Obstacle result with handle
        ObstacleResult AddCylinderObstacle(const glm::vec3& center,
                                            float radius, float height);

        /// @brief Remove an obstacle
        /// @param obstacleRef Obstacle reference from Add operation
        /// @return True if removed
        bool RemoveObstacle(uint32_t obstacleRef);

        /// @brief Update obstacle position
        /// @param obstacleRef Obstacle reference
        /// @param newCenter New center position
        /// @return True if updated
        bool UpdateObstaclePosition(uint32_t obstacleRef, const glm::vec3& newCenter);

        /// @brief Remove all obstacles
        void ClearAllObstacles();

        // =====================================================================
        // Tile Updates
        // =====================================================================

        /// @brief Mark tiles as dirty for rebuild
        /// @param center Center of affected area
        /// @param radius Radius of affected area
        void MarkDirty(const glm::vec3& center, float radius);

        /// @brief Mark a specific tile as dirty
        /// @param tileX Tile X coordinate
        /// @param tileZ Tile Z coordinate
        void MarkTileDirty(int32_t tileX, int32_t tileZ);

        /// @brief Process pending tile updates
        /// @param maxUpdates Maximum tiles to update (0 = all)
        /// @return Number of tiles updated
        int ProcessUpdates(int maxUpdates = 0);

        /// @brief Check if there are pending updates
        bool HasPendingUpdates() const;

        /// @brief Get number of pending tile updates
        int GetPendingUpdateCount() const;

        // =====================================================================
        // NavMesh Access
        // =====================================================================

        /// @brief Get the NavMesh
        dtNavMesh* GetNavMesh();

        /// @brief Get tile cache (for advanced usage)
        dtTileCache* GetTileCache() { return m_TileCache; }

        // =====================================================================
        // Tile Data
        // =====================================================================

        /// @brief Add pre-built tile data
        /// @param tileX Tile X coordinate
        /// @param tileZ Tile Z coordinate
        /// @param data Compressed tile data
        /// @param dataSize Data size
        /// @return True if added
        bool AddTileData(int32_t tileX, int32_t tileZ,
                         const unsigned char* data, int dataSize);

        /// @brief Remove tile
        /// @param tileX Tile X
        /// @param tileZ Tile Z
        void RemoveTile(int32_t tileX, int32_t tileZ);

        /// @brief Get tile coordinates for position
        void GetTileCoords(const glm::vec3& position, int32_t& outX, int32_t& outZ) const;

    private:
        /// @brief Custom allocator for TileCache
        class TileCacheAllocator;

        /// @brief Custom compressor for TileCache
        class TileCacheCompressor;

        /// @brief Custom mesh processor for TileCache
        class TileCacheMeshProcess;

    private:
        bool m_Initialized = false;
        NavMeshConfig m_Config;

        dtTileCache* m_TileCache = nullptr;
        dtNavMesh* m_NavMesh = nullptr;

        std::unique_ptr<TileCacheAllocator> m_Allocator;
        std::unique_ptr<TileCacheCompressor> m_Compressor;
        std::unique_ptr<TileCacheMeshProcess> m_MeshProcess;

        glm::vec3 m_BoundsMin;
        glm::vec3 m_BoundsMax;
        float m_TileWidth = 0.0f;
        int32_t m_TileCountX = 0;
        int32_t m_TileCountZ = 0;

        std::vector<uint32_t> m_ActiveObstacles;
    };

} // namespace Navigation
} // namespace Core
