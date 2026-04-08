// Core/Renderer/AccelerationStructureBuilder.h

#pragma once
#include "../RHI/RHIRayTracing.h"
#include <entt/entt.hpp>
#include <unordered_map>
#include <memory>
#include <vector>
#include <glm/glm.hpp>

namespace AIEngine::Rendering {

// Forward declarations
class MeshAsset;

/// BLAS cache key
struct BLASCacheKey {
    uint64_t meshHandle = 0;
    uint32_t lodLevel = 0;
    
    bool operator==(const BLASCacheKey& other) const {
        return meshHandle == other.meshHandle && lodLevel == other.lodLevel;
    }
};

/// Hash function for BLASCacheKey
struct BLASCacheKeyHash {
    size_t operator()(const BLASCacheKey& key) const {
        return std::hash<uint64_t>()(key.meshHandle) ^ 
               (std::hash<uint32_t>()(key.lodLevel) << 1);
    }
};

/// BLAS entry in cache
struct BLASEntry {
    std::unique_ptr<RHI::AccelerationStructure> blas;
    std::unique_ptr<RHI::RHIBuffer> scratchBuffer;
    bool isBuilt = false;
    bool needsRebuild = false;
    bool isCompacted = false;
    uint64_t lastUsedFrame = 0;
    uint64_t originalSize = 0;
    uint64_t compactedSize = 0;
};

/// TLAS instance entry
struct TLASInstance {
    entt::entity entity = entt::null;
    uint64_t blasAddress = 0;
    glm::mat4 transform = glm::mat4(1.0f);
    uint32_t customIndex = 0;
    uint32_t mask = 0xFF;
    uint32_t hitGroupOffset = 0;
    uint32_t flags = 0;
    bool isStatic = false;
};

/// Instance flags
enum class InstanceFlags : uint32_t {
    None = 0,
    TriangleFacingCullDisable = 1 << 0,
    TriangleFrontCounterClockwise = 1 << 1,
    ForceOpaque = 1 << 2,
    ForceNoOpaque = 1 << 3
};

inline InstanceFlags operator|(InstanceFlags a, InstanceFlags b) {
    return static_cast<InstanceFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

/// Build mode for TLAS
enum class TLASBuildMode {
    FullRebuild,        ///< Complete rebuild of TLAS
    TransformUpdate,    ///< Update transforms only (faster)
    Incremental         ///< Add/remove instances incrementally
};

/// Acceleration structure builder
class AccelerationStructureBuilder {
public:
    AccelerationStructureBuilder(RHI::RHIDeviceRT* device);
    ~AccelerationStructureBuilder();
    
    /// Initialize the builder
    bool Initialize();
    
    /// Shutdown and cleanup
    void Shutdown();
    
    /// Build or get BLAS for a mesh
    RHI::AccelerationStructure* GetOrCreateBLAS(
        const MeshAsset* mesh,
        uint32_t lodLevel = 0);
    
    /// Mark BLAS for rebuild (e.g., mesh deformation)
    void MarkBLASForRebuild(const MeshAsset* mesh);
    
    /// Mark all BLAS for rebuild
    void MarkAllBLASForRebuild();
    
    /// Build pending BLAS structures
    void BuildPendingBLAS(RHI::RHICommandBufferRT* cmdBuffer);
    
    /// Build TLAS from scene entities
    void BuildTLAS(entt::registry& registry,
                   RHI::RHICommandBufferRT* cmdBuffer,
                   TLASBuildMode mode = TLASBuildMode::FullRebuild);
    
    /// Get the scene TLAS
    RHI::AccelerationStructure* GetTLAS() const { return m_TLAS.get(); }
    
    /// Update TLAS transforms only (faster than full rebuild)
    void UpdateTLASTransforms(entt::registry& registry,
                              RHI::RHICommandBufferRT* cmdBuffer);
    
    /// Force full TLAS rebuild
    void MarkTLASForRebuild() { m_TLASNeedsRebuild = true; }
    
    /// Compact all built BLAS structures
    void CompactBLASStructures(RHI::RHICommandBufferRT* cmdBuffer);
    
    /// Clear BLAS cache for unused meshes
    void PruneUnusedBLAS(uint64_t currentFrame, uint64_t maxUnusedFrames = 60);
    
    /// Clear entire BLAS cache
    void ClearBLASCache();
    
    /// Get current frame number (for LRU tracking)
    void SetCurrentFrame(uint64_t frame) { m_CurrentFrame = frame; }
    
    /// Add custom instance to TLAS
    void AddCustomInstance(const TLASInstance& instance);
    
    /// Remove custom instance
    void RemoveCustomInstance(entt::entity entity);
    
    /// Set visibility mask for an entity
    void SetInstanceMask(entt::entity entity, uint32_t mask);
    
    /// Set hit group offset for an entity
    void SetInstanceHitGroup(entt::entity entity, uint32_t hitGroupOffset);
    
    /// Get statistics
    struct Stats {
        uint32_t blasCount = 0;
        uint32_t blasBuiltThisFrame = 0;
        uint32_t blasCompactedCount = 0;
        uint32_t tlasInstanceCount = 0;
        uint32_t staticInstanceCount = 0;
        uint32_t dynamicInstanceCount = 0;
        uint64_t totalBLASMemory = 0;
        uint64_t totalBLASCompactedMemory = 0;
        uint64_t tlasMemory = 0;
        uint64_t scratchMemory = 0;
        float lastBLASBuildTimeMs = 0.0f;
        float lastTLASBuildTimeMs = 0.0f;
    };
    Stats GetStats() const { return m_Stats; }
    
    /// Reset frame statistics
    void ResetFrameStats();
    
    /// Check if TLAS is valid and built
    bool IsTLASValid() const { return m_TLAS != nullptr && !m_TLASNeedsRebuild; }
    
    /// Get device
    RHI::RHIDeviceRT* GetDevice() const { return m_Device; }
    
private:
    BLASEntry* CreateBLASEntry(const MeshAsset* mesh, uint32_t lodLevel);
    void BuildBLASEntry(BLASEntry* entry, const MeshAsset* mesh,
                        RHI::RHICommandBufferRT* cmdBuffer);
    void CompactBLASEntry(BLASEntry* entry, RHI::RHICommandBufferRT* cmdBuffer);
    void CollectTLASInstances(entt::registry& registry);
    void UpdateInstanceBuffer();
    uint64_t CalculateMeshHandle(const MeshAsset* mesh) const;
    
    glm::mat3x4 ConvertToMat3x4(const glm::mat4& mat) const;
    
    RHI::RHIDeviceRT* m_Device = nullptr;
    
    // BLAS cache
    std::unordered_map<BLASCacheKey, BLASEntry, BLASCacheKeyHash> m_BLASCache;
    std::vector<BLASEntry*> m_PendingBLASBuilds;
    std::vector<BLASEntry*> m_PendingCompaction;
    
    // TLAS
    std::unique_ptr<RHI::AccelerationStructure> m_TLAS;
    std::unique_ptr<RHI::RHIBuffer> m_TLASScratchBuffer;
    std::unique_ptr<RHI::RHIBuffer> m_InstanceBuffer;
    std::vector<TLASInstance> m_TLASInstances;
    std::vector<TLASInstance> m_CustomInstances;
    bool m_TLASNeedsRebuild = true;
    
    // Per-instance overrides
    std::unordered_map<entt::entity, uint32_t> m_InstanceMasks;
    std::unordered_map<entt::entity, uint32_t> m_InstanceHitGroups;
    
    // State
    uint64_t m_CurrentFrame = 0;
    Stats m_Stats;
    
    // Scratch buffer pooling
    struct ScratchBufferPool {
        std::vector<std::unique_ptr<RHI::RHIBuffer>> buffers;
        std::vector<bool> inUse;
    };
    ScratchBufferPool m_ScratchPool;
    RHI::RHIBuffer* AcquireScratchBuffer(uint64_t minSize);
    void ReleaseScratchBuffer(RHI::RHIBuffer* buffer);
};

} // namespace AIEngine::Rendering
