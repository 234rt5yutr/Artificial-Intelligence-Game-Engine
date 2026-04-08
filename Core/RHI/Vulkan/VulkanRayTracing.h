// Core/RHI/Vulkan/VulkanRayTracing.h

#pragma once
#include "../RHIRayTracing.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

namespace AIEngine::RHI {

/// Vulkan acceleration structure implementation
class VulkanAccelerationStructure : public AccelerationStructure {
public:
    VulkanAccelerationStructure(VkDevice device, VkAccelerationStructureKHR as,
                                 VkBuffer buffer, VkDeviceMemory memory,
                                 AccelerationStructureType type, uint64_t size);
    ~VulkanAccelerationStructure() override;
    
    AccelerationStructureType GetType() const override { return m_Type; }
    uint64_t GetDeviceAddress() const override { return m_DeviceAddress; }
    uint64_t GetSize() const override { return m_Size; }
    bool IsCompacted() const override { return m_IsCompacted; }
    
    VkAccelerationStructureKHR GetHandle() const { return m_AccelerationStructure; }
    VkBuffer GetBuffer() const { return m_Buffer; }
    
    void SetCompacted(bool compacted) { m_IsCompacted = compacted; }
    
private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkAccelerationStructureKHR m_AccelerationStructure = VK_NULL_HANDLE;
    VkBuffer m_Buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_Memory = VK_NULL_HANDLE;
    AccelerationStructureType m_Type;
    uint64_t m_Size = 0;
    uint64_t m_DeviceAddress = 0;
    bool m_IsCompacted = false;
};

/// Vulkan shader binding table implementation
class VulkanShaderBindingTable : public ShaderBindingTable {
public:
    VulkanShaderBindingTable(VkDevice device, VkBuffer buffer, VkDeviceMemory memory,
                              uint64_t rayGenOffset, uint64_t rayGenSize, uint64_t rayGenStride,
                              uint64_t missOffset, uint64_t missSize, uint64_t missStride,
                              uint64_t hitGroupOffset, uint64_t hitGroupSize, uint64_t hitGroupStride,
                              uint64_t callableOffset, uint64_t callableSize, uint64_t callableStride);
    ~VulkanShaderBindingTable() override;
    
    uint64_t GetRayGenAddress() const override { return m_BufferDeviceAddress + m_RayGenOffset; }
    uint64_t GetMissAddress() const override { return m_BufferDeviceAddress + m_MissOffset; }
    uint64_t GetHitGroupAddress() const override { return m_BufferDeviceAddress + m_HitGroupOffset; }
    uint64_t GetCallableAddress() const override { return m_BufferDeviceAddress + m_CallableOffset; }
    
    uint64_t GetRayGenStride() const override { return m_RayGenStride; }
    uint64_t GetMissStride() const override { return m_MissStride; }
    uint64_t GetHitGroupStride() const override { return m_HitGroupStride; }
    uint64_t GetCallableStride() const override { return m_CallableStride; }
    
    uint64_t GetRayGenSize() const override { return m_RayGenSize; }
    uint64_t GetMissSize() const override { return m_MissSize; }
    uint64_t GetHitGroupSize() const override { return m_HitGroupSize; }
    uint64_t GetCallableSize() const override { return m_CallableSize; }
    
    VkBuffer GetBuffer() const { return m_Buffer; }
    
    VkStridedDeviceAddressRegionKHR GetRayGenRegion() const;
    VkStridedDeviceAddressRegionKHR GetMissRegion() const;
    VkStridedDeviceAddressRegionKHR GetHitGroupRegion() const;
    VkStridedDeviceAddressRegionKHR GetCallableRegion() const;
    
private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkBuffer m_Buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_Memory = VK_NULL_HANDLE;
    uint64_t m_BufferDeviceAddress = 0;
    
    uint64_t m_RayGenOffset = 0;
    uint64_t m_RayGenSize = 0;
    uint64_t m_RayGenStride = 0;
    
    uint64_t m_MissOffset = 0;
    uint64_t m_MissSize = 0;
    uint64_t m_MissStride = 0;
    
    uint64_t m_HitGroupOffset = 0;
    uint64_t m_HitGroupSize = 0;
    uint64_t m_HitGroupStride = 0;
    
    uint64_t m_CallableOffset = 0;
    uint64_t m_CallableSize = 0;
    uint64_t m_CallableStride = 0;
};

/// Vulkan ray tracing pipeline implementation
class VulkanRTPipeline : public RTPipeline {
public:
    VulkanRTPipeline(VkDevice device, VkPipeline pipeline, VkPipelineLayout layout,
                      std::vector<uint8_t> shaderGroupHandles, uint32_t handleSize);
    ~VulkanRTPipeline() override;
    
    const void* GetShaderGroupHandle(uint32_t groupIndex) const override;
    uint32_t GetShaderGroupCount() const override;
    
    VkPipeline GetHandle() const { return m_Pipeline; }
    VkPipelineLayout GetLayout() const { return m_Layout; }
    uint32_t GetHandleSize() const { return m_HandleSize; }
    
private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkPipeline m_Pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_Layout = VK_NULL_HANDLE;
    std::vector<uint8_t> m_ShaderGroupHandles;
    uint32_t m_HandleSize = 0;
};

/// Vulkan ray tracing device extension
class VulkanDeviceRT : public RHIDeviceRT {
public:
    VulkanDeviceRT(VkPhysicalDevice physicalDevice, VkDevice device);
    ~VulkanDeviceRT() override = default;
    
    RTCapabilities GetRTCapabilities() const override { return m_Capabilities; }
    bool IsRayTracingSupported() const override { return m_IsSupported; }
    
    ASSizeInfo GetAccelerationStructureBuildSizes(
        const ASBuildInfo& buildInfo) const override;
    
    std::unique_ptr<AccelerationStructure> CreateAccelerationStructure(
        AccelerationStructureType type,
        uint64_t size) override;
    
    std::unique_ptr<RTPipeline> CreateRTPipeline(
        const RTPipelineDesc& desc) override;
    
    std::unique_ptr<ShaderBindingTable> CreateShaderBindingTable(
        const RTPipeline* pipeline,
        const SBTDesc& desc) override;
    
    // Vulkan-specific helpers
    VkDevice GetDevice() const { return m_Device; }
    VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
    
    // Query extension function pointers
    PFN_vkGetAccelerationStructureBuildSizesKHR GetAccelerationStructureBuildSizesFn() const {
        return m_vkGetAccelerationStructureBuildSizesKHR;
    }
    
private:
    void QueryCapabilities();
    void LoadFunctionPointers();
    
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;
    RTCapabilities m_Capabilities;
    bool m_IsSupported = false;
    
    // Extension function pointers
    PFN_vkGetAccelerationStructureBuildSizesKHR m_vkGetAccelerationStructureBuildSizesKHR = nullptr;
    PFN_vkCreateAccelerationStructureKHR m_vkCreateAccelerationStructureKHR = nullptr;
    PFN_vkDestroyAccelerationStructureKHR m_vkDestroyAccelerationStructureKHR = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR m_vkGetAccelerationStructureDeviceAddressKHR = nullptr;
    PFN_vkCreateRayTracingPipelinesKHR m_vkCreateRayTracingPipelinesKHR = nullptr;
    PFN_vkGetRayTracingShaderGroupHandlesKHR m_vkGetRayTracingShaderGroupHandlesKHR = nullptr;
};

/// Vulkan ray tracing command buffer extension
class VulkanCommandBufferRT : public RHICommandBufferRT {
public:
    VulkanCommandBufferRT(VkDevice device, VkCommandBuffer commandBuffer);
    ~VulkanCommandBufferRT() override = default;
    
    void BuildAccelerationStructure(
        const ASBuildInfo& buildInfo,
        AccelerationStructure* dst,
        RHIBuffer* scratchBuffer,
        uint64_t scratchOffset) override;
    
    void CompactAccelerationStructure(
        AccelerationStructure* src,
        AccelerationStructure* dst) override;
    
    void CopyAccelerationStructure(
        AccelerationStructure* src,
        AccelerationStructure* dst) override;
    
    void TraceRays(
        const ShaderBindingTable* sbt,
        uint32_t width,
        uint32_t height,
        uint32_t depth = 1) override;
    
    void BindRTPipeline(const RTPipeline* pipeline) override;
    
    void WriteAccelerationStructureProperties(
        AccelerationStructure* as,
        RHIBuffer* buffer,
        uint64_t offset) override;
    
private:
    void LoadFunctionPointers();
    
    VkDevice m_Device = VK_NULL_HANDLE;
    VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
    
    // Extension function pointers
    PFN_vkCmdBuildAccelerationStructuresKHR m_vkCmdBuildAccelerationStructuresKHR = nullptr;
    PFN_vkCmdCopyAccelerationStructureKHR m_vkCmdCopyAccelerationStructureKHR = nullptr;
    PFN_vkCmdTraceRaysKHR m_vkCmdTraceRaysKHR = nullptr;
    PFN_vkCmdWriteAccelerationStructuresPropertiesKHR m_vkCmdWriteAccelerationStructuresPropertiesKHR = nullptr;
};

/// Required Vulkan extensions for ray tracing
inline const std::vector<const char*> GetRequiredRTExtensions() {
    return {
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME
    };
}

/// Check if ray tracing extensions are supported by a physical device
bool CheckRTExtensionSupport(VkPhysicalDevice physicalDevice);

/// Get required features for ray tracing
VkPhysicalDeviceRayTracingPipelineFeaturesKHR GetRTRequiredFeatures();

/// Get required acceleration structure features
VkPhysicalDeviceAccelerationStructureFeaturesKHR GetASRequiredFeatures();

} // namespace AIEngine::RHI
