#pragma once

// TerrainSystem - Manages terrain chunk loading, LOD, and rendering
// Handles async chunk generation using JobSystem and chunk pooling

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/TerrainComponent.h"
#include "Core/ECS/Components/CameraComponent.h"
#include "Core/Renderer/Terrain/TerrainChunk.h"
#include "Core/Renderer/Terrain/TerrainGenerator.h"
#include "Core/Profile.h"
#include "Core/Log.h"

#include <unordered_map>
#include <vector>
#include <queue>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

namespace Core {
namespace ECS {

    // Configuration for terrain system
    struct TerrainSystemConfig {
        uint32_t MaxLoadedChunks = 256;         // Maximum chunks in memory
        uint32_t ChunksToLoadPerFrame = 4;      // Max chunks to start loading per frame
        uint32_t ChunksToUploadPerFrame = 2;    // Max chunks to upload to GPU per frame
        float ChunkUpdateInterval = 0.1f;       // Seconds between chunk load/unload checks
        bool UseAsyncGeneration = true;         // Use JobSystem for async generation
        bool EnableChunkPooling = true;         // Reuse chunk memory
    };

    class TerrainSystem {
    public:
        TerrainSystem();
        ~TerrainSystem();

        // Non-copyable
        TerrainSystem(const TerrainSystem&) = delete;
        TerrainSystem& operator=(const TerrainSystem&) = delete;

        // Initialize system with device for GPU uploads
        void Initialize(std::shared_ptr<RHI::RHIDevice> device);

        // Shutdown and cleanup
        void Shutdown();

        // Update terrain based on camera position
        void Update(Scene& scene, float deltaTime);

        // Force update of all chunks (e.g., after terrain modification)
        void ForceUpdate();

        // Get loaded chunks for rendering
        const std::unordered_map<Renderer::ChunkKey, std::shared_ptr<Renderer::TerrainChunk>>& 
            GetLoadedChunks() const { return m_LoadedChunks; }

        // Get visible chunks for current frame
        const std::vector<Renderer::TerrainChunk*>& GetVisibleChunks() const { return m_VisibleChunks; }

        // Configuration
        void SetConfig(const TerrainSystemConfig& config) { m_Config = config; }
        const TerrainSystemConfig& GetConfig() const { return m_Config; }

        // Statistics
        uint32_t GetLoadedChunkCount() const { return static_cast<uint32_t>(m_LoadedChunks.size()); }
        uint32_t GetVisibleChunkCount() const { return static_cast<uint32_t>(m_VisibleChunks.size()); }
        uint32_t GetPendingChunkCount() const { return static_cast<uint32_t>(m_ChunksToLoad.size()); }

        // World-partition residency hooks used by the open-world runtime.
        void SetWorldPartitionCellResident(const std::string& cellId, bool resident);
        bool IsWorldPartitionCellResident(const std::string& cellId) const;
        void SetStreamingChunkBudget(uint32_t maxLoadedChunks);

        // Sample terrain height at world position
        float SampleHeight(float worldX, float worldZ, const TerrainComponent& terrain) const;

        // Get terrain normal at world position
        Math::Vec3 SampleNormal(float worldX, float worldZ, const TerrainComponent& terrain) const;

    private:
        // Determine which chunks should be loaded based on camera position
        void UpdateChunkLoading(const Math::Vec3& cameraPos, const TerrainComponent& terrain);

        // Update LOD levels for loaded chunks
        void UpdateChunkLODs(const Math::Vec3& cameraPos, const TerrainComponent& terrain);

        // Perform frustum culling on loaded chunks
        void UpdateVisibility(const Math::Mat4& viewProjection);

        // Start loading new chunks (async or sync)
        void ProcessLoadQueue(const TerrainComponent& terrain);

        // Upload generated chunks to GPU
        void ProcessUploadQueue();

        // Unload chunks that are too far from camera
        void UnloadDistantChunks(const Math::Vec3& cameraPos, const TerrainComponent& terrain);

        // Get chunk from pool or create new
        std::shared_ptr<Renderer::TerrainChunk> AcquireChunk(const Math::IVec2& coord, const Math::Vec3& worldPos);

        // Return chunk to pool
        void ReleaseChunk(std::shared_ptr<Renderer::TerrainChunk> chunk);

        // Convert world position to chunk coordinate
        Math::IVec2 WorldToChunkCoord(const Math::Vec3& worldPos, const TerrainComponent& terrain) const;

        // Convert chunk coordinate to world position
        Math::Vec3 ChunkCoordToWorld(const Math::IVec2& chunkCoord, const TerrainComponent& terrain) const;

        // Check if chunk is within view distance
        bool IsChunkInRange(const Math::IVec2& chunkCoord, const Math::Vec3& cameraPos, 
                           const TerrainComponent& terrain) const;

        // Frustum culling helper
        bool IsChunkVisible(const Renderer::TerrainChunk& chunk, const Math::Mat4& viewProjection) const;

        // Upload chunk buffers to GPU
        void UploadChunkToGPU(Renderer::TerrainChunk& chunk);

        // Configuration
        TerrainSystemConfig m_Config;

        // RHI device for GPU uploads
        std::shared_ptr<RHI::RHIDevice> m_Device;

        // Terrain generator
        std::unique_ptr<Renderer::TerrainGenerator> m_Generator;

        // Loaded chunks (key = chunk coord)
        std::unordered_map<Renderer::ChunkKey, std::shared_ptr<Renderer::TerrainChunk>> m_LoadedChunks;

        // Visible chunks for current frame (pointers to loaded chunks)
        std::vector<Renderer::TerrainChunk*> m_VisibleChunks;

        // Queue of chunks waiting to be loaded
        std::queue<Renderer::ChunkKey> m_ChunksToLoad;

        // Queue of chunks waiting to be uploaded to GPU
        std::queue<Renderer::ChunkKey> m_ChunksToUpload;

        // Chunk pool for memory reuse
        std::vector<std::shared_ptr<Renderer::TerrainChunk>> m_ChunkPool;
        std::mutex m_PoolMutex;

        // Timing
        float m_TimeSinceLastUpdate = 0.0f;

        // State
        bool m_IsInitialized = false;
        bool m_ForceUpdatePending = false;
        std::unordered_set<std::string> m_ResidentWorldPartitionCells;
    };

} // namespace ECS
} // namespace Core
