#include "DepthOfFieldPass.h"
#include "Core/ECS/Components/PostProcessComponent.h"
#include <stdexcept>
#include <algorithm>

namespace Core {
namespace Renderer {

DepthOfFieldPass::~DepthOfFieldPass() {
    Cleanup();
}

void DepthOfFieldPass::Initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                   VkRenderPass renderPass, VkExtent2D extent) {
    m_Device = device;
    m_PhysicalDevice = physicalDevice;
    m_RenderPass = renderPass;
    m_Extent = extent;

    CreateCoCBuffer();
    CreateDescriptorSetLayout();
    CreateDescriptorPool();
    AllocateDescriptorSets();
    CreatePipelines();
}

void DepthOfFieldPass::CreateCoCBuffer() {
    // CoC buffer (R16F - signed CoC values)
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16_SFLOAT;
    imageInfo.extent = {m_Extent.width, m_Extent.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_Device, &imageInfo, nullptr, &m_CoCImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create DoF CoC image");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_Device, m_CoCImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_CoCMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate DoF CoC memory");
    }

    vkBindImageMemory(m_Device, m_CoCImage, m_CoCMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_CoCImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_CoCView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create DoF CoC image view");
    }

    // Near and Far blur buffers at half resolution
    VkExtent2D halfExtent = {m_Extent.width / 2, m_Extent.height / 2};
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.extent = {halfExtent.width, halfExtent.height, 1};

    // Near field
    if (vkCreateImage(m_Device, &imageInfo, nullptr, &m_NearImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create DoF near image");
    }

    vkGetImageMemoryRequirements(m_Device, m_NearImage, &memRequirements);
    allocInfo.allocationSize = memRequirements.size;

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_NearMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate DoF near memory");
    }

    vkBindImageMemory(m_Device, m_NearImage, m_NearMemory, 0);

    viewInfo.image = m_NearImage;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;

    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_NearView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create DoF near image view");
    }

    // Far field
    if (vkCreateImage(m_Device, &imageInfo, nullptr, &m_FarImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create DoF far image");
    }

    vkGetImageMemoryRequirements(m_Device, m_FarImage, &memRequirements);
    allocInfo.allocationSize = memRequirements.size;

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_FarMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate DoF far memory");
    }

    vkBindImageMemory(m_Device, m_FarImage, m_FarMemory, 0);

    viewInfo.image = m_FarImage;

    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_FarView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create DoF far image view");
    }
}

void DepthOfFieldPass::CreateDescriptorSetLayout() {
    // Scene color + depth + CoC textures
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
    
    for (int i = 0; i < 3; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create DoF descriptor set layout");
    }
}

void DepthOfFieldPass::CreateDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 12;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 4;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    if (vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create DoF descriptor pool");
    }
}

void DepthOfFieldPass::AllocateDescriptorSets() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_DescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_DescriptorSetLayout;

    if (vkAllocateDescriptorSets(m_Device, &allocInfo, &m_CoCDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate DoF CoC descriptor set");
    }

    if (vkAllocateDescriptorSets(m_Device, &allocInfo, &m_BlurDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate DoF blur descriptor set");
    }

    if (vkAllocateDescriptorSets(m_Device, &allocInfo, &m_CompositeDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate DoF composite descriptor set");
    }
}

void DepthOfFieldPass::CreatePipelines() {
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(DoFPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_DescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create DoF pipeline layout");
    }

    // Note: Actual pipeline creation requires shader modules
}

void DepthOfFieldPass::Execute(VkCommandBuffer cmd, FramebufferChain& chain,
                                const ECS::PostProcessSettings& settings) {
    if (!IsEnabled(settings)) return;

    DepthOfFieldSettings dofSettings;
    dofSettings.focalDistance = settings.dofFocalDistance;
    dofSettings.focalRange = settings.dofFocalRange;
    dofSettings.maxBlur = settings.dofMaxBlur;

    // 1. Calculate Circle of Confusion
    CoCPass(cmd, dofSettings);

    // 2. Blur passes (separable)
    BlurPass(cmd, true);   // Horizontal
    BlurPass(cmd, false);  // Vertical

    // 3. Composite with original
    CompositePass(cmd, chain);
}

void DepthOfFieldPass::CoCPass(VkCommandBuffer cmd, const DepthOfFieldSettings& settings) {
    TransitionImageLayout(cmd, m_CoCImage,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_RenderPass;
    renderPassInfo.framebuffer = m_CoCFramebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_Extent;

    VkClearValue clearValue{};
    clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    if (m_CoCPipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_CoCPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout,
                                0, 1, &m_CoCDescriptorSet, 0, nullptr);

        DoFPushConstants pushConstants{};
        pushConstants.focalDistance = settings.focalDistance;
        pushConstants.focalRange = settings.focalRange;
        pushConstants.maxBlur = settings.maxBlur;
        pushConstants.aperture = settings.aperture;
        pushConstants.texelSize[0] = 1.0f / static_cast<float>(m_Extent.width);
        pushConstants.texelSize[1] = 1.0f / static_cast<float>(m_Extent.height);
        pushConstants.nearPlane = 0.1f;
        pushConstants.farPlane = 1000.0f;

        vkCmdPushConstants(cmd, m_PipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(DoFPushConstants), &pushConstants);

        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
}

void DepthOfFieldPass::BlurPass(VkCommandBuffer cmd, bool horizontal) {
    VkImage targetImage = horizontal ? m_NearImage : m_FarImage;
    VkFramebuffer framebuffer = horizontal ? m_NearFramebuffer : m_FarFramebuffer;

    TransitionImageLayout(cmd, targetImage,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    VkExtent2D halfExtent = {m_Extent.width / 2, m_Extent.height / 2};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_RenderPass;
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = halfExtent;

    VkClearValue clearValue{};
    clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    if (m_BlurPipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_BlurPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout,
                                0, 1, &m_BlurDescriptorSet, 0, nullptr);

        DoFPushConstants pushConstants{};
        pushConstants.texelSize[0] = 2.0f / static_cast<float>(m_Extent.width);
        pushConstants.texelSize[1] = 2.0f / static_cast<float>(m_Extent.height);
        pushConstants.horizontal = horizontal ? 1 : 0;

        vkCmdPushConstants(cmd, m_PipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(DoFPushConstants), &pushConstants);

        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
}

void DepthOfFieldPass::CompositePass(VkCommandBuffer cmd, FramebufferChain& chain) {
    TransitionImageLayout(cmd, m_NearImage,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    TransitionImageLayout(cmd, m_FarImage,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    TransitionImageLayout(cmd, m_CoCImage,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

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
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout,
                                0, 1, &m_CompositeDescriptorSet, 0, nullptr);

        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
}

void DepthOfFieldPass::TransitionImageLayout(VkCommandBuffer cmd, VkImage image,
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

void DepthOfFieldPass::Resize(VkExtent2D newExtent) {
    m_Extent = newExtent;

    // Cleanup existing buffers
    if (m_CoCFramebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(m_Device, m_CoCFramebuffer, nullptr);
    if (m_NearFramebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(m_Device, m_NearFramebuffer, nullptr);
    if (m_FarFramebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(m_Device, m_FarFramebuffer, nullptr);
    if (m_CoCView != VK_NULL_HANDLE) vkDestroyImageView(m_Device, m_CoCView, nullptr);
    if (m_NearView != VK_NULL_HANDLE) vkDestroyImageView(m_Device, m_NearView, nullptr);
    if (m_FarView != VK_NULL_HANDLE) vkDestroyImageView(m_Device, m_FarView, nullptr);
    if (m_CoCImage != VK_NULL_HANDLE) vkDestroyImage(m_Device, m_CoCImage, nullptr);
    if (m_NearImage != VK_NULL_HANDLE) vkDestroyImage(m_Device, m_NearImage, nullptr);
    if (m_FarImage != VK_NULL_HANDLE) vkDestroyImage(m_Device, m_FarImage, nullptr);
    if (m_CoCMemory != VK_NULL_HANDLE) vkFreeMemory(m_Device, m_CoCMemory, nullptr);
    if (m_NearMemory != VK_NULL_HANDLE) vkFreeMemory(m_Device, m_NearMemory, nullptr);
    if (m_FarMemory != VK_NULL_HANDLE) vkFreeMemory(m_Device, m_FarMemory, nullptr);

    m_CoCFramebuffer = VK_NULL_HANDLE;
    m_NearFramebuffer = VK_NULL_HANDLE;
    m_FarFramebuffer = VK_NULL_HANDLE;
    m_CoCView = VK_NULL_HANDLE;
    m_NearView = VK_NULL_HANDLE;
    m_FarView = VK_NULL_HANDLE;
    m_CoCImage = VK_NULL_HANDLE;
    m_NearImage = VK_NULL_HANDLE;
    m_FarImage = VK_NULL_HANDLE;
    m_CoCMemory = VK_NULL_HANDLE;
    m_NearMemory = VK_NULL_HANDLE;
    m_FarMemory = VK_NULL_HANDLE;

    CreateCoCBuffer();
}

void DepthOfFieldPass::Cleanup() {
    if (m_CoCFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_Device, m_CoCFramebuffer, nullptr);
        m_CoCFramebuffer = VK_NULL_HANDLE;
    }
    if (m_NearFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_Device, m_NearFramebuffer, nullptr);
        m_NearFramebuffer = VK_NULL_HANDLE;
    }
    if (m_FarFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_Device, m_FarFramebuffer, nullptr);
        m_FarFramebuffer = VK_NULL_HANDLE;
    }

    if (m_CoCView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_Device, m_CoCView, nullptr);
        m_CoCView = VK_NULL_HANDLE;
    }
    if (m_NearView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_Device, m_NearView, nullptr);
        m_NearView = VK_NULL_HANDLE;
    }
    if (m_FarView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_Device, m_FarView, nullptr);
        m_FarView = VK_NULL_HANDLE;
    }

    if (m_CoCImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_Device, m_CoCImage, nullptr);
        m_CoCImage = VK_NULL_HANDLE;
    }
    if (m_NearImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_Device, m_NearImage, nullptr);
        m_NearImage = VK_NULL_HANDLE;
    }
    if (m_FarImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_Device, m_FarImage, nullptr);
        m_FarImage = VK_NULL_HANDLE;
    }

    if (m_CoCMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_Device, m_CoCMemory, nullptr);
        m_CoCMemory = VK_NULL_HANDLE;
    }
    if (m_NearMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_Device, m_NearMemory, nullptr);
        m_NearMemory = VK_NULL_HANDLE;
    }
    if (m_FarMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_Device, m_FarMemory, nullptr);
        m_FarMemory = VK_NULL_HANDLE;
    }

    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }

    if (m_DescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
        m_DescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (m_CoCPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_Device, m_CoCPipeline, nullptr);
        m_CoCPipeline = VK_NULL_HANDLE;
    }
    if (m_BlurPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_Device, m_BlurPipeline, nullptr);
        m_BlurPipeline = VK_NULL_HANDLE;
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

bool DepthOfFieldPass::IsEnabled(const ECS::PostProcessSettings& settings) const {
    return settings.dofEnabled && settings.dofMaxBlur > 0.0f;
}

uint32_t DepthOfFieldPass::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type for DoF");
}

} // namespace Renderer
} // namespace Core
