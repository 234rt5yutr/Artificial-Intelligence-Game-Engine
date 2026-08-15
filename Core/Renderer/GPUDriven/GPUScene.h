#pragma once

// GPU scene: the residency side of GPU-driven rendering.
//
// A GPU culling pass can only produce draw arguments the GPU can execute if the
// geometry it selects lives in buffers the GPU can index without the CPU
// rebinding anything. `Mesh::UploadToGPU` gives every mesh its *own*
// vertex/index buffer, which forces one bind per mesh and makes indirect draws
// pointless.
//
// GPUScene keeps a merged vertex buffer and a merged index buffer for the whole
// scene. A mesh is registered once (clusterised, reordered, copied in) and from
// then on it is addressed purely by offsets, so an entire frame is one vertex
// bind, one index bind, and one indirect draw per material.

#include "Core/Math/Math.h"
#include "Core/RHI/Vulkan/VulkanGpuResources.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Core {
namespace ECS { struct DrawCommand; }
namespace RHI { class VulkanContext; }

namespace Renderer {

    class Mesh;

    // One cluster of up to `kMaxClusterTriangles` triangles, contiguous in the
    // merged index buffer. Layout matches the GLSL struct in the cull shader.
    struct GpuCluster {
        Math::Vec4 CenterRadius{0.0f};    // xyz = local-space centre, w = radius
        Math::Vec4 ConeAxisCutoff{0.0f};  // xyz = average normal, w = cos(half-angle); w >= 1 disables
        uint32_t FirstIndex = 0;          // into the merged index buffer
        uint32_t IndexCount = 0;
        uint32_t VertexOffset = 0;        // into the merged vertex buffer
        uint32_t Pad = 0;
    };
    static_assert(sizeof(GpuCluster) == 48, "GpuCluster must match the cull shader layout");

    // One drawable instance for this frame.
    struct GpuInstance {
        Math::Mat4 Transform{1.0f};
        Math::Vec4 BoundsCenterRadius{0.0f}; // local-space bounds of the whole mesh
        uint32_t ClusterBase = 0;            // into the shared cluster buffer
        uint32_t ClusterCount = 0;
        uint32_t MaterialIndex = 0;
        uint32_t Flags = 0;                  // bit 0: cast shadows
        // Non-zero for a skinned instance: the arena slice its posed vertices
        // were written to. The cull shader emits this as the draw's
        // vertexOffset, which is what lets skinned and static geometry share one
        // indirect path.
        uint32_t VertexOffset = 0;
        uint32_t Pad0 = 0;
        uint32_t Pad1 = 0;
        uint32_t Pad2 = 0;
    };
    static_assert(sizeof(GpuInstance) == 112, "GpuInstance must match the cull shader layout");

    // Per-mesh residency record.
    // One primitive of a mesh. A mesh with several primitives shades each with
    // its own material, which is what glTF files actually contain; drawing the
    // whole mesh with one material index applied the first one to everything.
    struct GpuMeshSection {
        uint32_t ClusterBase = 0;
        uint32_t ClusterCount = 0;
        // The primitive's material *within its own file*. The global index is
        // this plus the draw command's, because the importer registers a file's
        // materials contiguously and stores the first one on the component.
        uint32_t MaterialSlot = 0;
        Math::Vec4 BoundsCenterRadius{0.0f};
    };

    struct GpuMeshRecord {
        uint32_t VertexOffset = 0;   // in vertices
        uint32_t VertexCount = 0;
        uint32_t IndexOffset = 0;    // in indices
        uint32_t IndexCount = 0;
        uint32_t ClusterBase = 0;
        uint32_t ClusterCount = 0;
        Math::Vec4 BoundsCenterRadius{0.0f};
        std::vector<GpuMeshSection> Sections;
        // Skinned meshes keep their bind-pose copy in the arena for
        // clusterisation, plus their source vertices in the skinned buffer. Each
        // *instance* then gets its own dynamic slice, because two copies of a
        // character hold different poses.
        bool Skinned = false;
        uint32_t SkinnedSourceOffset = 0;
        uint32_t BoneCount = 0;
    };

    // A contiguous run of instances sharing one material, so the frame can issue
    // one indirect draw per material instead of one per object.
    struct GpuMaterialBatch {
        uint32_t MaterialIndex = 0;
        uint32_t FirstClusterSlot = 0;
        uint32_t ClusterSlotCount = 0;
        // Blended batches come last and are sorted back to front, so the caller
        // can draw them after everything they show through.
        bool Transparent = false;
    };

    struct GpuSceneStats {
        uint32_t ResidentMeshes = 0;
        uint32_t ResidentClusters = 0;
        uint32_t ResidentTriangles = 0;
        uint32_t FrameInstances = 0;
        uint32_t FrameClusterSlots = 0;
        uint32_t FrameMaterialBatches = 0;
        uint32_t RejectedMeshes = 0;
        uint32_t SkinnedInstances = 0;
        uint32_t SkinnedVerticesUsed = 0;
        uint32_t SkinnedVerticesCapacity = 0;
        uint32_t TransparentInstances = 0;
        uint32_t TransparentBatches = 0;
        uint64_t VertexBytesUsed = 0;
        uint64_t IndexBytesUsed = 0;
        uint64_t VertexBytesCapacity = 0;
        uint64_t IndexBytesCapacity = 0;
    };

    // ponytail: fixed-capacity arenas, no growth. Overflow rejects the mesh with
    // a logged reason and it falls back to the per-mesh draw path; add a
    // grow-and-recopy step only if real content actually hits a ceiling.
    struct GpuSceneLimits {
        uint32_t MaxVertices = 1024u * 1024u;   // 1M verts * 48 B = 48 MB
        uint32_t MaxIndices = 3u * 1024u * 1024u; // 3M indices = 12 MB
        uint32_t MaxClusters = 65536u;
        uint32_t MaxInstances = 8192u;
        uint32_t MaxClusterSlots = 65536u;
        uint32_t MaxClusterTriangles = 128u;
        // Vertices reserved at the top of the arena for per-instance skinning
        // output. Taken out of MaxVertices, not added to it.
        uint32_t MaxSkinnedVertices = 256u * 1024u;
        uint32_t MaxSkinnedSourceVertices = 256u * 1024u;
    };

    class GPUScene {
    public:
        GPUScene() = default;
        ~GPUScene();

        GPUScene(const GPUScene&) = delete;
        GPUScene& operator=(const GPUScene&) = delete;

        bool Initialize(RHI::VulkanContext* context, const GpuSceneLimits& limits = {});
        void Shutdown();
        bool IsInitialized() const { return m_Context != nullptr; }

        // Registers a mesh if it is not resident yet. Clusterises and copies it
        // into the merged buffers. Returns nullptr when the mesh has no
        // geometry or the buffers are full (logged once per mesh).
        const GpuMeshRecord* EnsureResident(const Mesh* mesh);

        // Rebuilds the per-frame instance list from the draw commands. Sorts by
        // material so each material becomes one indirect draw. Returns the total
        // number of cluster slots the cull pass must dispatch over.
        // The camera position orders blended instances back to front; without it
        // transparency composites in whatever order the draw list happened to
        // arrive in.
        uint32_t BeginFrame(const ECS::DrawCommand* commands, std::size_t commandCount,
                            const Math::Vec3& cameraPosition = Math::Vec3(0.0f));

        const std::vector<GpuMaterialBatch>& GetMaterialBatches() const { return m_MaterialBatches; }
        uint32_t GetFrameInstanceCount() const { return static_cast<uint32_t>(m_FrameInstances.size()); }
        uint32_t GetFrameClusterSlotCount() const { return m_FrameClusterSlots; }

        VkBuffer GetVertexBuffer() const { return m_VertexBuffer.Buffer; }
        VkBuffer GetIndexBuffer() const { return m_IndexBuffer.Buffer; }
        VkBuffer GetInstanceBuffer() const { return m_InstanceBuffer.Buffer; }
        VkBuffer GetClusterBuffer() const { return m_ClusterBuffer.Buffer; }
        // Prefix sum of cluster counts across this frame's instances, so the cull
        // shader can map a flat thread id back to its instance with a binary
        // search instead of the CPU expanding every cluster every frame.
        VkBuffer GetInstanceOffsetBuffer() const { return m_InstanceOffsetBuffer.Buffer; }
        VkBuffer GetSkinnedSourceBuffer() const { return m_SkinnedSourceBuffer.Buffer; }

        // Skinning work the caller must dispatch this frame, produced by
        // BeginFrame. Empty when nothing skinned is visible.
        struct PendingSkin {
            uint32_t SourceVertexOffset = 0;
            uint32_t TargetVertexOffset = 0;
            uint32_t VertexCount = 0;
            uint32_t BoneCount = 0;
            // Range into FrameRenderData::BoneMatrices, copied straight from the
            // draw command that produced this instance.
            uint32_t BoneOffset = 0;
            const Mesh* SourceMesh = nullptr;
        };
        const std::vector<PendingSkin>& GetPendingSkins() const { return m_PendingSkins; }
        // Sends every instance sharing this skinning slice back to the shared
        // bind pose, for when the pose could not be uploaded. Matched by slice
        // rather than by index because one mesh contributes one instance per
        // primitive, and those are not adjacent once sorted by material.
        void ClearSkinnedInstances(uint32_t targetVertexOffset);

        const GpuSceneStats& GetStats() const { return m_Stats; }
        const GpuSceneLimits& GetLimits() const { return m_Limits; }

    private:
        bool AllocateBuffers();
        bool UploadMesh(const Mesh* mesh, GpuMeshRecord& record);

        RHI::VulkanContext* m_Context = nullptr;
        GpuSceneLimits m_Limits{};

        RHI::GpuBuffer m_VertexBuffer{};
        RHI::GpuBuffer m_IndexBuffer{};
        RHI::GpuBuffer m_ClusterBuffer{};
        RHI::GpuBuffer m_InstanceBuffer{};
        RHI::GpuBuffer m_InstanceOffsetBuffer{};

        uint32_t m_VertexCursor = 0;
        uint32_t m_IndexCursor = 0;
        uint32_t m_ClusterCursor = 0;

        std::unordered_map<const Mesh*, GpuMeshRecord> m_Residency;
        std::unordered_map<const Mesh*, bool> m_Rejected;

        RHI::GpuBuffer m_SkinnedSourceBuffer{};
        uint32_t m_SkinnedSourceCursor = 0;
        // Reset every frame: skinning output is per instance, so last frame's
        // slices mean nothing.
        uint32_t m_SkinnedVertexCursor = 0;
        uint32_t m_SkinnedRegionStart = 0;
        std::vector<PendingSkin> m_PendingSkins;

        std::vector<GpuInstance> m_FrameInstances;
        std::vector<uint32_t> m_FrameInstanceOffsets;
        std::vector<GpuMaterialBatch> m_MaterialBatches;
        uint32_t m_FrameClusterSlots = 0;

        GpuSceneStats m_Stats{};
    };

    // ------------------------------------------------------------------------
    // Clusterisation
    // ------------------------------------------------------------------------

    struct MeshClusterSet {
        std::vector<uint32_t> ReorderedIndices;
        std::vector<GpuCluster> Clusters;
        Math::Vec4 BoundsCenterRadius{0.0f};
    };

    // Splits a triangle list into spatially coherent clusters and reorders the
    // index buffer so every cluster is one contiguous index range. Exposed for
    // testing; GPUScene calls it during registration.
    //
    // `positionsStride` lets this run over both Vertex and SkinnedVertex without
    // a template or a copy.
    MeshClusterSet BuildMeshClusters(const void* positions,
                                     std::size_t positionsStride,
                                     std::size_t vertexCount,
                                     const std::vector<uint32_t>& indices,
                                     uint32_t maxClusterTriangles);

} // namespace Renderer
} // namespace Core
