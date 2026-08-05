#include "VulkanGpuResources.h"

#include "Core/Log.h"
#include "Core/RHI/ShaderCompiler.h"

namespace Core {
namespace RHI {

    namespace {

        // Stage/access pair a layout implies. Conservative on purpose: these
        // transitions happen a handful of times per frame, not per draw.
        void LayoutStageAccess(VkImageLayout layout, VkPipelineStageFlags& stage, VkAccessFlags& access) {
            switch (layout) {
                case VK_IMAGE_LAYOUT_UNDEFINED:
                    stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                    access = 0;
                    break;
                case VK_IMAGE_LAYOUT_GENERAL:
                    stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                    access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                    break;
                case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                    stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                    access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    break;
                case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
                    stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                    access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    break;
                case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
                case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                    stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                    access = VK_ACCESS_SHADER_READ_BIT;
                    break;
                case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                    stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                    access = VK_ACCESS_TRANSFER_READ_BIT;
                    break;
                case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                    stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                    access = VK_ACCESS_TRANSFER_WRITE_BIT;
                    break;
                case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
                    stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
                    access = 0;
                    break;
                default:
                    stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                    access = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
                    break;
            }
        }

    } // namespace

    bool CreateGpuImage(VkDevice device, VmaAllocator allocator, const GpuImageDesc& desc, GpuImage& out) {
        out = GpuImage{};

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { desc.Width, desc.Height, 1 };
        imageInfo.mipLevels = desc.MipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.format = desc.Format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = desc.Usage;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &out.Image, &out.Allocation, nullptr) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("Failed to create image '{}' ({}x{})", desc.DebugName, desc.Width, desc.Height);
            out.Image = VK_NULL_HANDLE;
            return false;
        }

        out.Format = desc.Format;
        out.Extent = { desc.Width, desc.Height };
        out.MipLevels = desc.MipLevels;
        out.Aspect = desc.Aspect;
        out.Layout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = out.Image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = desc.Format;
        viewInfo.subresourceRange.aspectMask = desc.Aspect;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = desc.MipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &out.View) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("Failed to create image view for '{}'", desc.DebugName);
            DestroyGpuImage(device, allocator, out);
            return false;
        }

        if (desc.CreateMipViews) {
            out.MipViews.resize(desc.MipLevels, VK_NULL_HANDLE);
            for (uint32_t mip = 0; mip < desc.MipLevels; ++mip) {
                VkImageViewCreateInfo mipView = viewInfo;
                mipView.subresourceRange.baseMipLevel = mip;
                mipView.subresourceRange.levelCount = 1;
                if (vkCreateImageView(device, &mipView, nullptr, &out.MipViews[mip]) != VK_SUCCESS) {
                    ENGINE_CORE_ERROR("Failed to create mip {} view for '{}'", mip, desc.DebugName);
                    DestroyGpuImage(device, allocator, out);
                    return false;
                }
            }
        }

        return true;
    }

    void DestroyGpuImage(VkDevice device, VmaAllocator allocator, GpuImage& image) {
        for (VkImageView view : image.MipViews) {
            if (view != VK_NULL_HANDLE) {
                vkDestroyImageView(device, view, nullptr);
            }
        }
        image.MipViews.clear();

        if (image.View != VK_NULL_HANDLE) {
            vkDestroyImageView(device, image.View, nullptr);
            image.View = VK_NULL_HANDLE;
        }
        if (image.Image != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, image.Image, image.Allocation);
            image.Image = VK_NULL_HANDLE;
            image.Allocation = VK_NULL_HANDLE;
        }
        image.Layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    void TransitionImage(VkCommandBuffer cmd, GpuImage& image, VkImageLayout newLayout) {
        if (!image.IsValid() || image.Layout == newLayout) {
            return;
        }
        TransitionImageRange(cmd, image.Image, image.Aspect, 0, image.MipLevels, image.Layout, newLayout);
        image.Layout = newLayout;
    }

    void TransitionImageRange(VkCommandBuffer cmd,
                              VkImage image,
                              VkImageAspectFlags aspect,
                              uint32_t baseMip,
                              uint32_t mipCount,
                              VkImageLayout oldLayout,
                              VkImageLayout newLayout) {
        if (image == VK_NULL_HANDLE) {
            return;
        }

        VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        VkAccessFlags srcAccess = 0;
        VkAccessFlags dstAccess = 0;
        LayoutStageAccess(oldLayout, srcStage, srcAccess);
        LayoutStageAccess(newLayout, dstStage, dstAccess);

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = aspect;
        barrier.subresourceRange.baseMipLevel = baseMip;
        barrier.subresourceRange.levelCount = mipCount;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;

        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    bool CreateGpuBuffer(VmaAllocator allocator,
                         VkDeviceSize size,
                         VkBufferUsageFlags usage,
                         bool hostVisible,
                         GpuBuffer& out) {
        out = GpuBuffer{};
        if (size == 0) {
            return false;
        }

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        if (hostVisible) {
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                              VMA_ALLOCATION_CREATE_MAPPED_BIT;
        }

        VmaAllocationInfo allocated{};
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &out.Buffer, &out.Allocation, &allocated) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("Failed to allocate {} byte GPU buffer", static_cast<uint64_t>(size));
            out.Buffer = VK_NULL_HANDLE;
            return false;
        }

        out.Size = size;
        out.Mapped = hostVisible ? allocated.pMappedData : nullptr;
        return true;
    }

    void DestroyGpuBuffer(VmaAllocator allocator, GpuBuffer& buffer) {
        if (buffer.Buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, buffer.Buffer, buffer.Allocation);
            buffer.Buffer = VK_NULL_HANDLE;
            buffer.Allocation = VK_NULL_HANDLE;
            buffer.Mapped = nullptr;
            buffer.Size = 0;
        }
    }

    void BufferBarrier(VkCommandBuffer cmd,
                       VkBuffer buffer,
                       VkAccessFlags srcAccess,
                       VkAccessFlags dstAccess,
                       VkPipelineStageFlags srcStage,
                       VkPipelineStageFlags dstStage) {
        if (buffer == VK_NULL_HANDLE) {
            return;
        }

        VkBufferMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = buffer;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;

        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 1, &barrier, 0, nullptr);
    }

    ComputePipeline CreateComputePipeline(VkDevice device,
                                          VkPipelineCache cache,
                                          const std::string& glslSource,
                                          const std::string& debugName,
                                          const std::vector<VkDescriptorSetLayout>& setLayouts,
                                          uint32_t pushConstantSize) {
        ComputePipeline result{};

        ShaderCompileRequest request{};
        request.Source = glslSource;
        request.Stage = ShaderStage::Compute;
        request.SourceName = debugName;
        const auto compiled = ShaderCompiler::CompileToSPIRVChecked(request);
        if (!compiled.Succeeded || compiled.Spirv.empty()) {
            ENGINE_CORE_ERROR("Compute shader '{}' failed to compile: {}", debugName, compiled.ErrorMessage);
            return result;
        }

        VkShaderModuleCreateInfo moduleInfo{};
        moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        moduleInfo.codeSize = compiled.Spirv.size() * sizeof(uint32_t);
        moduleInfo.pCode = compiled.Spirv.data();

        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &moduleInfo, nullptr, &module) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("Failed to create shader module for '{}'", debugName);
            return result;
        }

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = pushConstantSize;

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.empty() ? nullptr : setLayouts.data();
        layoutInfo.pushConstantRangeCount = pushConstantSize > 0 ? 1u : 0u;
        layoutInfo.pPushConstantRanges = pushConstantSize > 0 ? &pushRange : nullptr;

        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &result.Layout) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("Failed to create pipeline layout for '{}'", debugName);
            vkDestroyShaderModule(device, module, nullptr);
            return result;
        }

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = module;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = result.Layout;

        if (vkCreateComputePipelines(device, cache, 1, &pipelineInfo, nullptr, &result.Pipeline) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("Failed to create compute pipeline '{}'", debugName);
            vkDestroyPipelineLayout(device, result.Layout, nullptr);
            result.Layout = VK_NULL_HANDLE;
        }

        vkDestroyShaderModule(device, module, nullptr);
        return result;
    }

    void DestroyComputePipeline(VkDevice device, ComputePipeline& pipeline) {
        if (pipeline.Pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline.Pipeline, nullptr);
            pipeline.Pipeline = VK_NULL_HANDLE;
        }
        if (pipeline.Layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, pipeline.Layout, nullptr);
            pipeline.Layout = VK_NULL_HANDLE;
        }
    }

    VkDescriptorSetLayout CreateComputeSetLayout(VkDevice device,
                                                 const std::vector<VkDescriptorType>& bindings) {
        std::vector<VkDescriptorSetLayoutBinding> layoutBindings(bindings.size());
        for (std::size_t i = 0; i < bindings.size(); ++i) {
            layoutBindings[i].binding = static_cast<uint32_t>(i);
            layoutBindings[i].descriptorType = bindings[i];
            layoutBindings[i].descriptorCount = 1;
            layoutBindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }

        VkDescriptorSetLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = static_cast<uint32_t>(layoutBindings.size());
        info.pBindings = layoutBindings.empty() ? nullptr : layoutBindings.data();

        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        if (vkCreateDescriptorSetLayout(device, &info, nullptr, &layout) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("Failed to create compute descriptor set layout");
            return VK_NULL_HANDLE;
        }
        return layout;
    }

    VkSampler CreateClampedSampler(VkDevice device, VkFilter filter) {
        VkSamplerCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = filter;
        info.minFilter = filter;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.minLod = 0.0f;
        info.maxLod = VK_LOD_CLAMP_NONE;
        info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

        VkSampler sampler = VK_NULL_HANDLE;
        if (vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("Failed to create sampler");
            return VK_NULL_HANDLE;
        }
        return sampler;
    }

} // namespace RHI
} // namespace Core
