#include "TileCacheManager.h"
#include <DetourTileCache.h>
#include <DetourTileCacheBuilder.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <spdlog/spdlog.h>
#include <cstring>
#include <algorithm>

namespace Core {
namespace Navigation {

    // =========================================================================
    // Custom Allocator Implementation
    // =========================================================================

    class TileCacheManager::TileCacheAllocator : public dtTileCacheAlloc {
    public:
        void reset() override {
            // Simple implementation - no pooling
        }

        void* alloc(const size_t size) override {
            return dtAlloc(size, DT_ALLOC_PERM);
        }

        void free(void* ptr) override {
            dtFree(ptr);
        }
    };

    // =========================================================================
    // Custom Compressor Implementation (no compression for simplicity)
    // =========================================================================

    class TileCacheManager::TileCacheCompressor : public dtTileCacheCompressor {
    public:
        int maxCompressedSize(const int bufferSize) override {
            return bufferSize;
        }

        dtStatus compress(const unsigned char* buffer, const int bufferSize,
                          unsigned char* compressed, const int maxCompressedSize,
                          int* compressedSize) override {
            // No compression - just copy
            if (bufferSize > maxCompressedSize) {
                return DT_FAILURE;
            }
            std::memcpy(compressed, buffer, bufferSize);
            *compressedSize = bufferSize;
            return DT_SUCCESS;
        }

        dtStatus decompress(const unsigned char* compressed, const int compressedSize,
                            unsigned char* buffer, const int maxBufferSize,
                            int* bufferSize) override {
            // No compression - just copy
            if (compressedSize > maxBufferSize) {
                return DT_FAILURE;
            }
            std::memcpy(buffer, compressed, compressedSize);
            *bufferSize = compressedSize;
            return DT_SUCCESS;
        }
    };

    // =========================================================================
    // Custom Mesh Process Implementation
    // =========================================================================

    class TileCacheManager::TileCacheMeshProcess : public dtTileCacheMeshProcess {
    public:
        void process(struct dtNavMeshCreateParams* params,
                     unsigned char* polyAreas, unsigned short* polyFlags) override {
            // Set flags based on area types
            for (int i = 0; i < params->polyCount; ++i) {
                if (polyAreas[i] == DT_TILECACHE_WALKABLE_AREA) {
                    polyAreas[i] = AREA_GROUND;
                }

                switch (polyAreas[i]) {
                    case AREA_GROUND:
                        polyFlags[i] = FLAG_WALK;
                        break;
                    case AREA_WATER:
                        polyFlags[i] = FLAG_SWIM;
                        break;
                    case AREA_ROAD:
                        polyFlags[i] = FLAG_WALK;
                        break;
                    case AREA_GRASS:
                        polyFlags[i] = FLAG_WALK;
                        break;
                    case AREA_DOOR:
                        polyFlags[i] = FLAG_WALK | FLAG_DOOR;
                        break;
                    case AREA_JUMP:
                        polyFlags[i] = FLAG_WALK | FLAG_JUMP;
                        break;
                    case AREA_DISABLED:
                        polyFlags[i] = FLAG_DISABLED;
                        break;
                    default:
                        polyFlags[i] = FLAG_WALK;
                        break;
                }
            }
        }
    };

    // =========================================================================
    // TileCacheManager Implementation
    // =========================================================================

    TileCacheManager::TileCacheManager() {
        m_Allocator = std::make_unique<TileCacheAllocator>();
        m_Compressor = std::make_unique<TileCacheCompressor>();
        m_MeshProcess = std::make_unique<TileCacheMeshProcess>();
    }

    TileCacheManager::~TileCacheManager() {
        Shutdown();
    }

    bool TileCacheManager::Initialize(const NavMeshConfig& config,
                                       int32_t tileCountX, int32_t tileCountZ,
                                       const glm::vec3& boundsMin,
                                       const glm::vec3& boundsMax) {
        if (m_Initialized) {
            Shutdown();
        }

        m_Config = config;
        m_BoundsMin = boundsMin;
        m_BoundsMax = boundsMax;
        m_TileCountX = tileCountX;
        m_TileCountZ = tileCountZ;
        m_TileWidth = config.TileSize * config.CellSize;

        // Calculate max tiles
        int maxTiles = tileCountX * tileCountZ;
        int maxPolysPerTile = static_cast<int>(config.MaxPolysPerTile);

        // Create NavMesh
        dtNavMeshParams navMeshParams;
        std::memset(&navMeshParams, 0, sizeof(navMeshParams));
        navMeshParams.orig[0] = boundsMin.x;
        navMeshParams.orig[1] = boundsMin.y;
        navMeshParams.orig[2] = boundsMin.z;
        navMeshParams.tileWidth = m_TileWidth;
        navMeshParams.tileHeight = m_TileWidth;
        navMeshParams.maxTiles = std::min(maxTiles, static_cast<int>(config.MaxTiles));
        navMeshParams.maxPolys = maxPolysPerTile;

        m_NavMesh = dtAllocNavMesh();
        if (!m_NavMesh) {
            spdlog::error("[TileCacheManager] Failed to allocate NavMesh");
            return false;
        }

        dtStatus status = m_NavMesh->init(&navMeshParams);
        if (dtStatusFailed(status)) {
            dtFreeNavMesh(m_NavMesh);
            m_NavMesh = nullptr;
            spdlog::error("[TileCacheManager] Failed to init NavMesh");
            return false;
        }

        // Create TileCache
        dtTileCacheParams tileCacheParams;
        std::memset(&tileCacheParams, 0, sizeof(tileCacheParams));
        tileCacheParams.orig[0] = boundsMin.x;
        tileCacheParams.orig[1] = boundsMin.y;
        tileCacheParams.orig[2] = boundsMin.z;
        tileCacheParams.cs = config.CellSize;
        tileCacheParams.ch = config.CellHeight;
        tileCacheParams.width = static_cast<int>(config.TileSize);
        tileCacheParams.height = static_cast<int>(config.TileSize);
        tileCacheParams.walkableHeight = config.AgentHeight;
        tileCacheParams.walkableRadius = config.AgentRadius;
        tileCacheParams.walkableClimb = config.AgentMaxClimb;
        tileCacheParams.maxSimplificationError = config.EdgeMaxError;
        tileCacheParams.maxTiles = maxTiles;
        tileCacheParams.maxObstacles = 256; // Max dynamic obstacles

        m_TileCache = dtAllocTileCache();
        if (!m_TileCache) {
            dtFreeNavMesh(m_NavMesh);
            m_NavMesh = nullptr;
            spdlog::error("[TileCacheManager] Failed to allocate TileCache");
            return false;
        }

        status = m_TileCache->init(&tileCacheParams, m_Allocator.get(),
                                   m_Compressor.get(), m_MeshProcess.get());
        if (dtStatusFailed(status)) {
            dtFreeTileCache(m_TileCache);
            m_TileCache = nullptr;
            dtFreeNavMesh(m_NavMesh);
            m_NavMesh = nullptr;
            spdlog::error("[TileCacheManager] Failed to init TileCache");
            return false;
        }

        m_Initialized = true;
        spdlog::info("[TileCacheManager] Initialized with {}x{} tiles", tileCountX, tileCountZ);
        return true;
    }

    void TileCacheManager::Shutdown() {
        ClearAllObstacles();

        if (m_TileCache) {
            dtFreeTileCache(m_TileCache);
            m_TileCache = nullptr;
        }

        if (m_NavMesh) {
            dtFreeNavMesh(m_NavMesh);
            m_NavMesh = nullptr;
        }

        m_Initialized = false;
    }

    ObstacleResult TileCacheManager::AddBoxObstacle(const glm::vec3& center,
                                                     const glm::vec3& halfExtents,
                                                     float yaw) {
        ObstacleResult result;

        if (!m_TileCache) {
            result.ErrorMessage = "TileCache not initialized";
            return result;
        }

        float bmin[3] = {
            center.x - halfExtents.x,
            center.y,
            center.z - halfExtents.z
        };
        float bmax[3] = {
            center.x + halfExtents.x,
            center.y + halfExtents.y * 2.0f,
            center.z + halfExtents.z
        };

        dtObstacleRef obstacleRef;
        dtStatus status = m_TileCache->addBoxObstacle(bmin, bmax, &obstacleRef);

        if (dtStatusFailed(status)) {
            result.ErrorMessage = "Failed to add box obstacle";
            return result;
        }

        result.Success = true;
        result.ObstacleRef = static_cast<uint32_t>(obstacleRef);
        m_ActiveObstacles.push_back(result.ObstacleRef);

        return result;
    }

    ObstacleResult TileCacheManager::AddCylinderObstacle(const glm::vec3& center,
                                                          float radius, float height) {
        ObstacleResult result;

        if (!m_TileCache) {
            result.ErrorMessage = "TileCache not initialized";
            return result;
        }

        float pos[3] = { center.x, center.y, center.z };

        dtObstacleRef obstacleRef;
        dtStatus status = m_TileCache->addObstacle(pos, radius, height, &obstacleRef);

        if (dtStatusFailed(status)) {
            result.ErrorMessage = "Failed to add cylinder obstacle";
            return result;
        }

        result.Success = true;
        result.ObstacleRef = static_cast<uint32_t>(obstacleRef);
        m_ActiveObstacles.push_back(result.ObstacleRef);

        return result;
    }

    bool TileCacheManager::RemoveObstacle(uint32_t obstacleRef) {
        if (!m_TileCache) return false;

        dtStatus status = m_TileCache->removeObstacle(static_cast<dtObstacleRef>(obstacleRef));
        if (dtStatusSucceed(status)) {
            m_ActiveObstacles.erase(
                std::remove(m_ActiveObstacles.begin(), m_ActiveObstacles.end(), obstacleRef),
                m_ActiveObstacles.end()
            );
            return true;
        }
        return false;
    }

    bool TileCacheManager::UpdateObstaclePosition(uint32_t obstacleRef, const glm::vec3& newCenter) {
        // TileCache doesn't support moving obstacles directly
        // We need to remove and re-add
        if (!m_TileCache) return false;

        // Get obstacle info
        const dtTileCacheObstacle* obstacle = m_TileCache->getObstacleByRef(
            static_cast<dtObstacleRef>(obstacleRef));
        
        if (!obstacle) return false;

        // Remove old obstacle
        m_TileCache->removeObstacle(static_cast<dtObstacleRef>(obstacleRef));

        // Add new obstacle at new position
        float pos[3] = { newCenter.x, newCenter.y, newCenter.z };
        dtObstacleRef newRef;
        
        if (obstacle->type == DT_OBSTACLE_CYLINDER) {
            m_TileCache->addObstacle(pos, obstacle->radius, obstacle->height, &newRef);
        } else {
            // For box obstacles, approximate with cylinder
            m_TileCache->addObstacle(pos, obstacle->radius, obstacle->height, &newRef);
        }

        // Update tracked reference
        auto it = std::find(m_ActiveObstacles.begin(), m_ActiveObstacles.end(), obstacleRef);
        if (it != m_ActiveObstacles.end()) {
            *it = static_cast<uint32_t>(newRef);
        }

        return true;
    }

    void TileCacheManager::ClearAllObstacles() {
        if (!m_TileCache) return;

        for (uint32_t ref : m_ActiveObstacles) {
            m_TileCache->removeObstacle(static_cast<dtObstacleRef>(ref));
        }
        m_ActiveObstacles.clear();
    }

    void TileCacheManager::MarkDirty(const glm::vec3& center, float radius) {
        int32_t minX, minZ, maxX, maxZ;
        
        GetTileCoords(center - glm::vec3(radius, 0, radius), minX, minZ);
        GetTileCoords(center + glm::vec3(radius, 0, radius), maxX, maxZ);

        for (int32_t z = minZ; z <= maxZ; ++z) {
            for (int32_t x = minX; x <= maxX; ++x) {
                MarkTileDirty(x, z);
            }
        }
    }

    void TileCacheManager::MarkTileDirty(int32_t tileX, int32_t tileZ) {
        if (!m_TileCache || !m_NavMesh) return;

        // Rebuild the tile
        dtCompressedTileRef tileRef = m_TileCache->getTileRef(
            m_TileCache->getTileAt(tileX, tileZ, 0));
        
        if (tileRef) {
            m_TileCache->buildNavMeshTile(tileRef, m_NavMesh);
        }
    }

    int TileCacheManager::ProcessUpdates(int maxUpdates) {
        if (!m_TileCache || !m_NavMesh) return 0;

        bool updated = false;
        int count = 0;

        do {
            updated = m_TileCache->update(0.0f, m_NavMesh, &updated);
            if (updated) {
                ++count;
            }
        } while (updated && (maxUpdates == 0 || count < maxUpdates));

        return count;
    }

    bool TileCacheManager::HasPendingUpdates() const {
        return false; // TileCache handles this internally
    }

    int TileCacheManager::GetPendingUpdateCount() const {
        return 0; // TileCache handles this internally
    }

    dtNavMesh* TileCacheManager::GetNavMesh() {
        return m_NavMesh;
    }

    bool TileCacheManager::AddTileData(int32_t tileX, int32_t tileZ,
                                        const unsigned char* data, int dataSize) {
        if (!m_TileCache) return false;

        dtCompressedTileRef ref;
        dtStatus status = m_TileCache->addTile(data, dataSize, DT_COMPRESSEDTILE_FREE_DATA, &ref);
        
        if (dtStatusFailed(status)) {
            return false;
        }

        // Build the NavMesh tile
        m_TileCache->buildNavMeshTile(ref, m_NavMesh);
        return true;
    }

    void TileCacheManager::RemoveTile(int32_t tileX, int32_t tileZ) {
        if (!m_TileCache || !m_NavMesh) return;

        // Remove from TileCache
        dtCompressedTileRef refs[32];
        int count = m_TileCache->getTilesAt(tileX, tileZ, refs, 32);
        
        for (int i = 0; i < count; ++i) {
            m_TileCache->removeTile(refs[i], nullptr, nullptr);
        }

        // Remove from NavMesh
        dtTileRef tileRef = m_NavMesh->getTileRefAt(tileX, tileZ, 0);
        if (tileRef) {
            m_NavMesh->removeTile(tileRef, nullptr, nullptr);
        }
    }

    void TileCacheManager::GetTileCoords(const glm::vec3& position,
                                          int32_t& outX, int32_t& outZ) const {
        outX = static_cast<int32_t>((position.x - m_BoundsMin.x) / m_TileWidth);
        outZ = static_cast<int32_t>((position.z - m_BoundsMin.z) / m_TileWidth);

        // Clamp to valid range
        outX = std::clamp(outX, 0, m_TileCountX - 1);
        outZ = std::clamp(outZ, 0, m_TileCountZ - 1);
    }

} // namespace Navigation
} // namespace Core
