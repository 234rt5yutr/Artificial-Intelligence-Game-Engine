#pragma once

#include "Core/Asset/Addressables/AddressablesCatalogTypes.h"
#include "Core/Math/Math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Core::Renderer {

class Mesh;

template <typename T>
using Result = Core::Asset::Addressables::Result<T>;

enum class VirtualGeometryResidencyEventType : uint8_t {
    Requested = 0,
    Loaded = 1,
    Promoted = 2,
    Evicted = 3,
    Failed = 4
};

struct VirtualGeometryResidencyEvent {
    VirtualGeometryResidencyEventType Type = VirtualGeometryResidencyEventType::Requested;
    uint32_t PageId = 0;
    uint64_t FrameIndex = 0;
    float PriorityScore = 0.0f;
    std::string Reason;
};

struct VirtualGeometryClusterRecord {
    uint32_t ClusterId = 0;
    uint32_t MaterialIndex = 0;
    uint32_t PrimitiveOffset = 0;
    uint32_t PrimitiveCount = 0;
    std::vector<uint32_t> PrimitiveIndices;
    uint32_t TriangleCount = 0;
    uint32_t UniqueVertexCount = 0;
    uint32_t PageId = 0;
    Math::Vec3 BoundsMin = Math::Vec3(0.0f);
    Math::Vec3 BoundsMax = Math::Vec3(0.0f);
    Math::Vec3 BoundsCenter = Math::Vec3(0.0f);
    float BoundsRadius = 0.0f;
    float ErrorMetric = 0.0f;
};

struct VirtualGeometryPageRecord {
    uint32_t PageId = 0;
    uint32_t ClusterOffset = 0;
    uint32_t ClusterCount = 0;
    uint64_t EstimatedSizeBytes = 0;
    Math::Vec3 BoundsMin = Math::Vec3(0.0f);
    Math::Vec3 BoundsMax = Math::Vec3(0.0f);
    Math::Vec3 BoundsCenter = Math::Vec3(0.0f);
    float BoundsRadius = 0.0f;
};

struct VirtualGeometryMetadata {
    uint64_t MetadataKey = 0;
    bool IsClusterized = false;
    bool CompatibilityFallbackSupported = true;
    uint32_t TargetPageSizeBytes = 0;
    std::vector<VirtualGeometryClusterRecord> Clusters;
    std::vector<VirtualGeometryPageRecord> Pages;
    std::vector<uint32_t> PrimitiveToCluster;
};

struct VirtualGeometryClusterBuildRequest {
    Mesh* SourceMesh = nullptr;
    uint32_t MaxClusterTriangles = 128;
    uint32_t MaxClusterVertices = 256;
    uint32_t TargetPageSizeBytes = 64U * 1024U;
    bool EnableClusterization = true;
    bool EnableCompatibilityFallback = true;
};

struct VirtualGeometryClusterBuildResult {
    VirtualGeometryMetadata Metadata;
    uint32_t PartitionCount = 0;
    uint32_t ClusterCount = 0;
    uint32_t PageCount = 0;
    bool CompatibilityFallbackUsed = false;
    std::string CompatibilityFallbackReason;
};

struct VirtualGeometryStreamRequest {
    Mesh* SourceMesh = nullptr;
    const VirtualGeometryMetadata* Metadata = nullptr;
    Math::Vec3 CameraPosition = Math::Vec3(0.0f);
    Math::Vec3 CameraVelocity = Math::Vec3(0.0f);
    std::vector<uint32_t> VisibleClusterIds;
    uint32_t MaxPagesToStreamIn = 8;
    uint32_t MaxPagesToEvict = 4;
    uint64_t MemoryBudgetBytes = 64ULL * 1024ULL * 1024ULL;
    uint64_t StreamInBudgetBytes = 8ULL * 1024ULL * 1024ULL;
    uint64_t FrameIndex = 0;
    std::vector<uint32_t> ForceFailPageIds;
};

struct VirtualGeometryStreamResult {
    std::vector<uint32_t> RequestedPages;
    std::vector<uint32_t> LoadedPages;
    std::vector<uint32_t> PromotedPages;
    std::vector<uint32_t> EvictedPages;
    std::vector<uint32_t> FailedPages;
    std::vector<uint32_t> FallbackPages;
    std::vector<uint32_t> ResidentPages;
    std::vector<VirtualGeometryResidencyEvent> Events;
    uint64_t ResidentBytes = 0;
    float QueuePressure = 0.0f;
    float BudgetSaturation = 0.0f;
    bool BudgetSaturated = false;
    bool RequiresFallbackRendering = false;
};

struct VirtualGeometryStreamingDiagnostics {
    uint32_t PendingRequestCount = 0;
    uint32_t ResidentPageCount = 0;
    uint64_t ResidentBytes = 0;
    uint64_t MemoryBudgetBytes = 0;
    float QueuePressure = 0.0f;
    float BudgetSaturation = 0.0f;
    bool BudgetSaturated = false;
    uint32_t LastRequestedCount = 0;
    uint32_t LastLoadedCount = 0;
    uint32_t LastEvictedCount = 0;
    uint32_t LastFailedCount = 0;
};

} // namespace Core::Renderer

