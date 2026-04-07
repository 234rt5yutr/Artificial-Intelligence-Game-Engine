#include "NavMeshManager.h"
#include "NavMeshBuilder.h"
#include "../ECS/Scene.h"
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <DetourCommon.h>
#include <spdlog/spdlog.h>
#include <algorithm>

namespace Core {
namespace Navigation {

    NavMeshManager& NavMeshManager::Get() {
        static NavMeshManager instance;
        return instance;
    }

    bool NavMeshManager::Initialize(const NavMeshConfig& config) {
        std::lock_guard<std::mutex> lock(m_Mutex);

        if (m_Initialized) {
            spdlog::warn("[NavMeshManager] Already initialized");
            return true;
        }

        m_Config = config;

        // Create NavMesh query
        m_NavMeshQuery = dtAllocNavMeshQuery();
        if (!m_NavMeshQuery) {
            spdlog::error("[NavMeshManager] Failed to allocate NavMesh query");
            return false;
        }

        // Create builder
        m_Builder = std::make_unique<NavMeshBuilder>();
        m_Builder->SetConfig(config);

        // Initialize default filter
        InitializeQueryFilter();

        m_Initialized = true;
        spdlog::info("[NavMeshManager] Initialized");
        return true;
    }

    void NavMeshManager::Shutdown() {
        std::lock_guard<std::mutex> lock(m_Mutex);

        if (!m_Initialized) {
            return;
        }

        m_DefaultFilter.reset();
        m_Builder.reset();
        m_OffMeshConnections.clear();
        m_DirtyTiles.clear();

        if (m_NavMeshQuery) {
            dtFreeNavMeshQuery(m_NavMeshQuery);
            m_NavMeshQuery = nullptr;
        }

        if (m_NavMesh) {
            dtFreeNavMesh(m_NavMesh);
            m_NavMesh = nullptr;
        }

        m_Initialized = false;
        spdlog::info("[NavMeshManager] Shutdown complete");
    }

    void NavMeshManager::InitializeQueryFilter() {
        m_DefaultFilter = std::make_unique<dtQueryFilter>();

        // Set area costs
        m_DefaultFilter->setAreaCost(AREA_GROUND, 1.0f);
        m_DefaultFilter->setAreaCost(AREA_WATER, 10.0f);
        m_DefaultFilter->setAreaCost(AREA_ROAD, 0.5f);  // Prefer roads
        m_DefaultFilter->setAreaCost(AREA_GRASS, 1.5f);
        m_DefaultFilter->setAreaCost(AREA_DOOR, 1.0f);
        m_DefaultFilter->setAreaCost(AREA_JUMP, 1.5f);

        // Include flags
        m_DefaultFilter->setIncludeFlags(FLAG_WALK | FLAG_DOOR);
        m_DefaultFilter->setExcludeFlags(FLAG_DISABLED);
    }

    NavMeshBuildResult NavMeshManager::BuildFromScene(ECS::Scene* scene) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        NavMeshBuildResult result;

        if (!m_Initialized) {
            result.success = false;
            result.errorMessage = "NavMeshManager not initialized";
            return result;
        }

        if (!scene) {
            result.success = false;
            result.errorMessage = "Scene is null";
            return result;
        }

        // Store scene for future rebuilds
        m_CurrentScene = scene;

        // Collect geometry
        m_Builder->CollectGeometry(scene);

        // Build NavMesh
        result = m_Builder->Build();
        if (!result.success) {
            return result;
        }

        // Free old NavMesh
        if (m_NavMesh) {
            dtFreeNavMesh(m_NavMesh);
        }

        // Transfer ownership
        m_NavMesh = m_Builder->GetNavMesh();

        // Initialize query
        if (m_NavMesh && m_NavMeshQuery) {
            dtStatus status = m_NavMeshQuery->init(m_NavMesh, 2048);
            if (dtStatusFailed(status)) {
                result.success = false;
                result.errorMessage = "Failed to init NavMesh query";
                return result;
            }
        }

        spdlog::info("[NavMeshManager] NavMesh built: {} vertices, {} polygons",
                     result.stats.vertexCount, result.stats.polyCount);
        return result;
    }

    NavMeshBuildResult NavMeshManager::BuildFromColliders(const std::vector<ColliderData>& colliders) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        NavMeshBuildResult result;

        if (!m_Initialized) {
            result.success = false;
            result.errorMessage = "NavMeshManager not initialized";
            return result;
        }

        // Set geometry manually
        std::vector<float> verts;
        std::vector<int> tris;
        std::vector<uint8_t> areas;

        for (const auto& collider : colliders) {
            size_t baseIndex = verts.size() / 3;

            // Add vertices
            verts.insert(verts.end(), collider.vertices.begin(), collider.vertices.end());

            // Add triangles with offset
            for (size_t i = 0; i < collider.indices.size(); i += 3) {
                tris.push_back(static_cast<int>(baseIndex) + collider.indices[i]);
                tris.push_back(static_cast<int>(baseIndex) + collider.indices[i + 1]);
                tris.push_back(static_cast<int>(baseIndex) + collider.indices[i + 2]);
            }

            // Add area types
            size_t numTris = collider.indices.size() / 3;
            for (size_t i = 0; i < numTris; ++i) {
                areas.push_back(collider.areaType);
            }
        }

        m_Builder->SetGeometry(verts, tris, areas);

        // Build NavMesh
        result = m_Builder->Build();
        if (!result.success) {
            return result;
        }

        // Free old NavMesh
        if (m_NavMesh) {
            dtFreeNavMesh(m_NavMesh);
        }

        // Transfer ownership
        m_NavMesh = m_Builder->GetNavMesh();

        // Initialize query
        if (m_NavMesh && m_NavMeshQuery) {
            dtStatus status = m_NavMeshQuery->init(m_NavMesh, 2048);
            if (dtStatusFailed(status)) {
                result.success = false;
                result.errorMessage = "Failed to init NavMesh query";
                return result;
            }
        }

        return result;
    }

    NavMeshBuildResult NavMeshManager::RebuildTile(int32_t tileX, int32_t tileZ) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        NavMeshBuildResult result;

        if (!m_Initialized || !m_CurrentScene) {
            result.success = false;
            result.errorMessage = "Not initialized or no scene";
            return result;
        }

        if (!m_NavMesh) {
            result.success = false;
            result.errorMessage = "No NavMesh exists";
            return result;
        }

        // Remove old tile
        dtTileRef tileRef = m_NavMesh->getTileRefAt(tileX, tileZ, 0);
        if (tileRef) {
            m_NavMesh->removeTile(tileRef, nullptr, nullptr);
        }

        // Rebuild tile
        result = m_Builder->BuildTile(tileX, tileZ);
        if (!result.success) {
            return result;
        }

        // Re-add tile data
        // Note: BuildTile updates the internal NavMesh
        spdlog::debug("[NavMeshManager] Rebuilt tile ({}, {})", tileX, tileZ);
        return result;
    }

    std::future<NavMeshBuildResult> NavMeshManager::BuildFromSceneAsync(ECS::Scene* scene) {
        return std::async(std::launch::async, [this, scene]() {
            return BuildFromScene(scene);
        });
    }

    dtNavMesh* NavMeshManager::GetNavMesh() {
        return m_NavMesh;
    }

    dtNavMeshQuery* NavMeshManager::GetNavMeshQuery() {
        return m_NavMeshQuery;
    }

    bool NavMeshManager::HasNavMeshData() const {
        return m_NavMesh != nullptr;
    }

    PathQueryResult NavMeshManager::FindPath(const glm::vec3& start, const glm::vec3& end,
                                              const dtQueryFilter* filter) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        PathQueryResult result;

        if (!m_NavMesh || !m_NavMeshQuery) {
            result.success = false;
            return result;
        }

        // Use default filter if none provided
        const dtQueryFilter* queryFilter = filter ? filter : m_DefaultFilter.get();

        // Convert positions
        float startPos[3] = { start.x, start.y, start.z };
        float endPos[3] = { end.x, end.y, end.z };
        float extents[3] = { 2.0f, 4.0f, 2.0f };

        // Find start polygon
        dtPolyRef startRef;
        float nearestStart[3];
        dtStatus status = m_NavMeshQuery->findNearestPoly(startPos, extents, queryFilter,
                                                          &startRef, nearestStart);
        if (dtStatusFailed(status) || startRef == 0) {
            result.success = false;
            return result;
        }

        // Find end polygon
        dtPolyRef endRef;
        float nearestEnd[3];
        status = m_NavMeshQuery->findNearestPoly(endPos, extents, queryFilter,
                                                  &endRef, nearestEnd);
        if (dtStatusFailed(status) || endRef == 0) {
            result.success = false;
            return result;
        }

        // Find path
        static constexpr int MAX_PATH_POLYS = 256;
        dtPolyRef path[MAX_PATH_POLYS];
        int pathCount = 0;

        status = m_NavMeshQuery->findPath(startRef, endRef, nearestStart, nearestEnd,
                                           queryFilter, path, &pathCount, MAX_PATH_POLYS);
        if (dtStatusFailed(status) || pathCount == 0) {
            result.success = false;
            return result;
        }

        // Find straight path
        static constexpr int MAX_STRAIGHT_PATH = 256;
        float straightPath[MAX_STRAIGHT_PATH * 3];
        unsigned char straightPathFlags[MAX_STRAIGHT_PATH];
        dtPolyRef straightPathPolys[MAX_STRAIGHT_PATH];
        int straightPathCount = 0;

        status = m_NavMeshQuery->findStraightPath(nearestStart, nearestEnd, path, pathCount,
                                                   straightPath, straightPathFlags,
                                                   straightPathPolys, &straightPathCount,
                                                   MAX_STRAIGHT_PATH, DT_STRAIGHTPATH_AREA_CROSSINGS);
        if (dtStatusFailed(status) || straightPathCount == 0) {
            result.success = false;
            return result;
        }

        // Copy result
        result.waypoints.resize(straightPathCount);
        result.totalDistance = 0.0f;

        for (int i = 0; i < straightPathCount; ++i) {
            result.waypoints[i] = glm::vec3(
                straightPath[i * 3],
                straightPath[i * 3 + 1],
                straightPath[i * 3 + 2]
            );

            if (i > 0) {
                result.totalDistance += glm::length(result.waypoints[i] - result.waypoints[i - 1]);
            }
        }

        result.success = true;
        result.isPartial = dtStatusDetail(status, DT_PARTIAL_RESULT) != 0;
        return result;
    }

    glm::vec3 NavMeshManager::FindNearestPoint(const glm::vec3& position,
                                                const glm::vec3& extents) {
        std::lock_guard<std::mutex> lock(m_Mutex);

        if (!m_NavMesh || !m_NavMeshQuery) {
            return position;
        }

        float pos[3] = { position.x, position.y, position.z };
        float ext[3] = { extents.x, extents.y, extents.z };
        float nearest[3];
        dtPolyRef polyRef;

        dtStatus status = m_NavMeshQuery->findNearestPoly(pos, ext, m_DefaultFilter.get(),
                                                          &polyRef, nearest);
        if (dtStatusFailed(status) || polyRef == 0) {
            return position;
        }

        return glm::vec3(nearest[0], nearest[1], nearest[2]);
    }

    bool NavMeshManager::IsPointOnNavMesh(const glm::vec3& position,
                                           const glm::vec3& extents) {
        std::lock_guard<std::mutex> lock(m_Mutex);

        if (!m_NavMesh || !m_NavMeshQuery) {
            return false;
        }

        float pos[3] = { position.x, position.y, position.z };
        float ext[3] = { extents.x, extents.y, extents.z };
        float nearest[3];
        dtPolyRef polyRef;

        dtStatus status = m_NavMeshQuery->findNearestPoly(pos, ext, m_DefaultFilter.get(),
                                                          &polyRef, nearest);
        return dtStatusSucceed(status) && polyRef != 0;
    }

    float NavMeshManager::GetDistanceToNavMesh(const glm::vec3& position) {
        glm::vec3 nearest = FindNearestPoint(position, glm::vec3(5.0f, 10.0f, 5.0f));
        return glm::length(position - nearest);
    }

    bool NavMeshManager::Raycast(const glm::vec3& start, const glm::vec3& end,
                                  glm::vec3& hitPoint, glm::vec3& hitNormal) {
        std::lock_guard<std::mutex> lock(m_Mutex);

        if (!m_NavMesh || !m_NavMeshQuery) {
            return false;
        }

        float startPos[3] = { start.x, start.y, start.z };
        float endPos[3] = { end.x, end.y, end.z };
        float extents[3] = { 1.0f, 2.0f, 1.0f };

        // Find start polygon
        dtPolyRef startRef;
        float nearestStart[3];
        dtStatus status = m_NavMeshQuery->findNearestPoly(startPos, extents, m_DefaultFilter.get(),
                                                          &startRef, nearestStart);
        if (dtStatusFailed(status) || startRef == 0) {
            return false;
        }

        // Raycast
        float t = 0;
        float hitNormalArray[3];
        static constexpr int MAX_POLYS = 64;
        dtPolyRef path[MAX_POLYS];
        int pathCount = 0;

        status = m_NavMeshQuery->raycast(startRef, nearestStart, endPos, m_DefaultFilter.get(),
                                          &t, hitNormalArray, path, &pathCount, MAX_POLYS);
        if (dtStatusFailed(status)) {
            return false;
        }

        if (t < 1.0f) {
            // Hit something
            hitPoint = start + (end - start) * t;
            hitNormal = glm::vec3(hitNormalArray[0], hitNormalArray[1], hitNormalArray[2]);
            return true;
        }

        return false;
    }

    uint32_t NavMeshManager::AddOffMeshConnection(const glm::vec3& start, const glm::vec3& end,
                                                   float radius, bool bidirectional,
                                                   uint8_t areaType) {
        std::lock_guard<std::mutex> lock(m_Mutex);

        OffMeshConnection conn;
        conn.id = m_NextConnectionId++;
        conn.start = start;
        conn.end = end;
        conn.radius = radius;
        conn.bidirectional = bidirectional;
        conn.areaType = areaType;
        conn.flags = FLAG_WALK;

        m_OffMeshConnections.push_back(conn);

        // Mark affected tiles dirty
        MarkTileDirty(static_cast<int32_t>(start.x / m_Config.tileSize),
                      static_cast<int32_t>(start.z / m_Config.tileSize));
        if (!bidirectional) {
            MarkTileDirty(static_cast<int32_t>(end.x / m_Config.tileSize),
                          static_cast<int32_t>(end.z / m_Config.tileSize));
        }

        return conn.id;
    }

    void NavMeshManager::RemoveOffMeshConnection(uint32_t connectionId) {
        std::lock_guard<std::mutex> lock(m_Mutex);

        auto it = std::find_if(m_OffMeshConnections.begin(), m_OffMeshConnections.end(),
                               [connectionId](const OffMeshConnection& c) {
                                   return c.id == connectionId;
                               });

        if (it != m_OffMeshConnections.end()) {
            // Mark tiles dirty
            MarkTileDirty(static_cast<int32_t>(it->start.x / m_Config.tileSize),
                          static_cast<int32_t>(it->start.z / m_Config.tileSize));
            m_OffMeshConnections.erase(it);
        }
    }

    void NavMeshManager::ClearOffMeshConnections() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_OffMeshConnections.clear();
    }

    void NavMeshManager::GetTileAt(const glm::vec3& position, int32_t& outX, int32_t& outZ) {
        outX = static_cast<int32_t>(std::floor(position.x / m_Config.tileSize));
        outZ = static_cast<int32_t>(std::floor(position.z / m_Config.tileSize));
    }

    void NavMeshManager::MarkTileDirty(int32_t tileX, int32_t tileZ) {
        m_DirtyTiles.insert({ tileX, tileZ });
    }

    void NavMeshManager::MarkTilesDirtyInRadius(const glm::vec3& center, float radius) {
        int32_t minX = static_cast<int32_t>(std::floor((center.x - radius) / m_Config.tileSize));
        int32_t maxX = static_cast<int32_t>(std::ceil((center.x + radius) / m_Config.tileSize));
        int32_t minZ = static_cast<int32_t>(std::floor((center.z - radius) / m_Config.tileSize));
        int32_t maxZ = static_cast<int32_t>(std::ceil((center.z + radius) / m_Config.tileSize));

        for (int32_t x = minX; x <= maxX; ++x) {
            for (int32_t z = minZ; z <= maxZ; ++z) {
                m_DirtyTiles.insert({ x, z });
            }
        }
    }

    void NavMeshManager::RebuildDirtyTiles() {
        auto dirtyCopy = m_DirtyTiles;
        m_DirtyTiles.clear();

        for (const auto& [x, z] : dirtyCopy) {
            RebuildTile(x, z);
        }
    }

    bool NavMeshManager::HasDirtyTiles() const {
        return !m_DirtyTiles.empty();
    }

    void NavMeshManager::SetQueryFilter(const dtQueryFilter& filter) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        *m_DefaultFilter = filter;
    }

    NavMeshStats NavMeshManager::GetStats() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        NavMeshStats stats;

        if (!m_NavMesh) {
            return stats;
        }

        const dtNavMesh* mesh = m_NavMesh;

        // Count tiles and geometry
        for (int i = 0; i < mesh->getMaxTiles(); ++i) {
            const dtMeshTile* tile = mesh->getTile(i);
            if (!tile || !tile->header) continue;

            stats.tileCount++;
            stats.vertexCount += tile->header->vertCount;
            stats.polyCount += tile->header->polyCount;

            // Estimate memory
            stats.memoryUsageBytes += tile->dataSize;
        }

        return stats;
    }

} // namespace Navigation
} // namespace Core
