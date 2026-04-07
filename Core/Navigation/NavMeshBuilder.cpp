#include "NavMeshBuilder.h"
#include "../ECS/Scene.h"
#include "../ECS/Components/Components.h"
#include "../Log.h"

#include <Recast.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <chrono>
#include <cstring>

namespace Core {
namespace Navigation {

// =============================================================================
// NavMeshContext Implementation
// =============================================================================

NavMeshContext::NavMeshContext(bool logEnabled) 
    : m_LogEnabled(logEnabled)
    , m_TimerStarts(RC_MAX_TIMERS, 0)
    , m_TimerAccum(RC_MAX_TIMERS, 0) {
}

void NavMeshContext::doLog(const rcLogCategory category, const char* msg, const int len) {
    if (!m_LogEnabled) return;
    
    std::string message(msg, len);
    switch (category) {
        case RC_LOG_PROGRESS:
            LOG_DEBUG("[NavMesh] {}", message);
            break;
        case RC_LOG_WARNING:
            LOG_WARN("[NavMesh] {}", message);
            break;
        case RC_LOG_ERROR:
            LOG_ERROR("[NavMesh] {}", message);
            break;
        default:
            break;
    }
}

void NavMeshContext::doStartTimer(const rcTimerLabel label) {
    m_TimerStarts[label] = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

void NavMeshContext::doStopTimer(const rcTimerLabel label) {
    int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    m_TimerAccum[label] += now - m_TimerStarts[label];
}

int NavMeshContext::doGetAccumulatedTime(const rcTimerLabel label) const {
    return static_cast<int>(m_TimerAccum[label]);
}

// =============================================================================
// NavMeshBuilder Implementation
// =============================================================================

NavMeshBuilder::NavMeshBuilder()
    : m_Context(std::make_unique<NavMeshContext>(true)) {
}

NavMeshBuilder::~NavMeshBuilder() {
    CleanupIntermediateData();
    if (m_NavMesh) {
        dtFreeNavMesh(m_NavMesh);
        m_NavMesh = nullptr;
    }
}

// =============================================================================
// Geometry Collection
// =============================================================================

NavMeshInputGeometry NavMeshBuilder::CollectGeometry(ECS::Scene* scene, uint8_t areaFilter) {
    NavMeshInputGeometry result;
    
    if (!scene) {
        LOG_ERROR("[NavMeshBuilder] Null scene provided");
        return result;
    }

    result.BoundsMin = glm::vec3(FLT_MAX);
    result.BoundsMax = glm::vec3(-FLT_MAX);

    auto& registry = scene->GetRegistry();
    
    // Collect from entities with colliders
    auto view = registry.view<ECS::TransformComponent, ECS::ColliderComponent>();
    
    for (auto entity : view) {
        auto& transform = view.get<ECS::TransformComponent>(entity);
        auto& collider = view.get<ECS::ColliderComponent>(entity);
        
        // Check if entity has NavMeshComponent for area type
        uint8_t areaType = AREA_GROUND;
        // NavMeshComponent would be checked here once implemented
        
        if ((areaFilter != 0xFF) && (areaType != areaFilter)) {
            continue;
        }

        // Get collider mesh data
        ColliderData data;
        data.Transform = transform.GetWorldMatrix();
        data.AreaType = areaType;

        // Extract vertices based on collider type
        switch (collider.Type) {
            case ECS::ColliderType::Box: {
                // Generate box vertices
                glm::vec3 half = collider.BoxHalfExtents;
                data.Vertices = {
                    {-half.x, -half.y, -half.z}, {half.x, -half.y, -half.z},
                    {half.x, -half.y, half.z}, {-half.x, -half.y, half.z},
                    {-half.x, half.y, -half.z}, {half.x, half.y, -half.z},
                    {half.x, half.y, half.z}, {-half.x, half.y, half.z}
                };
                data.Indices = {
                    0,1,2, 0,2,3,  // Bottom
                    4,6,5, 4,7,6,  // Top
                    0,4,5, 0,5,1,  // Front
                    2,6,7, 2,7,3,  // Back
                    0,3,7, 0,7,4,  // Left
                    1,5,6, 1,6,2   // Right
                };
                break;
            }
            case ECS::ColliderType::Sphere: {
                // Generate simple sphere approximation (icosphere)
                float r = collider.SphereRadius;
                float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
                data.Vertices = {
                    {-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0},
                    {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t},
                    {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}
                };
                for (auto& v : data.Vertices) {
                    v = glm::normalize(v) * r;
                }
                data.Indices = {
                    0,11,5, 0,5,1, 0,1,7, 0,7,10, 0,10,11,
                    1,5,9, 5,11,4, 11,10,2, 10,7,6, 7,1,8,
                    3,9,4, 3,4,2, 3,2,6, 3,6,8, 3,8,9,
                    4,9,5, 2,4,11, 6,2,10, 8,6,7, 9,8,1
                };
                break;
            }
            case ECS::ColliderType::Capsule: {
                // Generate capsule approximation
                float r = collider.CapsuleRadius;
                float h = collider.CapsuleHeight;
                // Simplified capsule as cylinder
                const int segments = 8;
                for (int i = 0; i < segments; ++i) {
                    float angle = 2.0f * 3.14159f * i / segments;
                    float x = r * std::cos(angle);
                    float z = r * std::sin(angle);
                    data.Vertices.push_back({x, -h/2, z});
                    data.Vertices.push_back({x, h/2, z});
                }
                for (int i = 0; i < segments; ++i) {
                    int next = (i + 1) % segments;
                    data.Indices.push_back(i * 2);
                    data.Indices.push_back(next * 2);
                    data.Indices.push_back(i * 2 + 1);
                    data.Indices.push_back(next * 2);
                    data.Indices.push_back(next * 2 + 1);
                    data.Indices.push_back(i * 2 + 1);
                }
                break;
            }
            case ECS::ColliderType::Mesh: {
                // Use mesh collider vertices directly
                data.Vertices = collider.MeshVertices;
                data.Indices = collider.MeshIndices;
                break;
            }
            default:
                continue;
        }

        // Transform vertices and add to result
        uint32_t baseVertex = static_cast<uint32_t>(result.Vertices.size() / 3);
        for (const auto& v : data.Vertices) {
            glm::vec4 transformed = data.Transform * glm::vec4(v, 1.0f);
            glm::vec3 worldPos(transformed);
            
            result.Vertices.push_back(worldPos.x);
            result.Vertices.push_back(worldPos.y);
            result.Vertices.push_back(worldPos.z);

            result.BoundsMin = glm::min(result.BoundsMin, worldPos);
            result.BoundsMax = glm::max(result.BoundsMax, worldPos);
        }

        // Add triangles
        for (size_t i = 0; i < data.Indices.size(); i += 3) {
            result.Triangles.push_back(static_cast<int32_t>(baseVertex + data.Indices[i]));
            result.Triangles.push_back(static_cast<int32_t>(baseVertex + data.Indices[i + 1]));
            result.Triangles.push_back(static_cast<int32_t>(baseVertex + data.Indices[i + 2]));
            result.AreaTypes.push_back(data.AreaType);
        }
    }

    m_InputGeometry = result;
    return result;
}

NavMeshInputGeometry NavMeshBuilder::CollectGeometry(const std::vector<ColliderData>& colliders) {
    NavMeshInputGeometry result;
    result.BoundsMin = glm::vec3(FLT_MAX);
    result.BoundsMax = glm::vec3(-FLT_MAX);

    for (const auto& data : colliders) {
        uint32_t baseVertex = static_cast<uint32_t>(result.Vertices.size() / 3);
        
        for (const auto& v : data.Vertices) {
            glm::vec4 transformed = data.Transform * glm::vec4(v, 1.0f);
            glm::vec3 worldPos(transformed);
            
            result.Vertices.push_back(worldPos.x);
            result.Vertices.push_back(worldPos.y);
            result.Vertices.push_back(worldPos.z);

            result.BoundsMin = glm::min(result.BoundsMin, worldPos);
            result.BoundsMax = glm::max(result.BoundsMax, worldPos);
        }

        for (size_t i = 0; i < data.Indices.size(); i += 3) {
            result.Triangles.push_back(static_cast<int32_t>(baseVertex + data.Indices[i]));
            result.Triangles.push_back(static_cast<int32_t>(baseVertex + data.Indices[i + 1]));
            result.Triangles.push_back(static_cast<int32_t>(baseVertex + data.Indices[i + 2]));
            result.AreaTypes.push_back(data.AreaType);
        }
    }

    m_InputGeometry = result;
    return result;
}

void NavMeshBuilder::AddGeometry(const NavMeshInputGeometry& geometry) {
    uint32_t baseVertex = static_cast<uint32_t>(m_InputGeometry.Vertices.size() / 3);
    
    m_InputGeometry.Vertices.insert(m_InputGeometry.Vertices.end(),
                                     geometry.Vertices.begin(),
                                     geometry.Vertices.end());
    
    for (const auto& idx : geometry.Triangles) {
        m_InputGeometry.Triangles.push_back(idx + baseVertex);
    }
    
    m_InputGeometry.AreaTypes.insert(m_InputGeometry.AreaTypes.end(),
                                      geometry.AreaTypes.begin(),
                                      geometry.AreaTypes.end());

    m_InputGeometry.BoundsMin = glm::min(m_InputGeometry.BoundsMin, geometry.BoundsMin);
    m_InputGeometry.BoundsMax = glm::max(m_InputGeometry.BoundsMax, geometry.BoundsMax);
}

void NavMeshBuilder::ClearGeometry() {
    m_InputGeometry = NavMeshInputGeometry{};
}

// =============================================================================
// NavMesh Building
// =============================================================================

NavMeshBuildResult NavMeshBuilder::Build(const NavMeshConfig& config) {
    NavMeshBuildResult result;
    auto startTime = std::chrono::high_resolution_clock::now();

    if (m_InputGeometry.Triangles.empty()) {
        result.ErrorMessage = "No geometry to build NavMesh from";
        return result;
    }

    CleanupIntermediateData();

    // For large worlds, use tiled NavMesh
    int32_t tileCountX, tileCountZ;
    GetTileCount(config, tileCountX, tileCountZ);

    if (tileCountX > 1 || tileCountZ > 1) {
        if (!CreateTiledNavMesh(config, tileCountX, tileCountZ)) {
            result.ErrorMessage = "Failed to create tiled NavMesh";
            return result;
        }
        result.TilesBuilt = tileCountX * tileCountZ;
    } else {
        // Single tile NavMesh
        if (!BuildHeightfield(config, m_InputGeometry.BoundsMin, m_InputGeometry.BoundsMax)) {
            result.ErrorMessage = "Failed to build heightfield";
            return result;
        }

        if (!BuildCompactHeightfield(config)) {
            result.ErrorMessage = "Failed to build compact heightfield";
            return result;
        }

        if (!BuildRegions(config)) {
            result.ErrorMessage = "Failed to build regions";
            return result;
        }

        if (!BuildContours(config)) {
            result.ErrorMessage = "Failed to build contours";
            return result;
        }

        if (!BuildPolyMesh(config)) {
            result.ErrorMessage = "Failed to build polygon mesh";
            return result;
        }

        if (config.BuildDetailMesh && !BuildDetailMesh(config)) {
            result.ErrorMessage = "Failed to build detail mesh";
            return result;
        }

        if (!CreateDetourNavMesh(config)) {
            result.ErrorMessage = "Failed to create Detour NavMesh";
            return result;
        }

        result.TilesBuilt = 1;
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    result.BuildTimeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    
    if (m_NavMesh) {
        result.Success = true;
        // Count polygons
        for (int i = 0; i < m_NavMesh->getMaxTiles(); ++i) {
            const dtMeshTile* tile = m_NavMesh->getTile(i);
            if (tile && tile->header) {
                result.PolygonCount += tile->header->polyCount;
            }
        }
    }

    LOG_INFO("[NavMeshBuilder] Built NavMesh: {} tiles, {} polygons in {:.2f}ms",
             result.TilesBuilt, result.PolygonCount, result.BuildTimeMs);

    return result;
}

NavMeshBuildResult NavMeshBuilder::BuildTile(const NavMeshConfig& config,
                                              int32_t tileX, int32_t tileZ) {
    NavMeshBuildResult result;
    auto startTime = std::chrono::high_resolution_clock::now();

    if (!m_NavMesh) {
        result.ErrorMessage = "NavMesh not initialized";
        return result;
    }

    // Calculate tile bounds
    float tileWidth = config.TileSize * config.CellSize;
    glm::vec3 tileBoundsMin = m_InputGeometry.BoundsMin;
    tileBoundsMin.x += tileX * tileWidth;
    tileBoundsMin.z += tileZ * tileWidth;

    glm::vec3 tileBoundsMax = tileBoundsMin;
    tileBoundsMax.x += tileWidth;
    tileBoundsMax.y = m_InputGeometry.BoundsMax.y;
    tileBoundsMax.z += tileWidth;

    // Remove existing tile
    m_NavMesh->removeTile(m_NavMesh->getTileRefAt(tileX, tileZ, 0), nullptr, nullptr);

    // Build new tile data
    int dataSize = 0;
    unsigned char* data = BuildTileData(config, tileX, tileZ, tileBoundsMin, tileBoundsMax, dataSize);

    if (data) {
        dtStatus status = m_NavMesh->addTile(data, dataSize, DT_TILE_FREE_DATA, 0, nullptr);
        if (dtStatusSucceed(status)) {
            result.Success = true;
            result.TilesBuilt = 1;
        } else {
            dtFree(data);
            result.ErrorMessage = "Failed to add tile to NavMesh";
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    result.BuildTimeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();

    return result;
}

dtNavMesh* NavMeshBuilder::GetNavMesh() {
    dtNavMesh* result = m_NavMesh;
    m_NavMesh = nullptr;
    return result;
}

// =============================================================================
// Utility
// =============================================================================

void NavMeshBuilder::GetTileCoords(const NavMeshConfig& config,
                                   const glm::vec3& position,
                                   int32_t& outTileX, int32_t& outTileZ) const {
    float tileWidth = config.TileSize * config.CellSize;
    outTileX = static_cast<int32_t>((position.x - m_InputGeometry.BoundsMin.x) / tileWidth);
    outTileZ = static_cast<int32_t>((position.z - m_InputGeometry.BoundsMin.z) / tileWidth);
}

void NavMeshBuilder::GetBounds(glm::vec3& outMin, glm::vec3& outMax) const {
    outMin = m_InputGeometry.BoundsMin;
    outMax = m_InputGeometry.BoundsMax;
}

void NavMeshBuilder::GetTileCount(const NavMeshConfig& config,
                                  int32_t& outCountX, int32_t& outCountZ) const {
    glm::vec3 size = m_InputGeometry.BoundsMax - m_InputGeometry.BoundsMin;
    float tileWidth = config.TileSize * config.CellSize;
    outCountX = std::max(1, static_cast<int32_t>(std::ceil(size.x / tileWidth)));
    outCountZ = std::max(1, static_cast<int32_t>(std::ceil(size.z / tileWidth)));
}

// =============================================================================
// Private Build Methods
// =============================================================================

bool NavMeshBuilder::BuildHeightfield(const NavMeshConfig& config,
                                       const glm::vec3& boundsMin,
                                       const glm::vec3& boundsMax) {
    float bmin[3] = {boundsMin.x, boundsMin.y, boundsMin.z};
    float bmax[3] = {boundsMax.x, boundsMax.y, boundsMax.z};

    int gridWidth, gridHeight;
    rcCalcGridSize(bmin, bmax, config.CellSize, &gridWidth, &gridHeight);

    m_Heightfield = rcAllocHeightfield();
    if (!m_Heightfield) {
        LOG_ERROR("[NavMeshBuilder] Failed to allocate heightfield");
        return false;
    }

    if (!rcCreateHeightfield(m_Context.get(), *m_Heightfield, gridWidth, gridHeight,
                             bmin, bmax, config.CellSize, config.CellHeight)) {
        LOG_ERROR("[NavMeshBuilder] Failed to create heightfield");
        return false;
    }

    // Rasterize triangles
    std::vector<unsigned char> triAreas(m_InputGeometry.AreaTypes.size(), 0);
    
    const float* verts = m_InputGeometry.Vertices.data();
    int nverts = static_cast<int>(m_InputGeometry.Vertices.size() / 3);
    const int* tris = m_InputGeometry.Triangles.data();
    int ntris = static_cast<int>(m_InputGeometry.Triangles.size() / 3);

    rcMarkWalkableTriangles(m_Context.get(), config.AgentMaxSlope, verts, nverts,
                            tris, ntris, triAreas.data());

    // Apply area types
    for (size_t i = 0; i < m_InputGeometry.AreaTypes.size(); ++i) {
        if (triAreas[i] != RC_NULL_AREA) {
            triAreas[i] = m_InputGeometry.AreaTypes[i];
        }
    }

    if (!rcRasterizeTriangles(m_Context.get(), verts, nverts, tris, triAreas.data(),
                              ntris, *m_Heightfield, static_cast<int>(config.AgentMaxClimb / config.CellHeight))) {
        LOG_ERROR("[NavMeshBuilder] Failed to rasterize triangles");
        return false;
    }

    // Filter walkable surfaces
    if (config.FilterLowHangingObstacles) {
        rcFilterLowHangingWalkableObstacles(m_Context.get(), 
            static_cast<int>(config.AgentMaxClimb / config.CellHeight), *m_Heightfield);
    }
    if (config.FilterLedgeSpans) {
        rcFilterLedgeSpans(m_Context.get(), 
            static_cast<int>(config.AgentHeight / config.CellHeight),
            static_cast<int>(config.AgentMaxClimb / config.CellHeight), *m_Heightfield);
    }
    if (config.FilterWalkableLowHeightSpans) {
        rcFilterWalkableLowHeightSpans(m_Context.get(),
            static_cast<int>(config.AgentHeight / config.CellHeight), *m_Heightfield);
    }

    return true;
}

bool NavMeshBuilder::BuildCompactHeightfield(const NavMeshConfig& config) {
    m_CompactHeightfield = rcAllocCompactHeightfield();
    if (!m_CompactHeightfield) {
        LOG_ERROR("[NavMeshBuilder] Failed to allocate compact heightfield");
        return false;
    }

    if (!rcBuildCompactHeightfield(m_Context.get(),
            static_cast<int>(config.AgentHeight / config.CellHeight),
            static_cast<int>(config.AgentMaxClimb / config.CellHeight),
            *m_Heightfield, *m_CompactHeightfield)) {
        LOG_ERROR("[NavMeshBuilder] Failed to build compact heightfield");
        return false;
    }

    // Erode walkable area by agent radius
    if (!rcErodeWalkableArea(m_Context.get(),
            static_cast<int>(std::ceil(config.AgentRadius / config.CellSize)),
            *m_CompactHeightfield)) {
        LOG_ERROR("[NavMeshBuilder] Failed to erode walkable area");
        return false;
    }

    return true;
}

bool NavMeshBuilder::BuildRegions(const NavMeshConfig& config) {
    if (!rcBuildDistanceField(m_Context.get(), *m_CompactHeightfield)) {
        LOG_ERROR("[NavMeshBuilder] Failed to build distance field");
        return false;
    }

    if (!rcBuildRegions(m_Context.get(), *m_CompactHeightfield, 0,
            static_cast<int>(config.RegionMinSize), static_cast<int>(config.RegionMergeSize))) {
        LOG_ERROR("[NavMeshBuilder] Failed to build regions");
        return false;
    }

    return true;
}

bool NavMeshBuilder::BuildContours(const NavMeshConfig& config) {
    m_ContourSet = rcAllocContourSet();
    if (!m_ContourSet) {
        LOG_ERROR("[NavMeshBuilder] Failed to allocate contour set");
        return false;
    }

    if (!rcBuildContours(m_Context.get(), *m_CompactHeightfield, config.EdgeMaxError,
            static_cast<int>(config.EdgeMaxLen / config.CellSize), *m_ContourSet)) {
        LOG_ERROR("[NavMeshBuilder] Failed to build contours");
        return false;
    }

    return true;
}

bool NavMeshBuilder::BuildPolyMesh(const NavMeshConfig& config) {
    m_PolyMesh = rcAllocPolyMesh();
    if (!m_PolyMesh) {
        LOG_ERROR("[NavMeshBuilder] Failed to allocate polygon mesh");
        return false;
    }

    if (!rcBuildPolyMesh(m_Context.get(), *m_ContourSet, 6, *m_PolyMesh)) {
        LOG_ERROR("[NavMeshBuilder] Failed to build polygon mesh");
        return false;
    }

    return true;
}

bool NavMeshBuilder::BuildDetailMesh(const NavMeshConfig& config) {
    m_DetailMesh = rcAllocPolyMeshDetail();
    if (!m_DetailMesh) {
        LOG_ERROR("[NavMeshBuilder] Failed to allocate detail mesh");
        return false;
    }

    if (!rcBuildPolyMeshDetail(m_Context.get(), *m_PolyMesh, *m_CompactHeightfield,
            config.DetailSampleDist, config.DetailSampleMaxError, *m_DetailMesh)) {
        LOG_ERROR("[NavMeshBuilder] Failed to build detail mesh");
        return false;
    }

    return true;
}

bool NavMeshBuilder::CreateDetourNavMesh(const NavMeshConfig& config) {
    dtNavMeshCreateParams params;
    std::memset(&params, 0, sizeof(params));

    params.verts = m_PolyMesh->verts;
    params.vertCount = m_PolyMesh->nverts;
    params.polys = m_PolyMesh->polys;
    params.polyAreas = m_PolyMesh->areas;
    params.polyFlags = m_PolyMesh->flags;
    params.polyCount = m_PolyMesh->npolys;
    params.nvp = m_PolyMesh->nvp;

    if (m_DetailMesh) {
        params.detailMeshes = m_DetailMesh->meshes;
        params.detailVerts = m_DetailMesh->verts;
        params.detailVertsCount = m_DetailMesh->nverts;
        params.detailTris = m_DetailMesh->tris;
        params.detailTriCount = m_DetailMesh->ntris;
    }

    params.walkableHeight = config.AgentHeight;
    params.walkableRadius = config.AgentRadius;
    params.walkableClimb = config.AgentMaxClimb;
    rcVcopy(params.bmin, m_PolyMesh->bmin);
    rcVcopy(params.bmax, m_PolyMesh->bmax);
    params.cs = config.CellSize;
    params.ch = config.CellHeight;
    params.buildBvTree = true;

    unsigned char* navData = nullptr;
    int navDataSize = 0;

    if (!dtCreateNavMeshData(&params, &navData, &navDataSize)) {
        LOG_ERROR("[NavMeshBuilder] Failed to create NavMesh data");
        return false;
    }

    m_NavMesh = dtAllocNavMesh();
    if (!m_NavMesh) {
        dtFree(navData);
        LOG_ERROR("[NavMeshBuilder] Failed to allocate NavMesh");
        return false;
    }

    dtStatus status = m_NavMesh->init(navData, navDataSize, DT_TILE_FREE_DATA);
    if (dtStatusFailed(status)) {
        dtFree(navData);
        dtFreeNavMesh(m_NavMesh);
        m_NavMesh = nullptr;
        LOG_ERROR("[NavMeshBuilder] Failed to initialize NavMesh");
        return false;
    }

    return true;
}

bool NavMeshBuilder::CreateTiledNavMesh(const NavMeshConfig& config,
                                        int32_t tileCountX, int32_t tileCountZ) {
    dtNavMeshParams params;
    std::memset(&params, 0, sizeof(params));

    params.orig[0] = m_InputGeometry.BoundsMin.x;
    params.orig[1] = m_InputGeometry.BoundsMin.y;
    params.orig[2] = m_InputGeometry.BoundsMin.z;
    params.tileWidth = config.TileSize * config.CellSize;
    params.tileHeight = config.TileSize * config.CellSize;
    params.maxTiles = std::min(config.MaxTiles, static_cast<uint32_t>(tileCountX * tileCountZ));
    params.maxPolys = config.MaxPolysPerTile;

    m_NavMesh = dtAllocNavMesh();
    if (!m_NavMesh) {
        LOG_ERROR("[NavMeshBuilder] Failed to allocate tiled NavMesh");
        return false;
    }

    dtStatus status = m_NavMesh->init(&params);
    if (dtStatusFailed(status)) {
        dtFreeNavMesh(m_NavMesh);
        m_NavMesh = nullptr;
        LOG_ERROR("[NavMeshBuilder] Failed to initialize tiled NavMesh");
        return false;
    }

    // Build each tile
    float tileWidth = config.TileSize * config.CellSize;
    for (int32_t z = 0; z < tileCountZ; ++z) {
        for (int32_t x = 0; x < tileCountX; ++x) {
            glm::vec3 tileBoundsMin = m_InputGeometry.BoundsMin;
            tileBoundsMin.x += x * tileWidth;
            tileBoundsMin.z += z * tileWidth;

            glm::vec3 tileBoundsMax = tileBoundsMin;
            tileBoundsMax.x += tileWidth;
            tileBoundsMax.y = m_InputGeometry.BoundsMax.y;
            tileBoundsMax.z += tileWidth;

            int dataSize = 0;
            unsigned char* data = BuildTileData(config, x, z, tileBoundsMin, tileBoundsMax, dataSize);
            if (data) {
                m_NavMesh->addTile(data, dataSize, DT_TILE_FREE_DATA, 0, nullptr);
            }
        }
    }

    return true;
}

unsigned char* NavMeshBuilder::BuildTileData(const NavMeshConfig& config,
                                              int32_t tileX, int32_t tileZ,
                                              const glm::vec3& tileBoundsMin,
                                              const glm::vec3& tileBoundsMax,
                                              int& dataSize) {
    CleanupIntermediateData();

    if (!BuildHeightfield(config, tileBoundsMin, tileBoundsMax)) {
        return nullptr;
    }

    if (!BuildCompactHeightfield(config)) {
        return nullptr;
    }

    if (!BuildRegions(config)) {
        return nullptr;
    }

    if (!BuildContours(config)) {
        return nullptr;
    }

    if (!BuildPolyMesh(config)) {
        return nullptr;
    }

    if (config.BuildDetailMesh && !BuildDetailMesh(config)) {
        return nullptr;
    }

    // Build tile data
    dtNavMeshCreateParams params;
    std::memset(&params, 0, sizeof(params));

    params.verts = m_PolyMesh->verts;
    params.vertCount = m_PolyMesh->nverts;
    params.polys = m_PolyMesh->polys;
    params.polyAreas = m_PolyMesh->areas;
    params.polyFlags = m_PolyMesh->flags;
    params.polyCount = m_PolyMesh->npolys;
    params.nvp = m_PolyMesh->nvp;

    if (m_DetailMesh) {
        params.detailMeshes = m_DetailMesh->meshes;
        params.detailVerts = m_DetailMesh->verts;
        params.detailVertsCount = m_DetailMesh->nverts;
        params.detailTris = m_DetailMesh->tris;
        params.detailTriCount = m_DetailMesh->ntris;
    }

    params.walkableHeight = config.AgentHeight;
    params.walkableRadius = config.AgentRadius;
    params.walkableClimb = config.AgentMaxClimb;
    rcVcopy(params.bmin, m_PolyMesh->bmin);
    rcVcopy(params.bmax, m_PolyMesh->bmax);
    params.cs = config.CellSize;
    params.ch = config.CellHeight;
    params.tileX = tileX;
    params.tileY = tileZ;
    params.buildBvTree = true;

    unsigned char* navData = nullptr;
    if (!dtCreateNavMeshData(&params, &navData, &dataSize)) {
        return nullptr;
    }

    return navData;
}

void NavMeshBuilder::CleanupIntermediateData() {
    if (m_Heightfield) {
        rcFreeHeightField(m_Heightfield);
        m_Heightfield = nullptr;
    }
    if (m_CompactHeightfield) {
        rcFreeCompactHeightfield(m_CompactHeightfield);
        m_CompactHeightfield = nullptr;
    }
    if (m_ContourSet) {
        rcFreeContourSet(m_ContourSet);
        m_ContourSet = nullptr;
    }
    if (m_PolyMesh) {
        rcFreePolyMesh(m_PolyMesh);
        m_PolyMesh = nullptr;
    }
    if (m_DetailMesh) {
        rcFreePolyMeshDetail(m_DetailMesh);
        m_DetailMesh = nullptr;
    }
}

} // namespace Navigation
} // namespace Core
