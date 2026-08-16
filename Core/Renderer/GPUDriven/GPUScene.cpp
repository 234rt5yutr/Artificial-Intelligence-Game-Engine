#include "GPUScene.h"

#include "Core/ECS/Systems/RenderSystem.h"
#include "Core/Log.h"

#include <meshoptimizer.h>
#include "Core/Renderer/Material/MaterialGraph.h"
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
        // The skinning region sits at the top of the arena so static residency
        // grows from the bottom and never collides with it.
        const uint32_t skinnedRegion = std::min(m_Limits.MaxSkinnedVertices, m_Limits.MaxVertices / 2);
        m_SkinnedRegionStart = m_Limits.MaxVertices - skinnedRegion;
        m_Stats.SkinnedVerticesCapacity = skinnedRegion;

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
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, m_InstanceOffsetBuffer) &&
            RHI::CreateGpuBuffer(allocator,
                                 static_cast<VkDeviceSize>(m_Limits.MaxSkinnedSourceVertices) *
                                     sizeof(SkinnedVertex),
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, m_SkinnedSourceBuffer);

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
        RHI::DestroyGpuBuffer(allocator, m_SkinnedSourceBuffer);

        m_Residency.clear();
        m_Rejected.clear();
        m_FrameInstances.clear();
        m_FrameInstanceOffsets.clear();
        m_MaterialBatches.clear();
        m_VertexCursor = 0;
        m_IndexCursor = 0;
        m_ClusterCursor = 0;
        m_SkinnedSourceCursor = 0;
        m_SkinnedVertexCursor = 0;
        m_PendingSkins.clear();
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
        if (mesh->vertices.empty() || mesh->indices.size() < 3) {
            return false;
        }

        // A skinned mesh also uploads its source vertices once. Its bind-pose
        // copy still goes into the arena below, because that is what the
        // clusteriser measures and what an instance without a pose falls back to.
        const bool skinned = mesh->IsSkeletal() && !mesh->skinnedVertices.empty();
        if (skinned) {
            const uint32_t sourceCount = static_cast<uint32_t>(mesh->skinnedVertices.size());
            if (sourceCount != mesh->vertices.size()) {
                ENGINE_CORE_WARN("GPUScene: skeletal mesh has {} skinned vertices but {} static "
                                 "ones; skinning needs them to correspond one to one",
                                 sourceCount, mesh->vertices.size());
                return false;
            }
            if (m_SkinnedSourceCursor + sourceCount > m_Limits.MaxSkinnedSourceVertices) {
                ENGINE_CORE_WARN("GPUScene: skinned source buffer is full");
                return false;
            }
            auto* sourceArena = static_cast<SkinnedVertex*>(m_SkinnedSourceBuffer.Mapped);
            if (!sourceArena) {
                return false;
            }
            std::memcpy(sourceArena + m_SkinnedSourceCursor, mesh->skinnedVertices.data(),
                        sourceCount * sizeof(SkinnedVertex));
            record.SkinnedSourceOffset = m_SkinnedSourceCursor;
            record.BoneCount = mesh->GetSkeleton().GetBoneCount();
            record.Skinned = true;
            m_SkinnedSourceCursor += sourceCount;
        }

        // Each primitive is clusterised on its own so it can carry its own
        // material. Meshes with no primitive list are treated as one.
        struct SectionBuild {
            uint32_t MaterialSlot = 0;
            uint32_t FirstIndex = 0;   // into the mesh index array
            uint32_t IndexCount = 0;
        };
        std::vector<SectionBuild> sectionBuilds;
        for (const auto& primitive : mesh->primitives) {
            if (primitive.indexCount >= 3 &&
                primitive.firstIndex + primitive.indexCount <= mesh->indices.size()) {
                sectionBuilds.push_back({primitive.materialIndex, primitive.firstIndex,
                                         primitive.indexCount});
            }
        }
        if (sectionBuilds.empty()) {
            sectionBuilds.push_back({0u, 0u, static_cast<uint32_t>(mesh->indices.size())});
        }

        std::vector<uint32_t> mergedIndices;
        std::vector<GpuCluster> mergedClusters;
        std::vector<GpuMeshSection> sections;
        Math::Vec4 meshBounds{0.0f};
        mergedIndices.reserve(mesh->indices.size());
        for (const auto& build : sectionBuilds) {
            const std::vector<uint32_t> sectionIndices(
                mesh->indices.begin() + build.FirstIndex,
                mesh->indices.begin() + build.FirstIndex + build.IndexCount);
            const MeshClusterSet clusterSet = BuildMeshClusters(
                mesh->vertices.data(), sizeof(Vertex), mesh->vertices.size(),
                sectionIndices, m_Limits.MaxClusterTriangles);
            if (clusterSet.Clusters.empty()) {
                continue;
            }

            GpuMeshSection section;
            section.MaterialSlot = build.MaterialSlot;

            Math::Vec3 sectionMin(std::numeric_limits<float>::max());
            Math::Vec3 sectionMax(std::numeric_limits<float>::lowest());

            // Level 0 is the geometry as authored; each further level is a
            // simplified index buffer over the *same* vertices, so switching
            // level costs nothing but a different cluster range - no second
            // vertex copy, no re-upload.
            auto appendLevel = [&](const MeshClusterSet& set, uint32_t level) {
                GpuMeshSection::Lod lod;
                lod.ClusterBase = static_cast<uint32_t>(mergedClusters.size());
                lod.ClusterCount = static_cast<uint32_t>(set.Clusters.size());
                lod.TriangleCount = static_cast<uint32_t>(set.ReorderedIndices.size() / 3);

                const uint32_t indexBase = static_cast<uint32_t>(mergedIndices.size());
                for (GpuCluster cluster : set.Clusters) {
                    if (level == 0) {
                        // Bounds come from level 0 only. A simplified level sits
                        // inside the original, and letting it shrink the bounds
                        // would make culling depend on which level was picked.
                        const Math::Vec3 centre(cluster.CenterRadius);
                        const float radius = cluster.CenterRadius.w;
                        sectionMin = glm::min(sectionMin, centre - radius);
                        sectionMax = glm::max(sectionMax, centre + radius);
                    }
                    cluster.FirstIndex += indexBase;
                    mergedClusters.push_back(cluster);
                }
                mergedIndices.insert(mergedIndices.end(), set.ReorderedIndices.begin(),
                                     set.ReorderedIndices.end());
                section.Lods[level] = lod;
                section.LodCount = level + 1;
            };

            appendLevel(clusterSet, 0);

            // Half then a quarter of the triangles. meshopt_simplify preserves
            // the vertex buffer and only rewrites indices, which is exactly what
            // a shared arena wants.
            std::vector<uint32_t> previousIndices = sectionIndices;
            for (uint32_t level = 1; level < kMaxSectionLods; ++level) {
                const std::size_t targetIndices = (previousIndices.size() / 2 / 3) * 3;
                // Below this a level is not worth an arena slot: the simplifier
                // has nothing left to remove that the cluster bounds do not
                // already approximate.
                if (targetIndices < 96) {
                    break;
                }

                std::vector<uint32_t> simplified(previousIndices.size());
                float resultError = 0.0f;
                const std::size_t count = meshopt_simplify(
                    simplified.data(), previousIndices.data(), previousIndices.size(),
                    &mesh->vertices[0].position.x, mesh->vertices.size(), sizeof(Vertex),
                    targetIndices, 0.05f, 0, &resultError);
                // A simplifier that cannot hit the target returns roughly what it
                // was given; another level of the same geometry is pure waste.
                if (count < 3 || count > previousIndices.size() * 3 / 4) {
                    break;
                }
                simplified.resize(count);

                const MeshClusterSet simplifiedSet = BuildMeshClusters(
                    mesh->vertices.data(), sizeof(Vertex), mesh->vertices.size(),
                    simplified, m_Limits.MaxClusterTriangles);
                if (simplifiedSet.Clusters.empty()) {
                    break;
                }
                appendLevel(simplifiedSet, level);
                previousIndices = std::move(simplified);
            }

            section.ClusterBase = section.Lods[0].ClusterBase;
            section.ClusterCount = section.Lods[0].ClusterCount;
            const Math::Vec3 sectionCentre = (sectionMin + sectionMax) * 0.5f;
            section.BoundsCenterRadius =
                Math::Vec4(sectionCentre, glm::length(sectionMax - sectionCentre));
            sections.push_back(section);
            meshBounds = clusterSet.BoundsCenterRadius;
        }

        if (mergedClusters.empty()) {
            ENGINE_CORE_WARN("GPUScene: clusterisation produced no clusters; mesh stays on the direct path");
            return false;
        }

        MeshClusterSet clusters;
        clusters.Clusters = std::move(mergedClusters);
        clusters.ReorderedIndices = std::move(mergedIndices);
        clusters.BoundsCenterRadius = meshBounds;

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
        for (auto& section : sections) {
            section.ClusterBase += m_ClusterCursor;
            for (uint32_t level = 0; level < section.LodCount; ++level) {
                section.Lods[level].ClusterBase += m_ClusterCursor;
            }
        }
        record.Sections = std::move(sections);
        if (record.Sections.size() > 1) {
            ENGINE_CORE_INFO("GPUScene: mesh resident as {} sections, one per material",
                             record.Sections.size());
        }

        m_VertexCursor += vertexCount;
        m_IndexCursor += indexCount;
        m_ClusterCursor += clusterCount;
        return true;
    }

    void GPUScene::ClearSkinnedInstances(uint32_t targetVertexOffset) {
        if (targetVertexOffset == 0) {
            return;
        }
        auto* arena = static_cast<GpuInstance*>(m_InstanceBuffer.Mapped);
        for (std::size_t i = 0; i < m_FrameInstances.size(); ++i) {
            if (m_FrameInstances[i].VertexOffset != targetVertexOffset) {
                continue;
            }
            m_FrameInstances[i].VertexOffset = 0;
            // The instance buffer was already uploaded, so patch it in place
            // rather than re-uploading the whole list.
            if (arena) {
                arena[i].VertexOffset = 0;
            }
        }
    }

    uint32_t GPUScene::BeginFrame(const ECS::DrawCommand* commands, std::size_t commandCount,
                                  const Math::Vec3& cameraPosition, float lodScale) {
        m_FrameInstances.clear();
        m_FrameInstanceOffsets.clear();
        m_MaterialBatches.clear();
        m_PendingSkins.clear();
        m_SkinnedVertexCursor = 0;
        m_FrameClusterSlots = 0;
        m_Stats.LodScale = lodScale;
        m_Stats.MinProjectedPixels = 0.0f;
        m_Stats.MaxProjectedPixels = 0.0f;

        if (!m_Context || !commands || commandCount == 0) {
            return 0;
        }

        // Sort by material so each material becomes one contiguous cluster-slot
        // range and therefore one indirect draw.
        struct PendingInstance {
            const GpuMeshSection* Section;
            const ECS::DrawCommand* Command;
            uint32_t MaterialIndex;
            uint32_t SkinnedVertexOffset;   // 0 for static geometry
            bool Transparent = false;
            float ViewDistance = 0.0f;      // only meaningful when transparent
            uint32_t Lod = 0;
        };
        std::vector<PendingInstance> pending;
        pending.reserve(commandCount);

        bool instanceLimitHit = false;
        for (std::size_t i = 0; i < commandCount && !instanceLimitHit; ++i) {
            const ECS::DrawCommand& command = commands[i];
            const GpuMeshRecord* record = EnsureResident(command.Mesh);
            if (!record) {
                continue;
            }

            // A skinned mesh takes one slice for the whole mesh, not one per
            // section: every section reads the same posed vertices.
            uint32_t skinnedOffset = 0;
            if (record->Skinned) {
                if (m_SkinnedVertexCursor + record->VertexCount <= m_Stats.SkinnedVerticesCapacity) {
                    skinnedOffset = m_SkinnedRegionStart + m_SkinnedVertexCursor;
                    PendingSkin skin;
                    skin.SourceVertexOffset = record->SkinnedSourceOffset;
                    skin.TargetVertexOffset = skinnedOffset;
                    skin.VertexCount = record->VertexCount;
                    skin.BoneCount = record->BoneCount;
                    skin.BoneOffset = command.BoneOffset;
                    skin.SourceMesh = command.Mesh;
                    m_PendingSkins.push_back(skin);
                    m_SkinnedVertexCursor += record->VertexCount;
                } else {
                    // Out of skinning space: the instance still draws, in its
                    // bind pose, rather than disappearing.
                    ENGINE_CORE_WARN("GPUScene: skinning region full; an instance renders in bind pose");
                }
            }

            for (const auto& section : record->Sections) {
                if (pending.size() >= m_Limits.MaxInstances) {
                    ENGINE_CORE_WARN("GPUScene instance limit ({}) reached; remaining draws are dropped this frame",
                                     m_Limits.MaxInstances);
                    instanceLimitHit = true;
                    break;
                }
                const uint32_t materialIndex = command.MaterialIndex + section.MaterialSlot;
                const MaterialInstance* material =
                    MaterialLibrary::Get().GetMaterial(materialIndex);
                const bool transparent =
                    material && material->AlphaMode == MaterialAlphaMode::Blend;

                // Bounds are section-local, so the sort key is the world-space
                // distance to the section's own centre, not the object origin.
                const Math::Vec3 worldCentre =
                    Math::Vec3(command.Transform *
                               Math::Vec4(Math::Vec3(section.BoundsCenterRadius), 1.0f));
                // Scale enters through the transform, so the bounds radius has
                // to be scaled with it or a giant object would pick the same
                // level as a small one.
                const float scale = std::sqrt(std::max({
                    glm::length2(Math::Vec3(command.Transform[0])),
                    glm::length2(Math::Vec3(command.Transform[1])),
                    glm::length2(Math::Vec3(command.Transform[2]))}));
                const float distance = glm::length(worldCentre - cameraPosition);
                const float radius = section.BoundsCenterRadius.w * scale;

                const float projectedPixels = radius * lodScale / std::max(distance, 1e-3f);
                m_Stats.MinProjectedPixels = m_FrameInstances.empty() && pending.empty()
                    ? projectedPixels
                    : std::min(m_Stats.MinProjectedPixels, projectedPixels);
                m_Stats.MaxProjectedPixels = std::max(m_Stats.MaxProjectedPixels, projectedPixels);

                uint32_t lod = 0;
                if (m_Lod.Enabled && lodScale > 0.0f && section.LodCount > 1) {
                    // Projected radius in pixels. Everything past the near plane
                    // shrinks with distance, so one divide picks the level.
                    for (uint32_t level = 0; level + 1 < section.LodCount; ++level) {
                        if (projectedPixels < m_Lod.Thresholds[level]) {
                            lod = level + 1;
                        }
                    }
                    lod = std::min(lod, section.LodCount - 1);
                }

                pending.push_back({&section, &command, materialIndex, skinnedOffset, transparent,
                                   distance, lod});
            }
        }

        // Opaque first, grouped by material so each becomes one indirect draw.
        // Blended instances follow, back to front, because they composite in
        // draw order and batching them by material would put the near pane
        // under the far one.
        std::stable_sort(pending.begin(), pending.end(),
                         [](const PendingInstance& lhs, const PendingInstance& rhs) {
                             if (lhs.Transparent != rhs.Transparent) {
                                 return !lhs.Transparent;
                             }
                             if (lhs.Transparent) {
                                 return lhs.ViewDistance > rhs.ViewDistance;
                             }
                             return lhs.MaterialIndex < rhs.MaterialIndex;
                         });

        m_FrameInstances.reserve(pending.size());
        m_FrameInstanceOffsets.reserve(pending.size() + 1);

        uint32_t slotCursor = 0;
        uint32_t frameTriangles = 0;
        uint32_t frameTrianglesAtLod0 = 0;
        for (auto& count : m_Stats.LodInstances) {
            count = 0;
        }
        for (const auto& item : pending) {
            const GpuMeshSection::Lod& lod = item.Section->Lods[item.Lod];
            if (slotCursor + lod.ClusterCount > m_Limits.MaxClusterSlots) {
                ENGINE_CORE_WARN("GPUScene cluster-slot limit ({}) reached; remaining draws are dropped this frame",
                                 m_Limits.MaxClusterSlots);
                break;
            }

            GpuInstance instance;
            instance.Transform = item.Command->Transform;
            instance.PreviousTransform = item.Command->PreviousTransform;
            instance.VertexOffset = item.SkinnedVertexOffset;
            instance.BoundsCenterRadius = item.Section->BoundsCenterRadius;
            instance.ClusterBase = lod.ClusterBase;
            instance.ClusterCount = lod.ClusterCount;
            ++m_Stats.LodInstances[item.Lod];
            frameTriangles += lod.TriangleCount;
            frameTrianglesAtLod0 += item.Section->Lods[0].TriangleCount;
            instance.MaterialIndex = item.MaterialIndex;
            // A blended surface casting an opaque shadow is worse than it
            // casting none: the shadow pass has no alpha, so a window would
            // throw the silhouette of a wall.
            instance.Flags = (item.Command->CastShadows && !item.Transparent) ? 1u : 0u;
            if (item.Transparent) {
                instance.Flags |= 2u;
            }

            // A blended batch never merges with the one before it: merging would
            // reorder the draws that its back-to-front sort just established.
            if (m_MaterialBatches.empty() || item.Transparent ||
                m_MaterialBatches.back().Transparent ||
                m_MaterialBatches.back().MaterialIndex != instance.MaterialIndex) {
                m_MaterialBatches.push_back({instance.MaterialIndex, slotCursor, 0,
                                             item.Transparent});
            }
            m_MaterialBatches.back().ClusterSlotCount += lod.ClusterCount;

            m_FrameInstanceOffsets.push_back(slotCursor);
            m_FrameInstances.push_back(instance);
            slotCursor += lod.ClusterCount;
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
        m_Stats.SkinnedInstances = static_cast<uint32_t>(m_PendingSkins.size());
        m_Stats.SkinnedVerticesUsed = m_SkinnedVertexCursor;
        m_Stats.FrameTriangles = frameTriangles;
        m_Stats.FrameTrianglesAtLod0 = frameTrianglesAtLod0;
        m_Stats.TransparentInstances = 0;
        m_Stats.TransparentBatches = 0;
        for (const auto& instance : m_FrameInstances) {
            if ((instance.Flags & 2u) != 0u) {
                ++m_Stats.TransparentInstances;
            }
        }
        for (const auto& batch : m_MaterialBatches) {
            if (batch.Transparent) {
                ++m_Stats.TransparentBatches;
            }
        }
        return m_FrameClusterSlots;
    }

} // namespace Renderer
} // namespace Core
