#pragma once

// Small allocation/barrier helpers shared by the GPU-driven culling, GI, and
// upscaling passes.
//
// The RHI's VulkanTexture/VulkanBuffer are asset-facing: they carry no image
// view, no per-mip views, and no layout tracking, so a render pass cannot use
// them as an attachment or a storage image. Rather than widen the RHI (which
// would force every asset path through render-target concerns), the render
// passes share these thin structs.

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Core {
namespace RHI {

    // An image plus everything needed to bind it: a whole-resource view, one
    // view per mip (storage images can only be bound a single mip at a time),
    // and the layout it was last transitioned to.
    struct GpuImage {
        VkImage Image = VK_NULL_HANDLE;
        VmaAllocation Allocation = VK_NULL_HANDLE;
        VkImageView View = VK_NULL_HANDLE;
        std::vector<VkImageView> MipViews;
        VkFormat Format = VK_FORMAT_UNDEFINED;
        VkExtent2D Extent{0, 0};
        uint32_t MipLevels = 1;
        VkImageAspectFlags Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;

        bool IsValid() const { return Image != VK_NULL_HANDLE; }
    };

    struct GpuImageDesc {
        uint32_t Width = 1;
        uint32_t Height = 1;
        uint32_t MipLevels = 1;
        VkFormat Format = VK_FORMAT_R16G16B16A16_SFLOAT;
        VkImageUsageFlags Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        VkImageAspectFlags Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        // Per-mip views cost one VkImageView each; only the HZB needs them.
        bool CreateMipViews = false;
        const char* DebugName = "GpuImage";
    };

    bool CreateGpuImage(VkDevice device, VmaAllocator allocator, const GpuImageDesc& desc, GpuImage& out);
    void DestroyGpuImage(VkDevice device, VmaAllocator allocator, GpuImage& image);

    // Full-subresource layout transition with conservative stage/access masks.
    // ponytail: one barrier helper covers every transition these passes need;
    // split it per-usage only if a profile shows the pipeline stalling on it.
    void TransitionImage(VkCommandBuffer cmd, GpuImage& image, VkImageLayout newLayout);

    // Explicit-range variant for the HZB's mip-by-mip downsample chain.
    void TransitionImageRange(VkCommandBuffer cmd,
                              VkImage image,
                              VkImageAspectFlags aspect,
                              uint32_t baseMip,
                              uint32_t mipCount,
                              VkImageLayout oldLayout,
                              VkImageLayout newLayout);

    struct GpuBuffer {
        VkBuffer Buffer = VK_NULL_HANDLE;
        VmaAllocation Allocation = VK_NULL_HANDLE;
        VkDeviceSize Size = 0;
        void* Mapped = nullptr;

        bool IsValid() const { return Buffer != VK_NULL_HANDLE; }
    };

    // `hostVisible` picks a mapped upload heap; otherwise device-local.
    bool CreateGpuBuffer(VmaAllocator allocator,
                         VkDeviceSize size,
                         VkBufferUsageFlags usage,
                         bool hostVisible,
                         GpuBuffer& out);
    void DestroyGpuBuffer(VmaAllocator allocator, GpuBuffer& buffer);

    void BufferBarrier(VkCommandBuffer cmd,
                       VkBuffer buffer,
                       VkAccessFlags srcAccess,
                       VkAccessFlags dstAccess,
                       VkPipelineStageFlags srcStage,
                       VkPipelineStageFlags dstStage);

    struct ComputePipeline {
        VkPipeline Pipeline = VK_NULL_HANDLE;
        VkPipelineLayout Layout = VK_NULL_HANDLE;

        bool IsValid() const { return Pipeline != VK_NULL_HANDLE; }
    };

    // Compiles GLSL to SPIR-V and builds a compute pipeline in one step. Returns
    // an invalid pipeline (and logs) on failure rather than asserting, so a
    // shader problem degrades one pass instead of taking the process down.
    ComputePipeline CreateComputePipeline(VkDevice device,
                                          VkPipelineCache cache,
                                          const std::string& glslSource,
                                          const std::string& debugName,
                                          const std::vector<VkDescriptorSetLayout>& setLayouts,
                                          uint32_t pushConstantSize);
    void DestroyComputePipeline(VkDevice device, ComputePipeline& pipeline);

    // Descriptor set layout from a flat binding list, all visible to compute.
    VkDescriptorSetLayout CreateComputeSetLayout(VkDevice device,
                                                 const std::vector<VkDescriptorType>& bindings);

    VkSampler CreateClampedSampler(VkDevice device, VkFilter filter);

    // Load-time submission: begin a one-shot command buffer, record, then submit
    // and wait. Used by asset upload, which happens outside the frame loop and
    // has no other way onto the queue - the RHI still owns no transfer path.
    //
    // Not thread-safe against frame submission: callers must run inside the
    // frame's idle window (which is where MCP tool handlers already run).
    VkCommandBuffer BeginImmediateCommands(VkDevice device, VkCommandPool pool);
    bool EndImmediateCommands(VkDevice device, VkCommandPool pool, VkQueue queue,
                              VkCommandBuffer commandBuffer);

} // namespace RHI
} // namespace Core
