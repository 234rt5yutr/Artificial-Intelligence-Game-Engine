#include "Core/Renderer/VirtualGeometry/VirtualGeometryClusterBuilder.h"

#include "Core/Renderer/Mesh.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Core::Renderer {
namespace {

struct ClusterAccumulation {
    uint32_t MaterialIndex = 0;
    std::vector<uint32_t> PrimitiveIndices;
    std::unordered_set<uint32_t> UniqueVertices;
    uint32_t TriangleCount = 0;
};

class VirtualGeometryClusterBuilderServiceImpl {
public:
    Result<VirtualGeometryClusterBuildResult> Build(const VirtualGeometryClusterBuildRequest& request) {
        if (request.SourceMesh == nullptr) {
            return Result<VirtualGeometryClusterBuildResult>::Failure("VIRTUAL_GEOMETRY_INVALID_MESH");
        }
        if (request.TargetPageSizeBytes == 0U) {
            return Result<VirtualGeometryClusterBuildResult>::Failure("VIRTUAL_GEOMETRY_INVALID_TARGET_PAGE_SIZE");
        }

        const Mesh& mesh = *request.SourceMesh;
        VirtualGeometryClusterBuildResult result{};
        result.Metadata.TargetPageSizeBytes = request.TargetPageSizeBytes;
        result.Metadata.CompatibilityFallbackSupported = request.EnableCompatibilityFallback;
        result.Metadata.PrimitiveToCluster.assign(mesh.primitives.size(), 0U);

        if (!request.EnableClusterization || mesh.primitives.empty() || mesh.indices.empty()) {
            PopulateFallbackResult(mesh, request, result, "VIRTUAL_GEOMETRY_CLUSTERIZATION_DISABLED_OR_EMPTY");
            PersistResult(request.SourceMesh, result.Metadata);
            request.SourceMesh->SetVirtualGeometryAssociation(
                result.Metadata.MetadataKey,
                static_cast<uint32_t>(result.Metadata.Clusters.size()),
                static_cast<uint32_t>(result.Metadata.Pages.size()),
                result.Metadata.IsClusterized,
                result.CompatibilityFallbackUsed);
            request.SourceMesh->SetVirtualGeometryPagesResident(true);
            request.SourceMesh->SetVirtualGeometryFallbackActive(result.CompatibilityFallbackUsed);
            return Result<VirtualGeometryClusterBuildResult>::Success(std::move(result));
        }

        const uint32_t maxClusterTriangles = request.MaxClusterTriangles == 0U ? 128U : request.MaxClusterTriangles;
        const uint32_t maxClusterVertices = request.MaxClusterVertices == 0U ? 256U : request.MaxClusterVertices;

        std::vector<uint32_t> sortedPrimitiveIndices(mesh.primitives.size());
        for (uint32_t primitiveIndex = 0; primitiveIndex < static_cast<uint32_t>(mesh.primitives.size()); ++primitiveIndex) {
            sortedPrimitiveIndices[primitiveIndex] = primitiveIndex;
        }
        std::stable_sort(
            sortedPrimitiveIndices.begin(),
            sortedPrimitiveIndices.end(),
            [&mesh](const uint32_t lhs, const uint32_t rhs) {
                const Primitive& leftPrimitive = mesh.primitives[lhs];
                const Primitive& rightPrimitive = mesh.primitives[rhs];
                if (leftPrimitive.materialIndex != rightPrimitive.materialIndex) {
                    return leftPrimitive.materialIndex < rightPrimitive.materialIndex;
                }
                return lhs < rhs;
            });

        uint32_t partitionCount = 0;
        std::vector<VirtualGeometryClusterRecord> clusters;
        clusters.reserve(mesh.primitives.size());

        uint32_t cursor = 0;
        while (cursor < static_cast<uint32_t>(sortedPrimitiveIndices.size())) {
            const uint32_t partitionMaterial = mesh.primitives[sortedPrimitiveIndices[cursor]].materialIndex;
            ++partitionCount;
            ClusterAccumulation accumulation{};
            accumulation.MaterialIndex = partitionMaterial;

            while (cursor < static_cast<uint32_t>(sortedPrimitiveIndices.size()) &&
                   mesh.primitives[sortedPrimitiveIndices[cursor]].materialIndex == partitionMaterial) {
                const uint32_t primitiveIndex = sortedPrimitiveIndices[cursor];
                const Primitive& primitive = mesh.primitives[primitiveIndex];
                const uint32_t primitiveTriangles = primitive.indexCount / 3U;
                std::unordered_set<uint32_t> primitiveVertices = CollectPrimitiveUniqueVertices(mesh, primitive);
                const uint32_t mergedUniqueVertexCount = MergeVertexCount(accumulation.UniqueVertices, primitiveVertices);

                const bool exceedsTriangleBudget =
                    !accumulation.PrimitiveIndices.empty() &&
                    (accumulation.TriangleCount + primitiveTriangles > maxClusterTriangles);
                const bool exceedsVertexBudget =
                    !accumulation.PrimitiveIndices.empty() &&
                    (mergedUniqueVertexCount > maxClusterVertices);

                if (exceedsTriangleBudget || exceedsVertexBudget) {
                    clusters.push_back(FinalizeCluster(mesh, accumulation, static_cast<uint32_t>(clusters.size())));
                    accumulation = ClusterAccumulation{};
                    accumulation.MaterialIndex = partitionMaterial;
                }

                accumulation.PrimitiveIndices.push_back(primitiveIndex);
                accumulation.TriangleCount += primitiveTriangles;
                for (const uint32_t vertexIndex : primitiveVertices) {
                    accumulation.UniqueVertices.insert(vertexIndex);
                }
                ++cursor;
            }

            if (!accumulation.PrimitiveIndices.empty()) {
                clusters.push_back(FinalizeCluster(mesh, accumulation, static_cast<uint32_t>(clusters.size())));
            }
        }

        VirtualGeometryMetadata metadata{};
        metadata.TargetPageSizeBytes = request.TargetPageSizeBytes;
        metadata.Clusters = std::move(clusters);
        metadata.PrimitiveToCluster.assign(mesh.primitives.size(), 0U);
        metadata.CompatibilityFallbackSupported = request.EnableCompatibilityFallback;

        for (const VirtualGeometryClusterRecord& cluster : metadata.Clusters) {
            for (const uint32_t primitiveIndex : cluster.PrimitiveIndices) {
                if (primitiveIndex < metadata.PrimitiveToCluster.size()) {
                    metadata.PrimitiveToCluster[primitiveIndex] = cluster.ClusterId;
                }
            }
        }

        PackClustersIntoPages(metadata);
        metadata.IsClusterized = !metadata.Clusters.empty() && !metadata.Pages.empty();
        metadata.MetadataKey = ComputeMetadataKey(mesh, metadata);

        result.Metadata = std::move(metadata);
        result.PartitionCount = partitionCount;
        result.ClusterCount = static_cast<uint32_t>(result.Metadata.Clusters.size());
        result.PageCount = static_cast<uint32_t>(result.Metadata.Pages.size());

        if (!result.Metadata.IsClusterized && request.EnableCompatibilityFallback) {
            result.CompatibilityFallbackUsed = true;
            result.CompatibilityFallbackReason = "VIRTUAL_GEOMETRY_CLUSTERIZATION_EMPTY_RESULT";
        }

        PersistResult(request.SourceMesh, result.Metadata);
        request.SourceMesh->SetVirtualGeometryAssociation(
            result.Metadata.MetadataKey,
            result.ClusterCount,
            result.PageCount,
            result.Metadata.IsClusterized,
            result.CompatibilityFallbackUsed);
        request.SourceMesh->SetVirtualGeometryPagesResident(!result.Metadata.IsClusterized);
        request.SourceMesh->SetVirtualGeometryFallbackActive(result.CompatibilityFallbackUsed);

        return Result<VirtualGeometryClusterBuildResult>::Success(std::move(result));
    }

    const VirtualGeometryMetadata* TryGetMetadataForMesh(const Mesh* mesh) const {
        if (mesh == nullptr) {
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(m_Mutex);
        const auto found = m_MetadataByMesh.find(mesh);
        if (found == m_MetadataByMesh.end()) {
            return nullptr;
        }
        return &found->second;
    }

    const VirtualGeometryMetadata* TryGetMetadataByKey(const uint64_t metadataKey) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const auto found = m_MetadataByKey.find(metadataKey);
        if (found == m_MetadataByKey.end()) {
            return nullptr;
        }
        return &found->second;
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_MetadataByMesh.clear();
        m_MetadataByKey.clear();
    }

private:
    static std::unordered_set<uint32_t> CollectPrimitiveUniqueVertices(const Mesh& mesh, const Primitive& primitive) {
        std::unordered_set<uint32_t> vertices;
        const uint32_t indexEnd = primitive.firstIndex + primitive.indexCount;
        for (uint32_t indexCursor = primitive.firstIndex; indexCursor < indexEnd && indexCursor < mesh.indices.size(); ++indexCursor) {
            vertices.insert(mesh.indices[indexCursor]);
        }
        return vertices;
    }

    static uint32_t MergeVertexCount(const std::unordered_set<uint32_t>& current, const std::unordered_set<uint32_t>& incoming) {
        uint32_t mergedCount = static_cast<uint32_t>(current.size());
        for (const uint32_t value : incoming) {
            if (current.find(value) == current.end()) {
                ++mergedCount;
            }
        }
        return mergedCount;
    }

    static VirtualGeometryClusterRecord FinalizeCluster(
        const Mesh& mesh,
        const ClusterAccumulation& accumulation,
        const uint32_t clusterId) {
        VirtualGeometryClusterRecord cluster{};
        cluster.ClusterId = clusterId;
        cluster.MaterialIndex = accumulation.MaterialIndex;
        cluster.PrimitiveCount = static_cast<uint32_t>(accumulation.PrimitiveIndices.size());
        cluster.TriangleCount = accumulation.TriangleCount;
        cluster.UniqueVertexCount = static_cast<uint32_t>(accumulation.UniqueVertices.size());
        cluster.PrimitiveOffset = accumulation.PrimitiveIndices.empty() ? 0U : accumulation.PrimitiveIndices.front();
        cluster.PrimitiveIndices = accumulation.PrimitiveIndices;

        Math::Vec3 boundsMin(std::numeric_limits<float>::max());
        Math::Vec3 boundsMax(std::numeric_limits<float>::lowest());

        std::vector<uint32_t> sortedVertices(accumulation.UniqueVertices.begin(), accumulation.UniqueVertices.end());
        std::sort(sortedVertices.begin(), sortedVertices.end());
        for (const uint32_t vertexIndex : sortedVertices) {
            if (vertexIndex >= mesh.vertices.size()) {
                continue;
            }
            const Math::Vec3& position = mesh.vertices[vertexIndex].position;
            boundsMin = glm::min(boundsMin, position);
            boundsMax = glm::max(boundsMax, position);
        }

        if (sortedVertices.empty()) {
            boundsMin = Math::Vec3(0.0f);
            boundsMax = Math::Vec3(0.0f);
        }

        cluster.BoundsMin = boundsMin;
        cluster.BoundsMax = boundsMax;
        cluster.BoundsCenter = 0.5f * (boundsMin + boundsMax);

        float maxDistanceSquared = 0.0f;
        for (const uint32_t vertexIndex : sortedVertices) {
            if (vertexIndex >= mesh.vertices.size()) {
                continue;
            }
            const Math::Vec3 delta = mesh.vertices[vertexIndex].position - cluster.BoundsCenter;
            maxDistanceSquared = std::max(maxDistanceSquared, glm::dot(delta, delta));
        }
        cluster.BoundsRadius = std::sqrt(maxDistanceSquared);
        cluster.ErrorMetric = cluster.BoundsRadius / static_cast<float>(std::max(cluster.TriangleCount, 1U));
        return cluster;
    }

    static uint64_t EstimateClusterSizeBytes(const VirtualGeometryClusterRecord& cluster) {
        const uint64_t indexBytes = static_cast<uint64_t>(cluster.TriangleCount) * 3ULL * sizeof(uint32_t);
        const uint64_t vertexBytes = static_cast<uint64_t>(cluster.UniqueVertexCount) * sizeof(Vertex);
        constexpr uint64_t clusterMetadataBytes = 64ULL;
        return indexBytes + vertexBytes + clusterMetadataBytes;
    }

    static void PackClustersIntoPages(VirtualGeometryMetadata& metadata) {
        metadata.Pages.clear();
        if (metadata.Clusters.empty()) {
            return;
        }

        VirtualGeometryPageRecord currentPage{};
        currentPage.PageId = 0U;
        currentPage.ClusterOffset = 0U;
        currentPage.ClusterCount = 0U;
        currentPage.EstimatedSizeBytes = 0ULL;
        currentPage.BoundsMin = Math::Vec3(std::numeric_limits<float>::max());
        currentPage.BoundsMax = Math::Vec3(std::numeric_limits<float>::lowest());

        uint32_t currentPageIndex = 0U;
        for (uint32_t clusterIndex = 0U; clusterIndex < static_cast<uint32_t>(metadata.Clusters.size()); ++clusterIndex) {
            VirtualGeometryClusterRecord& cluster = metadata.Clusters[clusterIndex];
            const uint64_t clusterBytes = EstimateClusterSizeBytes(cluster);

            const bool wouldOverflowPage =
                currentPage.ClusterCount > 0U &&
                (currentPage.EstimatedSizeBytes + clusterBytes > static_cast<uint64_t>(metadata.TargetPageSizeBytes));

            if (wouldOverflowPage) {
                FinalizePageBounds(currentPage);
                metadata.Pages.push_back(currentPage);
                ++currentPageIndex;
                currentPage = VirtualGeometryPageRecord{};
                currentPage.PageId = currentPageIndex;
                currentPage.ClusterOffset = clusterIndex;
                currentPage.ClusterCount = 0U;
                currentPage.EstimatedSizeBytes = 0ULL;
                currentPage.BoundsMin = Math::Vec3(std::numeric_limits<float>::max());
                currentPage.BoundsMax = Math::Vec3(std::numeric_limits<float>::lowest());
            }

            cluster.PageId = currentPage.PageId;
            currentPage.ClusterCount += 1U;
            currentPage.EstimatedSizeBytes += clusterBytes;
            currentPage.BoundsMin = glm::min(currentPage.BoundsMin, cluster.BoundsMin);
            currentPage.BoundsMax = glm::max(currentPage.BoundsMax, cluster.BoundsMax);
        }

        if (currentPage.ClusterCount > 0U) {
            FinalizePageBounds(currentPage);
            metadata.Pages.push_back(currentPage);
        }
    }

    static void FinalizePageBounds(VirtualGeometryPageRecord& page) {
        if (page.ClusterCount == 0U) {
            page.BoundsMin = Math::Vec3(0.0f);
            page.BoundsMax = Math::Vec3(0.0f);
            page.BoundsCenter = Math::Vec3(0.0f);
            page.BoundsRadius = 0.0f;
            return;
        }
        page.BoundsCenter = 0.5f * (page.BoundsMin + page.BoundsMax);
        const Math::Vec3 extent = page.BoundsMax - page.BoundsCenter;
        page.BoundsRadius = glm::length(extent);
    }

    static uint64_t HashCombineU64(uint64_t seed, const uint64_t value) {
        constexpr uint64_t kPrime = 1099511628211ULL;
        seed ^= value;
        seed *= kPrime;
        return seed;
    }

    static uint64_t QuantizeFloat(const float value) {
        const float scaled = value * 1000.0f;
        const int64_t rounded = static_cast<int64_t>(std::llround(static_cast<double>(scaled)));
        return static_cast<uint64_t>(rounded);
    }

    static uint64_t ComputeMetadataKey(const Mesh& mesh, const VirtualGeometryMetadata& metadata) {
        uint64_t hash = 1469598103934665603ULL;
        hash = HashCombineU64(hash, static_cast<uint64_t>(mesh.primitives.size()));
        hash = HashCombineU64(hash, static_cast<uint64_t>(mesh.indices.size()));
        hash = HashCombineU64(hash, static_cast<uint64_t>(metadata.Clusters.size()));
        hash = HashCombineU64(hash, static_cast<uint64_t>(metadata.Pages.size()));
        for (const VirtualGeometryClusterRecord& cluster : metadata.Clusters) {
            hash = HashCombineU64(hash, static_cast<uint64_t>(cluster.MaterialIndex));
            hash = HashCombineU64(hash, static_cast<uint64_t>(cluster.PrimitiveOffset));
            hash = HashCombineU64(hash, static_cast<uint64_t>(cluster.PrimitiveCount));
            hash = HashCombineU64(hash, static_cast<uint64_t>(cluster.TriangleCount));
            hash = HashCombineU64(hash, static_cast<uint64_t>(cluster.UniqueVertexCount));
            hash = HashCombineU64(hash, static_cast<uint64_t>(cluster.PageId));
            hash = HashCombineU64(hash, QuantizeFloat(cluster.BoundsCenter.x));
            hash = HashCombineU64(hash, QuantizeFloat(cluster.BoundsCenter.y));
            hash = HashCombineU64(hash, QuantizeFloat(cluster.BoundsCenter.z));
            hash = HashCombineU64(hash, QuantizeFloat(cluster.ErrorMetric));
        }
        return hash;
    }

    static uint64_t ComputeFallbackMetadataKey(const Mesh& mesh) {
        uint64_t hash = 1469598103934665603ULL;
        hash = HashCombineU64(hash, static_cast<uint64_t>(mesh.primitives.size()));
        hash = HashCombineU64(hash, static_cast<uint64_t>(mesh.indices.size()));
        hash = HashCombineU64(hash, static_cast<uint64_t>(mesh.vertices.size()));
        for (const Primitive& primitive : mesh.primitives) {
            hash = HashCombineU64(hash, static_cast<uint64_t>(primitive.firstIndex));
            hash = HashCombineU64(hash, static_cast<uint64_t>(primitive.indexCount));
            hash = HashCombineU64(hash, static_cast<uint64_t>(primitive.materialIndex));
        }
        return hash;
    }

    static void PopulateFallbackResult(
        const Mesh& mesh,
        const VirtualGeometryClusterBuildRequest& request,
        VirtualGeometryClusterBuildResult& result,
        const std::string& fallbackReason) {
        result.Metadata.MetadataKey = ComputeFallbackMetadataKey(mesh);
        result.Metadata.IsClusterized = false;
        result.Metadata.TargetPageSizeBytes = request.TargetPageSizeBytes;
        result.Metadata.CompatibilityFallbackSupported = request.EnableCompatibilityFallback;
        result.PartitionCount = 0U;
        result.ClusterCount = 0U;
        result.PageCount = 0U;
        result.CompatibilityFallbackUsed = request.EnableCompatibilityFallback;
        result.CompatibilityFallbackReason = fallbackReason;
    }

    void PersistResult(const Mesh* mesh, const VirtualGeometryMetadata& metadata) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_MetadataByMesh[mesh] = metadata;
        m_MetadataByKey[metadata.MetadataKey] = metadata;
    }

    mutable std::mutex m_Mutex;
    std::unordered_map<const Mesh*, VirtualGeometryMetadata> m_MetadataByMesh;
    std::unordered_map<uint64_t, VirtualGeometryMetadata> m_MetadataByKey;
};

VirtualGeometryClusterBuilderServiceImpl& GetImpl() {
    static VirtualGeometryClusterBuilderServiceImpl impl;
    return impl;
}

} // namespace

Result<VirtualGeometryClusterBuildResult> VirtualGeometryClusterBuilderService::Build(
    const VirtualGeometryClusterBuildRequest& request) {
    return GetImpl().Build(request);
}

const VirtualGeometryMetadata* VirtualGeometryClusterBuilderService::TryGetMetadataForMesh(const Mesh* mesh) const {
    return GetImpl().TryGetMetadataForMesh(mesh);
}

const VirtualGeometryMetadata* VirtualGeometryClusterBuilderService::TryGetMetadataByKey(const uint64_t metadataKey) const {
    return GetImpl().TryGetMetadataByKey(metadataKey);
}

void VirtualGeometryClusterBuilderService::Clear() {
    GetImpl().Clear();
}

VirtualGeometryClusterBuilderService& GetVirtualGeometryClusterBuilderService() {
    static VirtualGeometryClusterBuilderService service;
    return service;
}

Result<VirtualGeometryClusterBuildResult> BuildVirtualizedGeometryClusters(
    const VirtualGeometryClusterBuildRequest& request) {
    return GetVirtualGeometryClusterBuilderService().Build(request);
}

} // namespace Core::Renderer

