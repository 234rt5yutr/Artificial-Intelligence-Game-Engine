#include "GPUScene.h"

#include "Core/ECS/Systems/RenderSystem.h"
#include "Core/Log.h"
#include "Core/RHI/Vulkan/VulkanContext.h"
#include "Core/Renderer/Mesh.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace Core {
namespace Renderer {

    namespace {

        // Expand a 10-bit integer into 30 bits with two zero bits between each.
        uint32_t ExpandBits10(uint32_t v) {
            v = (v * 0x00010001u) & 0xFF0000FFu;
            v = (v * 0x00000101u) & 0x0F00F00Fu;
            v = (v * 0x00000011u) & 0xC30C30C3u;
            v = (v * 0x00000005u) & 0x49249249u;
            return v;
        }

        uint32_t MortonCode(const Math::Vec3& normalized) {
            const float x = std::clamp(normalized.x, 0.0f, 1.0f) * 1023.0f;
            const float y = std::clamp(normalized.y, 0.0f, 1.0f) * 1023.0f;
            const float z = std::clamp(normalized.z, 0.0f, 1.0f) * 1023.0f;
            return (ExpandBits10(static_cast<uint32_t>(x)) << 2) |
                   (ExpandBits10(static_cast<uint32_t>(y)) << 1) |
                    ExpandBits10(static_cast<uint32_t>(z));
        }

        const Math::Vec3& PositionAt(const void* positions, std::size_t stride, std::size_t index) {
            const auto* base = static_cast<const uint8_t*>(positions);
            return *reinterpret_cast<const Math::Vec3*>(base + index * stride);
        }

    } // namespace

    // ========================================================================
    // Clusterisation
    // ========================================================================

    MeshClusterSet BuildMeshClusters(const void* positions,
                                     std::size_t positionsStride,
                                     std::size_t vertexCount,
                                     const std::vector<uint32_t>& indices,
                                     uint32_t maxClusterTriangles) {
        MeshClusterSet result;
        if (!positions || positionsStride < sizeof(Math::Vec3) || vertexCount == 0 ||
            indices.size() < 3 || maxClusterTriangles == 0) {
            return result;
        }

        const std::size_t triangleCount = indices.size() / 3;

        Math::Vec3 meshMin(std::numeric_limits<float>::max());
        Math::Vec3 meshMax(std::numeric_limits<float>::lowest());
        for (std::size_t i = 0; i < vertexCount; ++i) {
            const Math::Vec3& p = PositionAt(positions, positionsStride, i);
            meshMin = glm::min(meshMin, p);
            meshMax = glm::max(meshMax, p);
        }
        const Math::Vec3 meshExtent = glm::max(meshMax - meshMin, Math::Vec3(1e-6f));
        const Math::Vec3 meshCenter = (meshMin + meshMax) * 0.5f;
        result.BoundsCenterRadius = Math::Vec4(meshCenter, glm::length(meshMax - meshCenter));

        // Sort triangles along a Morton curve so a cluster's triangles are
        // spatially adjacent. Sequential order is usually coherent already, but
        // it collapses badly on meshes authored as separate shells.
        struct SortedTriangle {
            uint32_t Code;
            uint32_t Index;
        };
        std::vector<SortedTriangle> sorted(triangleCount);
        for (std::size_t t = 0; t < triangleCount; ++t) {
            const Math::Vec3& a = PositionAt(positions, positionsStride, indices[t * 3 + 0] % vertexCount);
            const Math::Vec3& b = PositionAt(positions, positionsStride, indices[t * 3 + 1] % vertexCount);
            const Math::Vec3& c = PositionAt(positions, positionsStride, indices[t * 3 + 2] % vertexCount);
            const Math::Vec3 centroid = (a + b + c) / 3.0f;
            sorted[t].Code = MortonCode((centroid - meshMin) / meshExtent);
            sorted[t].Index = static_cast<uint32_t>(t);
        }
        std::sort(sorted.begin(), sorted.end(),
                  [](const SortedTriangle& lhs, const SortedTriangle& rhs) {
                      return lhs.Code < rhs.Code;
                  });

        result.ReorderedIndices.reserve(indices.size());
        const std::size_t clusterCount = (triangleCount + maxClusterTriangles - 1) / maxClusterTriangles;
        result.Clusters.reserve(clusterCount);

        for (std::size_t start = 0; start < triangleCount; start += maxClusterTriangles) {
            const std::size_t end = std::min(start + maxClusterTriangles, triangleCount);

            GpuCluster cluster;
            cluster.FirstIndex = static_cast<uint32_t>(result.ReorderedIndices.size());
            cluster.IndexCount = static_cast<uint32_t>((end - start) * 3);

            Math::Vec3 clusterMin(std::numeric_limits<float>::max());
            Math::Vec3 clusterMax(std::numeric_limits<float>::lowest());
            Math::Vec3 normalSum(0.0f);
            std::vector<Math::Vec3> triangleNormals;
            triangleNormals.reserve(end - start);

            for (std::size_t t = start; t < end; ++t) {
                const uint32_t tri = sorted[t].Index;
                const uint32_t i0 = indices[tri * 3 + 0];
                const uint32_t i1 = indices[tri * 3 + 1];
                const uint32_t i2 = indices[tri * 3 + 2];
                result.ReorderedIndices.push_back(i0);
                result.ReorderedIndices.push_back(i1);
                result.ReorderedIndices.push_back(i2);

                const Math::Vec3& a = PositionAt(positions, positionsStride, i0 % vertexCount);
                const Math::Vec3& b = PositionAt(positions, positionsStride, i1 % vertexCount);
                const Math::Vec3& c = PositionAt(positions, positionsStride, i2 % vertexCount);
                clusterMin = glm::min(clusterMin, glm::min(a, glm::min(b, c)));
                clusterMax = glm::max(clusterMax, glm::max(a, glm::max(b, c)));

                const Math::Vec3 cross = glm::cross(b - a, c - a);
                const float area = glm::length(cross);
                if (area > 1e-12f) {
                    const Math::Vec3 normal = cross / area;
                    triangleNormals.push_back(normal);
                    normalSum += normal * area; // area-weighted, so slivers do not dominate
                }
            }

            const Math::Vec3 center = (clusterMin + clusterMax) * 0.5f;
            cluster.CenterRadius = Math::Vec4(center, glm::length(clusterMax - center));

            // Backface cone (meshoptimizer's formulation): the cluster is
            // entirely backfacing when dot(normalize(center - eye), axis) is at
            // least the cutoff. A cluster whose normals span more than a
            // hemisphere gets a cutoff that can never trigger.
            cluster.ConeAxisCutoff = Math::Vec4(0.0f, 0.0f, 1.0f, 2.0f);
            const float axisLength = glm::length(normalSum);
            if (axisLength > 1e-6f && !triangleNormals.empty()) {
                const Math::Vec3 axis = normalSum / axisLength;
                float minDot = 1.0f;
                for (const Math::Vec3& normal : triangleNormals) {
                    minDot = std::min(minDot, glm::dot(normal, axis));
                }
                if (minDot > 0.01f) {
                    const float cutoff = std::sqrt(std::max(0.0f, 1.0f - minDot * minDot)) / minDot;
                    cluster.ConeAxisCutoff = Math::Vec4(axis, std::min(cutoff, 1.0f));
                }
            }

            result.Clusters.push_back(cluster);
        }

        return result;
    }

    // ========================================================================
    // Residency
    // ========================================================================

    GPUScene::~GPUScene() {
        Shutdown();
    }

    bool GPUScene::Initialize(RHI::VulkanContext* context, const GpuSceneLimits& limits) {
        if (!context || context->GetAllocator() == VK_NULL_HANDLE) {
            ENGINE_CORE_ERROR("GPUScene::Initialize requires an initialized Vulkan context");
            return false;
        }
        Shutdown();
        m_Context = context;
        m_Limits = limits;
        if (!AllocateBuffers()) {
            Shutdown();
            return false;
        }
        m_Stats.VertexBytesCapacity = m_VertexBuffer.Size;
        m_Stats.IndexBytesCapacity = m_IndexBuffer.Size;
        ENGINE_CORE_INFO("GPUScene ready: {} MB vertices, {} MB indices, {} clusters, {} instances",
                         m_VertexBuffer.Size / (1024 * 1024), m_IndexBuffer.Size / (1024 * 1024),
                         m_Limits.MaxClusters, m_Limits.MaxInstances);
        return true;
    }

    bool GPUScene::AllocateBuffers() {
        VmaAllocator allocator = m_Context->GetAllocator();

        // ponytail: host-visible arenas. The RHI still has no transfer-queue
        // submission path, so a device-local arena could not be filled; revisit
        // when VulkanDevice owns command lists.
        const bool ok =
            RHI::CreateGpuBuffer(allocator,
                                 static_cast<VkDeviceSize>(m_Limits.MaxVertices) * sizeof(Vertex),
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                 true, m_VertexBuffer) &&
            RHI::CreateGpuBuffer(allocator,
                                 static_cast<VkDeviceSize>(m_Limits.MaxIndices) * sizeof(uint32_t),
                                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                 true, m_IndexBuffer) &&
            RHI::CreateGpuBuffer(allocator,
                                 static_cast<VkDeviceSize>(m_Limits.MaxClusters) * sizeof(GpuCluster),
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, m_ClusterBuffer) &&
            RHI::CreateGpuBuffer(allocator,
                                 static_cast<VkDeviceSize>(m_Limits.MaxInstances) * sizeof(GpuInstance),
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, m_InstanceBuffer) &&
            RHI::CreateGpuBuffer(allocator,
                                 static_cast<VkDeviceSize>(m_Limits.MaxInstances + 1) * sizeof(uint32_t),
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, m_InstanceOffsetBuffer);

        if (!ok) {
            ENGINE_CORE_ERROR("GPUScene failed to allocate its merged geometry arenas");
        }
        return ok;
    }

    void GPUScene::Shutdown() {
        if (!m_Context) {
            return;
        }
        VmaAllocator allocator = m_Context->GetAllocator();
        RHI::DestroyGpuBuffer(allocator, m_VertexBuffer);
        RHI::DestroyGpuBuffer(allocator, m_IndexBuffer);
        RHI::DestroyGpuBuffer(allocator, m_ClusterBuffer);
        RHI::DestroyGpuBuffer(allocator, m_InstanceBuffer);
        RHI::DestroyGpuBuffer(allocator, m_InstanceOffsetBuffer);

        m_Residency.clear();
        m_Rejected.clear();
        m_FrameInstances.clear();
        m_FrameInstanceOffsets.clear();
        m_MaterialBatches.clear();
        m_VertexCursor = 0;
        m_IndexCursor = 0;
        m_ClusterCursor = 0;
        m_FrameClusterSlots = 0;
        m_Stats = GpuSceneStats{};
        m_Context = nullptr;
    }

    const GpuMeshRecord* GPUScene::EnsureResident(const Mesh* mesh) {
        if (!m_Context || !mesh) {
            return nullptr;
        }

        auto resident = m_Residency.find(mesh);
        if (resident != m_Residency.end()) {
            return &resident->second;
        }
        if (m_Rejected.find(mesh) != m_Rejected.end()) {
            return nullptr;
        }

        GpuMeshRecord record{};
        if (!UploadMesh(mesh, record)) {
            m_Rejected[mesh] = true;
            ++m_Stats.RejectedMeshes;
            return nullptr;
        }

        auto inserted = m_Residency.emplace(mesh, record).first;
        m_Stats.ResidentMeshes = static_cast<uint32_t>(m_Residency.size());
        m_Stats.ResidentClusters = m_ClusterCursor;
        m_Stats.ResidentTriangles += record.IndexCount / 3;
        m_Stats.VertexBytesUsed = static_cast<uint64_t>(m_VertexCursor) * sizeof(Vertex);
        m_Stats.IndexBytesUsed = static_cast<uint64_t>(m_IndexCursor) * sizeof(uint32_t);
        return &inserted->second;
    }

    bool GPUScene::UploadMesh(const Mesh* mesh, GpuMeshRecord& record) {
        // Skinned meshes keep the per-mesh draw path: the merged arena stores one
        // vertex layout, and a skinned vertex is a different size. They are drawn
        // directly rather than through the indirect path.
        if (mesh->IsSkeletal()) {
            ENGINE_CORE_TRACE("GPUScene: skeletal mesh stays on the direct draw path");
            return false;
        }
        if (mesh->vertices.empty() || mesh->indices.size() < 3) {
            return false;
        }

        const MeshClusterSet clusters = BuildMeshClusters(
            mesh->vertices.data(), sizeof(Vertex), mesh->vertices.size(),
            mesh->indices, m_Limits.MaxClusterTriangles);
        if (clusters.Clusters.empty()) {
            ENGINE_CORE_WARN("GPUScene: clusterisation produced no clusters; mesh stays on the direct path");
            return false;
        }

        const uint32_t vertexCount = static_cast<uint32_t>(mesh->vertices.size());
        const uint32_t indexCount = static_cast<uint32_t>(clusters.ReorderedIndices.size());
        const uint32_t clusterCount = static_cast<uint32_t>(clusters.Clusters.size());

        if (m_VertexCursor + vertexCount > m_Limits.MaxVertices ||
            m_IndexCursor + indexCount > m_Limits.MaxIndices ||
            m_ClusterCursor + clusterCount > m_Limits.MaxClusters) {
            ENGINE_CORE_WARN("GPUScene arenas are full ({} verts, {} indices, {} clusters resident); "
                             "mesh stays on the direct draw path",
                             m_VertexCursor, m_IndexCursor, m_ClusterCursor);
            return false;
        }

        auto* vertexArena = static_cast<Vertex*>(m_VertexBuffer.Mapped);
        auto* indexArena = static_cast<uint32_t*>(m_IndexBuffer.Mapped);
        auto* clusterArena = static_cast<GpuCluster*>(m_ClusterBuffer.Mapped);
        if (!vertexArena || !indexArena || !clusterArena) {
            ENGINE_CORE_ERROR("GPUScene arenas are not host-mapped");
            return false;
        }

        std::memcpy(vertexArena + m_VertexCursor, mesh->vertices.data(), vertexCount * sizeof(Vertex));
        std::memcpy(indexArena + m_IndexCursor, clusters.ReorderedIndices.data(),
                    indexCount * sizeof(uint32_t));

        // Cluster index ranges are mesh-local; rebase them onto the arena so the
        // draw arguments the GPU writes need no per-mesh fix-up.
        for (uint32_t i = 0; i < clusterCount; ++i) {
            GpuCluster cluster = clusters.Clusters[i];
            cluster.FirstIndex += m_IndexCursor;
            cluster.VertexOffset = m_VertexCursor;
            clusterArena[m_ClusterCursor + i] = cluster;
        }

        record.VertexOffset = m_VertexCursor;
        record.VertexCount = vertexCount;
        record.IndexOffset = m_IndexCursor;
        record.IndexCount = indexCount;
        record.ClusterBase = m_ClusterCursor;
        record.ClusterCount = clusterCount;
        record.BoundsCenterRadius = clusters.BoundsCenterRadius;

        m_VertexCursor += vertexCount;
        m_IndexCursor += indexCount;
        m_ClusterCursor += clusterCount;
        return true;
    }

    uint32_t GPUScene::BeginFrame(const ECS::DrawCommand* commands, std::size_t commandCount) {
        m_FrameInstances.clear();
        m_FrameInstanceOffsets.clear();
        m_MaterialBatches.clear();
        m_FrameClusterSlots = 0;

        if (!m_Context || !commands || commandCount == 0) {
            return 0;
        }

        // Sort by material so each material becomes one contiguous cluster-slot
        // range and therefore one indirect draw.
        struct PendingInstance {
            const GpuMeshRecord* Record;
            const ECS::DrawCommand* Command;
        };
        std::vector<PendingInstance> pending;
        pending.reserve(commandCount);

        for (std::size_t i = 0; i < commandCount; ++i) {
            const ECS::DrawCommand& command = commands[i];
            const GpuMeshRecord* record = EnsureResident(command.Mesh);
            if (!record) {
                continue;
            }
            if (pending.size() >= m_Limits.MaxInstances) {
                ENGINE_CORE_WARN("GPUScene instance limit ({}) reached; remaining draws are dropped this frame",
                                 m_Limits.MaxInstances);
                break;
            }
            pending.push_back({record, &command});
        }

        std::stable_sort(pending.begin(), pending.end(),
                         [](const PendingInstance& lhs, const PendingInstance& rhs) {
                             return lhs.Command->MaterialIndex < rhs.Command->MaterialIndex;
                         });

        m_FrameInstances.reserve(pending.size());
        m_FrameInstanceOffsets.reserve(pending.size() + 1);

        uint32_t slotCursor = 0;
        for (const auto& item : pending) {
            if (slotCursor + item.Record->ClusterCount > m_Limits.MaxClusterSlots) {
                ENGINE_CORE_WARN("GPUScene cluster-slot limit ({}) reached; remaining draws are dropped this frame",
                                 m_Limits.MaxClusterSlots);
                break;
            }

            GpuInstance instance;
            instance.Transform = item.Command->Transform;
            instance.BoundsCenterRadius = item.Record->BoundsCenterRadius;
            instance.ClusterBase = item.Record->ClusterBase;
            instance.ClusterCount = item.Record->ClusterCount;
            instance.MaterialIndex = item.Command->MaterialIndex;
            instance.Flags = item.Command->CastShadows ? 1u : 0u;

            if (m_MaterialBatches.empty() ||
                m_MaterialBatches.back().MaterialIndex != instance.MaterialIndex) {
                m_MaterialBatches.push_back({instance.MaterialIndex, slotCursor, 0});
            }
            m_MaterialBatches.back().ClusterSlotCount += item.Record->ClusterCount;

            m_FrameInstanceOffsets.push_back(slotCursor);
            m_FrameInstances.push_back(instance);
            slotCursor += item.Record->ClusterCount;
        }
        m_FrameInstanceOffsets.push_back(slotCursor);
        m_FrameClusterSlots = slotCursor;

        if (auto* instanceArena = static_cast<GpuInstance*>(m_InstanceBuffer.Mapped)) {
            std::memcpy(instanceArena, m_FrameInstances.data(),
                        m_FrameInstances.size() * sizeof(GpuInstance));
        }
        if (auto* offsetArena = static_cast<uint32_t*>(m_InstanceOffsetBuffer.Mapped)) {
            std::memcpy(offsetArena, m_FrameInstanceOffsets.data(),
                        m_FrameInstanceOffsets.size() * sizeof(uint32_t));
        }

        m_Stats.FrameInstances = static_cast<uint32_t>(m_FrameInstances.size());
        m_Stats.FrameClusterSlots = m_FrameClusterSlots;
        m_Stats.FrameMaterialBatches = static_cast<uint32_t>(m_MaterialBatches.size());
        return m_FrameClusterSlots;
    }

} // namespace Renderer
} // namespace Core
