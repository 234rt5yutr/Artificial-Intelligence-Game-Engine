#include "Core/ECS/Systems/TerrainSystem.h"

namespace Core {
namespace ECS {

    TerrainSystem::TerrainSystem()
        : m_Generator(std::make_unique<Renderer::TerrainGenerator>())
    {
    }

    TerrainSystem::~TerrainSystem()
    {
        Shutdown();
    }

    void TerrainSystem::Initialize(std::shared_ptr<RHI::RHIDevice> device)
    {
        PROFILE_FUNCTION();

        m_Device = device;
        m_IsInitialized = true;

        ENGINE_CORE_INFO("TerrainSystem initialized");
    }

    void TerrainSystem::Shutdown()
    {
        PROFILE_FUNCTION();

        if (!m_IsInitialized) return;

        // Wait for any pending async jobs
        if (m_Generator) {
            m_Generator->WaitForAllJobs();
        }

        // Clear all chunks
        m_LoadedChunks.clear();
        m_VisibleChunks.clear();
        
        // Clear queues
        while (!m_ChunksToLoad.empty()) m_ChunksToLoad.pop();
        while (!m_ChunksToUpload.empty()) m_ChunksToUpload.pop();

        // Clear pool
        {
            std::lock_guard<std::mutex> lock(m_PoolMutex);
            m_ChunkPool.clear();
        }

        m_IsInitialized = false;

        ENGINE_CORE_INFO("TerrainSystem shutdown");
    }

    void TerrainSystem::Update(Scene& scene, float deltaTime)
    {
        PROFILE_FUNCTION();

        if (!m_IsInitialized) return;

        m_TimeSinceLastUpdate += deltaTime;

        // Get camera position
        Math::Vec3 cameraPos(0.0f);
        Math::Mat4 viewProjection(1.0f);

        auto cameraView = scene.View<TransformComponent, CameraComponent>();
        for (auto entity : cameraView) {
            auto& transform = cameraView.get<TransformComponent>(entity);
            auto& camera = cameraView.get<CameraComponent>(entity);
            
            if (camera.IsActive) {
                cameraPos = Math::Vec3(transform.WorldMatrix[3]);
                viewProjection = camera.GetProjectionMatrix() * camera.GetViewMatrix(transform);
                break;
            }
        }

        // Process terrain entities
        auto terrainView = scene.View<TransformComponent, TerrainComponent>();
        
        for (auto entity : terrainView) {
            auto& terrain = terrainView.get<TerrainComponent>(entity);
            
            if (!terrain.Visible) continue;

            // Periodic chunk loading/unloading check
            if (m_TimeSinceLastUpdate >= m_Config.ChunkUpdateInterval || m_ForceUpdatePending) {
                UpdateChunkLoading(cameraPos, terrain);
                UnloadDistantChunks(cameraPos, terrain);
                m_TimeSinceLastUpdate = 0.0f;
                m_ForceUpdatePending = false;
            }

            // Process load queue
            ProcessLoadQueue(terrain);

            // Process upload queue
            ProcessUploadQueue();

            // Update LODs for loaded chunks
            UpdateChunkLODs(cameraPos, terrain);
        }

        // Update visibility (frustum culling)
        UpdateVisibility(viewProjection);

        ENGINE_CORE_TRACE("TerrainSystem: {} loaded, {} visible, {} pending",
                          GetLoadedChunkCount(), GetVisibleChunkCount(), GetPendingChunkCount());
    }

    void TerrainSystem::ForceUpdate()
    {
        m_ForceUpdatePending = true;
    }

    float TerrainSystem::SampleHeight(float worldX, float worldZ, const TerrainComponent& terrain) const
    {
        // Find the chunk containing this position
        Math::IVec2 chunkCoord = WorldToChunkCoord(Math::Vec3(worldX, 0, worldZ), terrain);
        Renderer::ChunkKey key(chunkCoord);

        auto it = m_LoadedChunks.find(key);
        if (it == m_LoadedChunks.end() || !it->second->IsReady()) {
            return 0.0f;
        }

        const auto& chunk = *it->second;
        
        // Convert to local chunk position
        float chunkWorldSize = terrain.ChunkSize * terrain.HorizontalScale;
        float localX = worldX - chunk.WorldPosition.x;
        float localZ = worldZ - chunk.WorldPosition.z;

        return chunk.SampleHeight(localX / terrain.HorizontalScale, 
                                  localZ / terrain.HorizontalScale, 
                                  terrain.ChunkSize);
    }

    Math::Vec3 TerrainSystem::SampleNormal(float worldX, float worldZ, const TerrainComponent& terrain) const
    {
        // Sample heights at neighboring positions for normal calculation
        float epsilon = terrain.HorizontalScale;
        
        float hL = SampleHeight(worldX - epsilon, worldZ, terrain);
        float hR = SampleHeight(worldX + epsilon, worldZ, terrain);
        float hD = SampleHeight(worldX, worldZ - epsilon, terrain);
        float hU = SampleHeight(worldX, worldZ + epsilon, terrain);

        Math::Vec3 normal(hL - hR, 2.0f * epsilon, hD - hU);
        return glm::normalize(normal);
    }

    void TerrainSystem::UpdateChunkLoading(const Math::Vec3& cameraPos, const TerrainComponent& terrain)
    {
        PROFILE_FUNCTION();

        Math::IVec2 centerChunk = WorldToChunkCoord(cameraPos, terrain);
        float chunkWorldSize = terrain.ChunkSize * terrain.HorizontalScale;
        
        // Calculate view radius in chunks
        int viewRadiusChunks = static_cast<int>(terrain.ViewDistance / chunkWorldSize) + 1;

        // Iterate over chunks in view range
        for (int dz = -viewRadiusChunks; dz <= viewRadiusChunks; ++dz) {
            for (int dx = -viewRadiusChunks; dx <= viewRadiusChunks; ++dx) {
                Math::IVec2 chunkCoord(centerChunk.x + dx, centerChunk.y + dz);
                Renderer::ChunkKey key(chunkCoord);

                // Skip if already loaded or queued
                if (m_LoadedChunks.find(key) != m_LoadedChunks.end()) {
                    continue;
                }

                // Check if chunk is actually in range (circular, not square)
                if (!IsChunkInRange(chunkCoord, cameraPos, terrain)) {
                    continue;
                }

                // Add to load queue
                m_ChunksToLoad.push(key);
            }
        }
    }

    void TerrainSystem::UpdateChunkLODs(const Math::Vec3& cameraPos, const TerrainComponent& terrain)
    {
        PROFILE_FUNCTION();

        for (auto& [key, chunk] : m_LoadedChunks) {
            if (!chunk->IsReady()) continue;

            // Calculate distance to chunk center
            Math::Vec3 chunkCenter = chunk->BoundingBox.GetCenter();
            float distance = glm::length(cameraPos - chunkCenter);
            chunk->DistanceToCamera = distance;

            // Determine target LOD
            uint32_t targetLOD = terrain.GetLODForDistance(distance);
            
            if (targetLOD != chunk->TargetLOD) {
                chunk->TargetLOD = targetLOD;
                chunk->NeedsUpdate = true;
            }

            // Smooth LOD transition (geomorphing)
            if (terrain.EnableGeomorphing && chunk->CurrentLOD != chunk->TargetLOD) {
                float transitionSpeed = 2.0f; // LOD transitions per second
                chunk->LODTransition += transitionSpeed * 0.016f; // Assume ~60fps
                
                if (chunk->LODTransition >= 1.0f) {
                    chunk->CurrentLOD = chunk->TargetLOD;
                    chunk->LODTransition = 0.0f;
                }
            } else {
                chunk->CurrentLOD = chunk->TargetLOD;
            }
        }
    }

    void TerrainSystem::UpdateVisibility(const Math::Mat4& viewProjection)
    {
        PROFILE_FUNCTION();

        m_VisibleChunks.clear();
        m_VisibleChunks.reserve(m_LoadedChunks.size());

        for (auto& [key, chunk] : m_LoadedChunks) {
            if (!chunk->IsReady()) continue;

            bool visible = IsChunkVisible(*chunk, viewProjection);
            chunk->IsVisible = visible;

            if (visible) {
                m_VisibleChunks.push_back(chunk.get());
            }
        }

        // Sort by distance for front-to-back rendering (better Z rejection)
        std::sort(m_VisibleChunks.begin(), m_VisibleChunks.end(),
            [](const Renderer::TerrainChunk* a, const Renderer::TerrainChunk* b) {
                return a->DistanceToCamera < b->DistanceToCamera;
            });
    }

    void TerrainSystem::ProcessLoadQueue(const TerrainComponent& terrain)
    {
        PROFILE_FUNCTION();

        uint32_t chunksLoaded = 0;

        while (!m_ChunksToLoad.empty() && chunksLoaded < m_Config.ChunksToLoadPerFrame) {
            Renderer::ChunkKey key = m_ChunksToLoad.front();
            m_ChunksToLoad.pop();

            // Skip if already loaded
            if (m_LoadedChunks.find(key) != m_LoadedChunks.end()) {
                continue;
            }

            // Check max chunks limit
            if (m_LoadedChunks.size() >= m_Config.MaxLoadedChunks) {
                break;
            }

            // Calculate world position
            Math::IVec2 coord(key.X, key.Z);
            Math::Vec3 worldPos = ChunkCoordToWorld(coord, terrain);

            // Acquire chunk from pool or create new
            auto chunk = AcquireChunk(coord, worldPos);
            chunk->Seed = terrain.Seed;

            // Start generation
            if (m_Config.UseAsyncGeneration) {
                chunk->State = Renderer::ChunkState::Generating;
                
                m_Generator->GenerateChunkAsync(*chunk, terrain, 0,
                    [this, key](Renderer::TerrainChunk& c, bool success) {
                        if (success) {
                            c.State = Renderer::ChunkState::Generated;
                            m_ChunksToUpload.push(key);
                        } else {
                            ENGINE_CORE_ERROR("Failed to generate terrain chunk ({}, {})", 
                                             c.ChunkCoord.x, c.ChunkCoord.y);
                            c.State = Renderer::ChunkState::Unloaded;
                        }
                    });
            } else {
                // Synchronous generation
                bool success = m_Generator->GenerateChunk(*chunk, terrain, 0);
                if (success) {
                    chunk->State = Renderer::ChunkState::Generated;
                    m_ChunksToUpload.push(key);
                } else {
                    chunk->State = Renderer::ChunkState::Unloaded;
                }
            }

            m_LoadedChunks[key] = chunk;
            ++chunksLoaded;
        }
    }

    void TerrainSystem::ProcessUploadQueue()
    {
        PROFILE_FUNCTION();

        if (!m_Device) return;

        uint32_t chunksUploaded = 0;

        while (!m_ChunksToUpload.empty() && chunksUploaded < m_Config.ChunksToUploadPerFrame) {
            Renderer::ChunkKey key = m_ChunksToUpload.front();
            m_ChunksToUpload.pop();

            auto it = m_LoadedChunks.find(key);
            if (it == m_LoadedChunks.end()) continue;

            auto& chunk = *it->second;
            if (chunk.State != Renderer::ChunkState::Generated) continue;

            chunk.State = Renderer::ChunkState::Uploading;
            UploadChunkToGPU(chunk);
            chunk.State = Renderer::ChunkState::Ready;

            ++chunksUploaded;
        }
    }

    void TerrainSystem::UnloadDistantChunks(const Math::Vec3& cameraPos, const TerrainComponent& terrain)
    {
        PROFILE_FUNCTION();

        float unloadDistance = terrain.ViewDistance * 1.5f; // Hysteresis to prevent thrashing

        std::vector<Renderer::ChunkKey> toUnload;

        for (auto& [key, chunk] : m_LoadedChunks) {
            Math::Vec3 chunkCenter = chunk->BoundingBox.GetCenter();
            float distance = glm::length(cameraPos - chunkCenter);

            if (distance > unloadDistance) {
                toUnload.push_back(key);
            }
        }

        for (const auto& key : toUnload) {
            auto it = m_LoadedChunks.find(key);
            if (it != m_LoadedChunks.end()) {
                ReleaseChunk(it->second);
                m_LoadedChunks.erase(it);
            }
        }
    }

    std::shared_ptr<Renderer::TerrainChunk> TerrainSystem::AcquireChunk(
        const Math::IVec2& coord, const Math::Vec3& worldPos)
    {
        if (m_Config.EnableChunkPooling) {
            std::lock_guard<std::mutex> lock(m_PoolMutex);
            
            if (!m_ChunkPool.empty()) {
                auto chunk = m_ChunkPool.back();
                m_ChunkPool.pop_back();
                chunk->Reset();
                chunk->ChunkCoord = coord;
                chunk->WorldPosition = worldPos;
                return chunk;
            }
        }

        return std::make_shared<Renderer::TerrainChunk>(coord, worldPos);
    }

    void TerrainSystem::ReleaseChunk(std::shared_ptr<Renderer::TerrainChunk> chunk)
    {
        if (m_Config.EnableChunkPooling) {
            std::lock_guard<std::mutex> lock(m_PoolMutex);
            
            chunk->Reset();
            m_ChunkPool.push_back(chunk);
        }
    }

    Math::IVec2 TerrainSystem::WorldToChunkCoord(const Math::Vec3& worldPos, const TerrainComponent& terrain) const
    {
        float chunkWorldSize = terrain.ChunkSize * terrain.HorizontalScale;
        
        int chunkX = static_cast<int>(std::floor(worldPos.x / chunkWorldSize));
        int chunkZ = static_cast<int>(std::floor(worldPos.z / chunkWorldSize));
        
        return Math::IVec2(chunkX, chunkZ);
    }

    Math::Vec3 TerrainSystem::ChunkCoordToWorld(const Math::IVec2& chunkCoord, const TerrainComponent& terrain) const
    {
        float chunkWorldSize = terrain.ChunkSize * terrain.HorizontalScale;
        
        return Math::Vec3(
            chunkCoord.x * chunkWorldSize,
            0.0f,
            chunkCoord.y * chunkWorldSize
        );
    }

    bool TerrainSystem::IsChunkInRange(const Math::IVec2& chunkCoord, const Math::Vec3& cameraPos,
                                       const TerrainComponent& terrain) const
    {
        Math::Vec3 chunkCenter = ChunkCoordToWorld(chunkCoord, terrain);
        chunkCenter.x += (terrain.ChunkSize * terrain.HorizontalScale) * 0.5f;
        chunkCenter.z += (terrain.ChunkSize * terrain.HorizontalScale) * 0.5f;

        float dx = cameraPos.x - chunkCenter.x;
        float dz = cameraPos.z - chunkCenter.z;
        float distanceSquared = dx * dx + dz * dz;

        return distanceSquared <= terrain.ViewDistance * terrain.ViewDistance;
    }

    bool TerrainSystem::IsChunkVisible(const Renderer::TerrainChunk& chunk, const Math::Mat4& viewProjection) const
    {
        // Simple frustum culling using AABB
        const auto& aabb = chunk.BoundingBox;
        
        // Transform AABB corners to clip space and check if any are visible
        Math::Vec3 corners[8] = {
            Math::Vec3(aabb.Min.x, aabb.Min.y, aabb.Min.z),
            Math::Vec3(aabb.Max.x, aabb.Min.y, aabb.Min.z),
            Math::Vec3(aabb.Min.x, aabb.Max.y, aabb.Min.z),
            Math::Vec3(aabb.Max.x, aabb.Max.y, aabb.Min.z),
            Math::Vec3(aabb.Min.x, aabb.Min.y, aabb.Max.z),
            Math::Vec3(aabb.Max.x, aabb.Min.y, aabb.Max.z),
            Math::Vec3(aabb.Min.x, aabb.Max.y, aabb.Max.z),
            Math::Vec3(aabb.Max.x, aabb.Max.y, aabb.Max.z)
        };

        // Check if all corners are outside any single frustum plane
        int outsideLeft = 0, outsideRight = 0;
        int outsideTop = 0, outsideBottom = 0;
        int outsideNear = 0, outsideFar = 0;

        for (int i = 0; i < 8; ++i) {
            Math::Vec4 clipPos = viewProjection * Math::Vec4(corners[i], 1.0f);
            
            if (clipPos.x < -clipPos.w) ++outsideLeft;
            if (clipPos.x > clipPos.w) ++outsideRight;
            if (clipPos.y < -clipPos.w) ++outsideBottom;
            if (clipPos.y > clipPos.w) ++outsideTop;
            if (clipPos.z < 0) ++outsideNear;
            if (clipPos.z > clipPos.w) ++outsideFar;
        }

        // If all 8 corners are outside any single plane, the box is not visible
        return !(outsideLeft == 8 || outsideRight == 8 ||
                 outsideTop == 8 || outsideBottom == 8 ||
                 outsideNear == 8 || outsideFar == 8);
    }

    void TerrainSystem::UploadChunkToGPU(Renderer::TerrainChunk& chunk)
    {
        PROFILE_FUNCTION();

        if (!m_Device || chunk.Vertices.empty() || chunk.Indices.empty()) {
            return;
        }

        // Create vertex buffer
        RHI::BufferDesc vertexDesc;
        vertexDesc.Size = chunk.Vertices.size() * sizeof(Renderer::TerrainVertex);
        vertexDesc.Usage = RHI::BufferUsage::VertexBuffer;
        vertexDesc.MemoryType = RHI::MemoryType::DeviceLocal;

        chunk.VertexBuffer = m_Device->CreateBuffer(vertexDesc, chunk.Vertices.data());

        // Create index buffer
        RHI::BufferDesc indexDesc;
        indexDesc.Size = chunk.Indices.size() * sizeof(uint32_t);
        indexDesc.Usage = RHI::BufferUsage::IndexBuffer;
        indexDesc.MemoryType = RHI::MemoryType::DeviceLocal;

        chunk.IndexBuffer = m_Device->CreateBuffer(indexDesc, chunk.Indices.data());

        // Optionally clear CPU geometry to save memory
        // chunk.ClearCPUGeometry();

        ENGINE_CORE_TRACE("Uploaded terrain chunk ({}, {}) to GPU: {} vertices, {} indices",
                          chunk.ChunkCoord.x, chunk.ChunkCoord.y,
                          chunk.Vertices.size(), chunk.Indices.size());
    }

} // namespace ECS
} // namespace Core
