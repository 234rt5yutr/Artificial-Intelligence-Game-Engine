#include "MotionBlurPass.h"
#include "Core/ECS/Components/PostProcessComponent.h"
#include <stdexcept>
#include <cstring>

namespace Core {
namespace Renderer {

MotionBlurPass::~MotionBlurPass() {
    Cleanup();
}

void MotionBlurPass::Initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                 VkRenderPass renderPass, VkExtent2D extent) {
    m_Device = device;
    m_PhysicalDevice = physicalDevice;
    m_RenderPass = renderPass;
    m_Extent = extent;

    CreateVelocityBuffer();
    CreateDescriptorSetLayout();
    CreateDescriptorPool();
    AllocateDescriptorSets();
    CreatePipelines();
}

void MotionBlurPass::CreateVelocityBuffer() {
    // Camera motion uniform buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(CameraMotionData);
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_Device, &bufferInfo, nullptr, &m_CameraBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create motion blur camera buffer");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_Device, m_CameraBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_CameraMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate motion blur camera memory");
    }

    vkBindBufferMemory(m_Device, m_CameraBuffer, m_CameraMemory, 0);

    // Tile max velocity buffer (1/8 resolution)
    VkExtent2D tileExtent = {m_Extent.width / 8, m_Extent.height / 8};

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16G16_SFLOAT;  // 2D velocity
    imageInfo.extent = {tileExtent.width, tileExtent.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_Device, &imageInfo, nullptr, &m_TileMaxImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create motion blur tile max image");
    }

    vkGetImageMemoryRequirements(m_Device, m_TileMaxImage, &memRequirements);

    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_TileMaxMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate motion blur tile max memory");
    }

    vkBindImageMemory(m_Device, m_TileMaxImage, m_TileMaxMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_TileMaxImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_TileMaxView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create motion blur tile max image view");
    }
}

void MotionBlurPass::CreateDescriptorSetLayout() {
    // Scene color + velocity + tile max + camera buffer
    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
    
    // Scene color
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Velocity buffer
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Tile max velocity
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Camera motion buffer
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create motion blur descriptor set layout");
    }
}

void MotionBlurPass::CreateDescriptorPool() {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 8;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = 2;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 3;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    if (vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create motion blur descriptor pool");
    }
}

void MotionBlurPass::AllocateDescriptorSets() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_DescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_DescriptorSetLayout;

    if (vkAllocateDescriptorSets(m_Device, &allocInfo, &m_TileMaxDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate motion blur tile max descriptor set");
    }

    if (vkAllocateDescriptorSets(m_Device, &allocInfo, &m_MotionBlurDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate motion blur descriptor set");
    }
}

void MotionBlurPass::CreatePipelines() {
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(MotionBlurPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_DescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create motion blur pipeline layout");
    }

    // Note: Actual pipeline creation requires shader modules
}

void MotionBlurPass::UpdateCameraMotion(const CameraMotionData& data) {
    void* mapped;
    vkMapMemory(m_Device, m_CameraMemory, 0, sizeof(CameraMotionData), 0, &mapped);
    std::memcpy(mapped, &data, sizeof(CameraMotionData));
    vkUnmapMemory(m_Device, m_CameraMemory);
}

void MotionBlurPass::Execute(VkCommandBuffer cmd, FramebufferChain& chain,
                              const ECS::PostProcessSettings& settings) {
    if (!IsEnabled(settings)) return;

    MotionBlurSettings mbSettings;
    mbSettings.scale = settings.motionBlurScale;
    mbSettings.samples = settings.motionBlurSamples;

    // Transition tile max image
    TransitionImageLayout(cmd, m_TileMaxImage,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // Pass 1: Calculate tile max velocity
    VkExtent2D tileExtent = {m_Extent.width / 8, m_Extent.height / 8};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_RenderPass;
    renderPassInfo.framebuffer = m_TileMaxFramebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = tileExtent;

    VkClearValue clearValue{};
    clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    if (m_TileMaxPipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_TileMaxPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout,
                                0, 1, &m_TileMaxDescriptorSet, 0, nullptr);

        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);

    // Transition tile max for reading
    TransitionImageLayout(cmd, m_TileMaxImage,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Pass 2: Apply motion blur
    renderPassInfo.framebuffer = chain.GetCurrentFramebuffer();
    renderPassInfo.renderArea.extent = chain.GetExtent();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    if (m_MotionBlurPipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_MotionBlurPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout,
                                0, 1, &m_MotionBlurDescriptorSet, 0, nullptr);

        MotionBlurPushConstants pushConstants{};
        pushConstants.scale = mbSettings.scale;
        pushConstants.samples = mbSettings.samples;
        pushConstants.maxVelocity = mbSettings.maxVelocity;
        pushConstants.texelSize[0] = 1.0f / static_cast<float>(m_Extent.width);
        pushConstants.texelSize[1] = 1.0f / static_cast<float>(m_Extent.height);
        pushConstants.centerWeight = 1.0f;

        vkCmdPushConstants(cmd, m_PipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(MotionBlurPushConstants), &pushConstants);

        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
}

void MotionBlurPass::TransitionImageLayout(VkCommandBuffer cmd, VkImage image,
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

void MotionBlurPass::Resize(VkExtent2D newExtent) {
    m_Extent = newExtent;

    // Cleanup tile max resources
    if (m_TileMaxFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_Device, m_TileMaxFramebuffer, nullptr);
        m_TileMaxFramebuffer = VK_NULL_HANDLE;
    }
    if (m_TileMaxView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_Device, m_TileMaxView, nullptr);
        m_TileMaxView = VK_NULL_HANDLE;
    }
    if (m_TileMaxImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_Device, m_TileMaxImage, nullptr);
        m_TileMaxImage = VK_NULL_HANDLE;
    }
    if (m_TileMaxMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_Device, m_TileMaxMemory, nullptr);
        m_TileMaxMemory = VK_NULL_HANDLE;
    }

    // Recreate (the camera buffer stays the same size)
    VkExtent2D tileExtent = {m_Extent.width / 8, m_Extent.height / 8};

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16G16_SFLOAT;
    imageInfo.extent = {tileExtent.width, tileExtent.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_Device, &imageInfo, nullptr, &m_TileMaxImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create motion blur tile max image on resize");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_Device, m_TileMaxImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_TileMaxMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate motion blur tile max memory on resize");
    }

    vkBindImageMemory(m_Device, m_TileMaxImage, m_TileMaxMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_TileMaxImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_TileMaxView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create motion blur tile max image view on resize");
    }
}

void MotionBlurPass::Cleanup() {
    if (m_TileMaxFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_Device, m_TileMaxFramebuffer, nullptr);
        m_TileMaxFramebuffer = VK_NULL_HANDLE;
    }

    if (m_TileMaxView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_Device, m_TileMaxView, nullptr);
        m_TileMaxView = VK_NULL_HANDLE;
    }

    if (m_TileMaxImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_Device, m_TileMaxImage, nullptr);
        m_TileMaxImage = VK_NULL_HANDLE;
    }

    if (m_TileMaxMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_Device, m_TileMaxMemory, nullptr);
        m_TileMaxMemory = VK_NULL_HANDLE;
    }

    if (m_CameraBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_Device, m_CameraBuffer, nullptr);
        m_CameraBuffer = VK_NULL_HANDLE;
    }

    if (m_CameraMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_Device, m_CameraMemory, nullptr);
        m_CameraMemory = VK_NULL_HANDLE;
    }

    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }

    if (m_DescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
        m_DescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (m_TileMaxPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_Device, m_TileMaxPipeline, nullptr);
        m_TileMaxPipeline = VK_NULL_HANDLE;
    }

    if (m_MotionBlurPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_Device, m_MotionBlurPipeline, nullptr);
        m_MotionBlurPipeline = VK_NULL_HANDLE;
    }

    if (m_PipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
        m_PipelineLayout = VK_NULL_HANDLE;
    }
}

bool MotionBlurPass::IsEnabled(const ECS::PostProcessSettings& settings) const {
    return settings.motionBlurEnabled && settings.motionBlurScale > 0.0f;
}

uint32_t MotionBlurPass::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type for motion blur");
}

} // namespace Renderer
} // namespace Core
