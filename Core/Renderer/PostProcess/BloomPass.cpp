#include "BloomPass.h"
#include "Core/ECS/Components/PostProcessComponent.h"
#include <stdexcept>
#include <algorithm>
#include <cstring>

namespace Core {
namespace Renderer {

BloomPass::~BloomPass() {
    Cleanup();
}

void BloomPass::Initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                           VkRenderPass renderPass, VkExtent2D extent) {
    m_Device = device;
    m_PhysicalDevice = physicalDevice;
    m_RenderPass = renderPass;
    m_Extent = extent;

    CreateMipChain();
    CreateDescriptorSetLayout();
    CreateDescriptorPool();
    AllocateDescriptorSets();
    CreatePipelines();
}

void BloomPass::CreateMipChain() {
    DestroyMipChain();

    VkExtent2D mipExtent = m_Extent;
    m_MipChain.resize(m_MipLevels);

    for (int i = 0; i < m_MipLevels; ++i) {
        mipExtent.width = std::max(1u, mipExtent.width / 2);
        mipExtent.height = std::max(1u, mipExtent.height / 2);

        MipLevel& mip = m_MipChain[i];
        mip.extent = mipExtent;

        // Create image
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        imageInfo.extent = {mipExtent.width, mipExtent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (vkCreateImage(m_Device, &imageInfo, nullptr, &mip.image) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create bloom mip image");
        }

        // Allocate memory
        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(m_Device, mip.image, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &mip.memory) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate bloom mip memory");
        }

        vkBindImageMemory(m_Device, mip.image, mip.memory, 0);

        // Create image view
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = mip.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_Device, &viewInfo, nullptr, &mip.view) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create bloom mip image view");
        }

        // Create framebuffer
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_RenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &mip.view;
        fbInfo.width = mipExtent.width;
        fbInfo.height = mipExtent.height;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(m_Device, &fbInfo, nullptr, &mip.framebuffer) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create bloom mip framebuffer");
        }
    }
}

void BloomPass::CreateDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerBinding;

    if (vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create bloom descriptor set layout");
    }
}

void BloomPass::CreateDescriptorPool() {
    // Need descriptors for each mip level + source + output
    uint32_t maxSets = static_cast<uint32_t>(m_MipLevels * 2 + 2);

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = maxSets;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = maxSets;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    if (vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create bloom descriptor pool");
    }
}

void BloomPass::AllocateDescriptorSets() {
    // Allocate descriptor sets for each mip level
    m_DescriptorSets.resize(m_MipLevels * 2 + 2);

    std::vector<VkDescriptorSetLayout> layouts(m_DescriptorSets.size(), m_DescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_DescriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(m_DescriptorSets.size());
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(m_Device, &allocInfo, m_DescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate bloom descriptor sets");
    }
}

void BloomPass::CreatePipelines() {
    // Push constant range for bloom parameters
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(BloomPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_DescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create bloom pipeline layout");
    }

    // Note: Actual pipeline creation requires shader modules
    // This would load fullscreen_quad.vert + bloom_downsample.frag / bloom_upsample.frag / bloom_composite.frag
    // For now, pipelines are created as stubs - actual shader loading would be done by the renderer
}

void BloomPass::DestroyMipChain() {
    for (auto& mip : m_MipChain) {
        if (mip.framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_Device, mip.framebuffer, nullptr);
        }
        if (mip.view != VK_NULL_HANDLE) {
            vkDestroyImageView(m_Device, mip.view, nullptr);
        }
        if (mip.image != VK_NULL_HANDLE) {
            vkDestroyImage(m_Device, mip.image, nullptr);
        }
        if (mip.memory != VK_NULL_HANDLE) {
            vkFreeMemory(m_Device, mip.memory, nullptr);
        }
    }
    m_MipChain.clear();
}

void BloomPass::Execute(VkCommandBuffer cmd, FramebufferChain& chain,
                        const ECS::PostProcessSettings& settings) {
    if (!IsEnabled(settings)) return;

    BloomSettings bloomSettings;
    bloomSettings.threshold = settings.bloomThreshold;
    bloomSettings.softKnee = settings.bloomSoftKnee;
    bloomSettings.intensity = settings.bloomIntensity;
    bloomSettings.scatter = settings.bloomScatter;
    bloomSettings.mipLevels = std::min(settings.bloomMipLevels, m_MipLevels);

    // Downsample pass - progressively blur and threshold
    for (int i = 0; i < bloomSettings.mipLevels - 1; ++i) {
        DownsamplePass(cmd, i - 1, i, bloomSettings);
    }

    // Upsample pass - blend mip levels back together
    for (int i = bloomSettings.mipLevels - 2; i >= 0; --i) {
        UpsamplePass(cmd, i + 1, i, bloomSettings);
    }

    // Composite bloom with original scene
    CompositePass(cmd, chain, bloomSettings);
}

void BloomPass::DownsamplePass(VkCommandBuffer cmd, int srcMip, int dstMip, 
                                const BloomSettings& bloomSettings) {
    // Transition source to shader read
    VkImage srcImage = (srcMip < 0) ? VK_NULL_HANDLE : m_MipChain[srcMip].image;
    VkImage dstImage = m_MipChain[dstMip].image;
    VkExtent2D dstExtent = m_MipChain[dstMip].extent;

    if (srcImage != VK_NULL_HANDLE) {
        TransitionImageLayout(cmd, srcImage,
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    TransitionImageLayout(cmd, dstImage,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // Begin render pass
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_RenderPass;
    renderPassInfo.framebuffer = m_MipChain[dstMip].framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = dstExtent;

    VkClearValue clearValue{};
    clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Bind pipeline and descriptor set
    if (m_DownsamplePipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_DownsamplePipeline);

        int descriptorIndex = (srcMip < 0) ? 0 : srcMip + 1;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout,
                                0, 1, &m_DescriptorSets[descriptorIndex], 0, nullptr);

        // Push constants
        BloomPushConstants pushConstants{};
        pushConstants.threshold = (srcMip < 0) ? bloomSettings.threshold : 0.0f;
        pushConstants.softKnee = bloomSettings.softKnee;
        pushConstants.intensity = bloomSettings.intensity;
        pushConstants.scatter = bloomSettings.scatter;
        pushConstants.texelSize[0] = 1.0f / static_cast<float>(dstExtent.width);
        pushConstants.texelSize[1] = 1.0f / static_cast<float>(dstExtent.height);
        pushConstants.mipLevel = dstMip;
        pushConstants.isUpsampling = 0;

        vkCmdPushConstants(cmd, m_PipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(BloomPushConstants), &pushConstants);

        // Draw fullscreen triangle
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
}

void BloomPass::UpsamplePass(VkCommandBuffer cmd, int srcMip, int dstMip,
                              const BloomSettings& bloomSettings) {
    VkImage srcImage = m_MipChain[srcMip].image;
    VkImage dstImage = m_MipChain[dstMip].image;
    VkExtent2D dstExtent = m_MipChain[dstMip].extent;

    TransitionImageLayout(cmd, srcImage,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Note: dst is already in color attachment optimal from previous pass

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_RenderPass;
    renderPassInfo.framebuffer = m_MipChain[dstMip].framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = dstExtent;

    VkClearValue clearValue{};
    clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    if (m_UpsamplePipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_UpsamplePipeline);

        int descriptorIndex = m_MipLevels + srcMip;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout,
                                0, 1, &m_DescriptorSets[descriptorIndex], 0, nullptr);

        BloomPushConstants pushConstants{};
        pushConstants.threshold = 0.0f;
        pushConstants.softKnee = bloomSettings.softKnee;
        pushConstants.intensity = bloomSettings.intensity;
        pushConstants.scatter = bloomSettings.scatter;
        pushConstants.texelSize[0] = 1.0f / static_cast<float>(dstExtent.width);
        pushConstants.texelSize[1] = 1.0f / static_cast<float>(dstExtent.height);
        pushConstants.mipLevel = dstMip;
        pushConstants.isUpsampling = 1;

        vkCmdPushConstants(cmd, m_PipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(BloomPushConstants), &pushConstants);

        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
}

void BloomPass::CompositePass(VkCommandBuffer cmd, FramebufferChain& chain,
                               const BloomSettings& bloomSettings) {
    // Transition bloom result to shader read
    if (!m_MipChain.empty()) {
        TransitionImageLayout(cmd, m_MipChain[0].image,
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_RenderPass;
    renderPassInfo.framebuffer = chain.GetCurrentFramebuffer();
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = chain.GetExtent();

    VkClearValue clearValue{};
    clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    if (m_CompositePipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_CompositePipeline);

        // Descriptor set with scene color + bloom result
        int descriptorIndex = static_cast<int>(m_DescriptorSets.size()) - 1;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout,
                                0, 1, &m_DescriptorSets[descriptorIndex], 0, nullptr);

        BloomPushConstants pushConstants{};
        pushConstants.intensity = bloomSettings.intensity;
        pushConstants.texelSize[0] = 1.0f / static_cast<float>(chain.GetExtent().width);
        pushConstants.texelSize[1] = 1.0f / static_cast<float>(chain.GetExtent().height);

        vkCmdPushConstants(cmd, m_PipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(BloomPushConstants), &pushConstants);

        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
}

void BloomPass::TransitionImageLayout(VkCommandBuffer cmd, VkImage image,
                                       VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags srcStage;
    VkPipelineStageFlags dstStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
}

void BloomPass::Resize(VkExtent2D newExtent) {
    m_Extent = newExtent;
    CreateMipChain();
}

void BloomPass::Cleanup() {
    DestroyMipChain();

    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }

    if (m_DescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
        m_DescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (m_DownsamplePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_Device, m_DownsamplePipeline, nullptr);
        m_DownsamplePipeline = VK_NULL_HANDLE;
    }

    if (m_UpsamplePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_Device, m_UpsamplePipeline, nullptr);
        m_UpsamplePipeline = VK_NULL_HANDLE;
    }

    if (m_CompositePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_Device, m_CompositePipeline, nullptr);
        m_CompositePipeline = VK_NULL_HANDLE;
    }

    if (m_PipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
        m_PipelineLayout = VK_NULL_HANDLE;
    }
}

bool BloomPass::IsEnabled(const ECS::PostProcessSettings& settings) const {
    return settings.bloomEnabled && settings.bloomIntensity > 0.0f;
}

uint32_t BloomPass::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type for bloom");
}

} // namespace Renderer
} // namespace Core
