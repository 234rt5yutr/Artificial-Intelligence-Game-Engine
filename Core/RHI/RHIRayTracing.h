// Core/RHI/RHIRayTracing.h

#pragma once
#include "RHIResources.h"
#include <vector>
#include <memory>
#include <glm/glm.hpp>

namespace AIEngine::RHI {

// Forward declarations
class RHIDevice;
class RHICommandBuffer;
class RHIBuffer;
class RHIPipeline;

/// Ray tracing capability flags
enum class RTCapabilityFlags : uint32_t {
    None = 0,
    RayTracingPipeline = 1 << 0,
    RayQuery = 1 << 1,
    AccelerationStructure = 1 << 2,
    IndirectBuild = 1 << 3,
    HostCommands = 1 << 4,
    PrimitiveCulling = 1 << 5
};

/// Ray tracing pipeline stage
enum class RTShaderStage {
    RayGeneration,
    ClosestHit,
    AnyHit,
    Miss,
    Intersection,
    Callable
};

/// Acceleration structure type
enum class AccelerationStructureType {
    BottomLevel,    ///< BLAS - geometry data
    TopLevel        ///< TLAS - instance data
};

/// Acceleration structure build flags
enum class ASBuildFlags : uint32_t {
    None = 0,
    AllowUpdate = 1 << 0,
    AllowCompaction = 1 << 1,
    PreferFastTrace = 1 << 2,
    PreferFastBuild = 1 << 3,
    LowMemory = 1 << 4
};

/// Geometry type for BLAS
enum class GeometryType {
    Triangles,
    AABBs,
    Instances
};

/// Geometry flags
enum class GeometryFlags : uint32_t {
    None = 0,
    Opaque = 1 << 0,
    NoDuplicateAnyHit = 1 << 1
};

/// Vertex format for ray tracing
enum class VertexFormat {
    Float2,
    Float3,
    Float4
};

/// Index format for ray tracing
enum class IndexFormat {
    UInt16,
    UInt32
};

/// Ray tracing capabilities query result
struct RTCapabilities {
    RTCapabilityFlags flags = RTCapabilityFlags::None;
    uint32_t maxRayRecursionDepth = 0;
    uint32_t maxRayDispatchInvocations = 0;
    uint32_t maxGeometryCount = 0;
    uint32_t maxInstanceCount = 0;
    uint32_t maxPrimitiveCount = 0;
    uint32_t shaderGroupHandleSize = 0;
    uint32_t shaderGroupBaseAlignment = 0;
    uint32_t shaderGroupHandleAlignment = 0;
    uint64_t maxScratchDataSize = 0;
};

/// Triangle geometry description for BLAS
struct TriangleGeometryDesc {
    RHIBuffer* vertexBuffer = nullptr;
    uint64_t vertexOffset = 0;
    uint32_t vertexStride = 0;
    uint32_t vertexCount = 0;
    VertexFormat vertexFormat = VertexFormat::Float3;
    
    RHIBuffer* indexBuffer = nullptr;
    uint64_t indexOffset = 0;
    uint32_t indexCount = 0;
    IndexFormat indexFormat = IndexFormat::UInt32;
    
    RHIBuffer* transformBuffer = nullptr;
    uint64_t transformOffset = 0;
    
    GeometryFlags flags = GeometryFlags::Opaque;
};

/// AABB geometry description for BLAS
struct AABBGeometryDesc {
    RHIBuffer* aabbBuffer = nullptr;
    uint64_t offset = 0;
    uint32_t stride = 0;
    uint32_t count = 0;
    
    GeometryFlags flags = GeometryFlags::None;
};

/// Instance description for TLAS
struct InstanceDesc {
    glm::mat3x4 transform;
    uint32_t instanceId : 24;
    uint32_t mask : 8;
    uint32_t hitGroupOffset : 24;
    uint32_t flags : 8;
    uint64_t blasAddress;
};

/// Acceleration structure build info
struct ASBuildInfo {
    AccelerationStructureType type = AccelerationStructureType::BottomLevel;
    ASBuildFlags flags = ASBuildFlags::PreferFastTrace;
    
    std::vector<TriangleGeometryDesc> triangleGeometries;
    std::vector<AABBGeometryDesc> aabbGeometries;
    
    RHIBuffer* instanceBuffer = nullptr;
    uint64_t instanceOffset = 0;
    uint32_t instanceCount = 0;
    
    bool isUpdate = false;
    class AccelerationStructure* sourceAS = nullptr;
};

/// Acceleration structure size info
struct ASSizeInfo {
    uint64_t accelerationStructureSize = 0;
    uint64_t buildScratchSize = 0;
    uint64_t updateScratchSize = 0;
};

/// Acceleration structure handle
class AccelerationStructure {
public:
    virtual ~AccelerationStructure() = default;
    
    virtual AccelerationStructureType GetType() const = 0;
    virtual uint64_t GetDeviceAddress() const = 0;
    virtual uint64_t GetSize() const = 0;
    virtual bool IsCompacted() const = 0;
};

/// Shader binding table description
struct SBTDesc {
    struct ShaderRecord {
        const void* shaderGroupHandle = nullptr;
        const void* inlineData = nullptr;
        uint32_t inlineDataSize = 0;
    };
    
    std::vector<ShaderRecord> rayGenRecords;
    std::vector<ShaderRecord> missRecords;
    std::vector<ShaderRecord> hitGroupRecords;
    std::vector<ShaderRecord> callableRecords;
};

/// Shader binding table handle
class ShaderBindingTable {
public:
    virtual ~ShaderBindingTable() = default;
    
    virtual uint64_t GetRayGenAddress() const = 0;
    virtual uint64_t GetMissAddress() const = 0;
    virtual uint64_t GetHitGroupAddress() const = 0;
    virtual uint64_t GetCallableAddress() const = 0;
    
    virtual uint64_t GetRayGenStride() const = 0;
    virtual uint64_t GetMissStride() const = 0;
    virtual uint64_t GetHitGroupStride() const = 0;
    virtual uint64_t GetCallableStride() const = 0;
    
    virtual uint64_t GetRayGenSize() const = 0;
    virtual uint64_t GetMissSize() const = 0;
    virtual uint64_t GetHitGroupSize() const = 0;
    virtual uint64_t GetCallableSize() const = 0;
};

// Forward declare pipeline layout
class RHIPipelineLayout;

/// Ray tracing pipeline description
struct RTPipelineDesc {
    struct ShaderStageDesc {
        RTShaderStage stage;
        const void* shaderCode = nullptr;
        size_t codeSize = 0;
        const char* entryPoint = "main";
    };
    
    struct HitGroupDesc {
        const char* name = nullptr;
        int closestHitIndex = -1;
        int anyHitIndex = -1;
        int intersectionIndex = -1;
    };
    
    std::vector<ShaderStageDesc> stages;
    std::vector<HitGroupDesc> hitGroups;
    std::vector<int> missShaderIndices;
    int rayGenIndex = 0;
    
    uint32_t maxRayRecursionDepth = 1;
    uint32_t maxPayloadSize = 32;
    uint32_t maxAttributeSize = 8;
    
    RHIPipelineLayout* pipelineLayout = nullptr;
};

/// Ray tracing pipeline handle
class RTPipeline {
public:
    virtual ~RTPipeline() = default;
    
    virtual const void* GetShaderGroupHandle(uint32_t groupIndex) const = 0;
    virtual uint32_t GetShaderGroupCount() const = 0;
};

/// Extended RHI device interface for ray tracing
class RHIDeviceRT {
public:
    virtual ~RHIDeviceRT() = default;
    
    virtual RTCapabilities GetRTCapabilities() const = 0;
    virtual bool IsRayTracingSupported() const = 0;
    
    virtual ASSizeInfo GetAccelerationStructureBuildSizes(
        const ASBuildInfo& buildInfo) const = 0;
    
    virtual std::unique_ptr<AccelerationStructure> CreateAccelerationStructure(
        AccelerationStructureType type,
        uint64_t size) = 0;
    
    virtual std::unique_ptr<RTPipeline> CreateRTPipeline(
        const RTPipelineDesc& desc) = 0;
    
    virtual std::unique_ptr<ShaderBindingTable> CreateShaderBindingTable(
        const RTPipeline* pipeline,
        const SBTDesc& desc) = 0;
};

/// Extended command buffer interface for ray tracing
class RHICommandBufferRT {
public:
    virtual ~RHICommandBufferRT() = default;
    
    virtual void BuildAccelerationStructure(
        const ASBuildInfo& buildInfo,
        AccelerationStructure* dst,
        RHIBuffer* scratchBuffer,
        uint64_t scratchOffset) = 0;
    
    virtual void CompactAccelerationStructure(
        AccelerationStructure* src,
        AccelerationStructure* dst) = 0;
    
    virtual void CopyAccelerationStructure(
        AccelerationStructure* src,
        AccelerationStructure* dst) = 0;
    
    virtual void TraceRays(
        const ShaderBindingTable* sbt,
        uint32_t width,
        uint32_t height,
        uint32_t depth = 1) = 0;
    
    virtual void BindRTPipeline(const RTPipeline* pipeline) = 0;
    
    virtual void WriteAccelerationStructureProperties(
        AccelerationStructure* as,
        RHIBuffer* buffer,
        uint64_t offset) = 0;
};

// Bitwise operators for flags
inline RTCapabilityFlags operator|(RTCapabilityFlags a, RTCapabilityFlags b) {
    return static_cast<RTCapabilityFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline RTCapabilityFlags operator&(RTCapabilityFlags a, RTCapabilityFlags b) {
    return static_cast<RTCapabilityFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline ASBuildFlags operator|(ASBuildFlags a, ASBuildFlags b) {
    return static_cast<ASBuildFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline ASBuildFlags operator&(ASBuildFlags a, ASBuildFlags b) {
    return static_cast<ASBuildFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline GeometryFlags operator|(GeometryFlags a, GeometryFlags b) {
    return static_cast<GeometryFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline GeometryFlags operator&(GeometryFlags a, GeometryFlags b) {
    return static_cast<GeometryFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

} // namespace AIEngine::RHI
