#include "SSAOPass.h"
#include "Core/ECS/Components/PostProcessComponent.h"
#include <stdexcept>
#include <random>
#include <cmath>
#include <cstring>

namespace Core {
namespace Renderer {

SSAOPass::~SSAOPass() {
    Cleanup();
}

void SSAOPass::Initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                          VkRenderPass renderPass, VkExtent2D extent) {
    m_Device = device;
    m_PhysicalDevice = physicalDevice;
    m_RenderPass = renderPass;
    m_Extent = extent;

    CreateKernelBuffer();
    CreateNoiseTexture();
    CreateAOBuffer();
    CreateDescriptorSetLayout();
    CreateDescriptorPool();
    AllocateDescriptorSets();
    CreatePipelines();
}

void SSAOPass::CreateKernelBuffer() {
    // Generate hemisphere sample kernel
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> randomFloat(0.0f, 1.0f);

    for (int i = 0; i < MAX_KERNEL_SIZE; ++i) {
        // Random point in hemisphere
        float x = randomFloat(gen) * 2.0f - 1.0f;
        float y = randomFloat(gen) * 2.0f - 1.0f;
        float z = randomFloat(gen);  // Hemisphere (z >= 0)

        // Normalize
        float len = std::sqrt(x * x + y * y + z * z);
        x /= len;
        y /= len;
        z /= len;

        // Scale to distribute samples closer to origin
        float scale = static_cast<float>(i) / static_cast<float>(MAX_KERNEL_SIZE);
        scale = 0.1f + scale * scale * 0.9f;  // Lerp between 0.1 and 1.0

        m_Kernel[i * 4 + 0] = x * scale;
        m_Kernel[i * 4 + 1] = y * scale;
        m_Kernel[i * 4 + 2] = z * scale;
        m_Kernel[i * 4 + 3] = 0.0f;  // Padding
    }

    // Create buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(m_Kernel);
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_Device, &bufferInfo, nullptr, &m_KernelBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSAO kernel buffer");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_Device, m_KernelBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_KernelMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate SSAO kernel memory");
    }

    vkBindBufferMemory(m_Device, m_KernelBuffer, m_KernelMemory, 0);

    // Copy kernel data
    void* data;
    vkMapMemory(m_Device, m_KernelMemory, 0, sizeof(m_Kernel), 0, &data);
    std::memcpy(data, m_Kernel.data(), sizeof(m_Kernel));
    vkUnmapMemory(m_Device, m_KernelMemory);
}

void SSAOPass::CreateNoiseTexture() {
    // 4x4 noise texture with random rotation vectors
    const int noiseSize = 4;
    std::vector<float> noiseData(noiseSize * noiseSize * 4);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> randomFloat(0.0f, 1.0f);

    for (int i = 0; i < noiseSize * noiseSize; ++i) {
        // Random rotation vector in tangent plane (z = 0)
        float x = randomFloat(gen) * 2.0f - 1.0f;
        float y = randomFloat(gen) * 2.0f - 1.0f;
        float len = std::sqrt(x * x + y * y);
        
        noiseData[i * 4 + 0] = x / len;
        noiseData[i * 4 + 1] = y / len;
        noiseData[i * 4 + 2] = 0.0f;
        noiseData[i * 4 + 3] = 0.0f;
    }

    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    imageInfo.extent = {noiseSize, noiseSize, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_Device, &imageInfo, nullptr, &m_NoiseImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSAO noise image");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_Device, m_NoiseImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_NoiseMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate SSAO noise memory");
    }

    vkBindImageMemory(m_Device, m_NoiseImage, m_NoiseMemory, 0);

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_NoiseImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_NoiseView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSAO noise image view");
    }

    // Note: Actual data upload requires staging buffer and command buffer
    // This would be handled by the engine's texture loading system
}

void SSAOPass::CreateAOBuffer() {
    // Create AO result image (half resolution for performance)
    VkExtent2D aoExtent = {m_Extent.width / 2, m_Extent.height / 2};

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8_UNORM;  // Single channel for AO
    imageInfo.extent = {aoExtent.width, aoExtent.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_Device, &imageInfo, nullptr, &m_AOImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSAO result image");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_Device, m_AOImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_AOMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate SSAO result memory");
    }

    vkBindImageMemory(m_Device, m_AOImage, m_AOMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_AOImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_AOView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSAO result image view");
    }

    // Create blur intermediate buffer (same format)
    if (vkCreateImage(m_Device, &imageInfo, nullptr, &m_BlurImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSAO blur image");
    }

    vkGetImageMemoryRequirements(m_Device, m_BlurImage, &memRequirements);
    allocInfo.allocationSize = memRequirements.size;

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_BlurMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate SSAO blur memory");
    }

    vkBindImageMemory(m_Device, m_BlurImage, m_BlurMemory, 0);

    viewInfo.image = m_BlurImage;
    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_BlurView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSAO blur image view");
    }
}

void SSAOPass::CreateDescriptorSetLayout() {
    // SSAO main pass: depth, normal, noise, kernel
    std::array<VkDescriptorSetLayoutBinding, 4> ssaoBindings{};
    
    ssaoBindings[0].binding = 0;
    ssaoBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssaoBindings[0].descriptorCount = 1;
    ssaoBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    ssaoBindings[1].binding = 1;
    ssaoBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssaoBindings[1].descriptorCount = 1;
    ssaoBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    ssaoBindings[2].binding = 2;
    ssaoBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssaoBindings[2].descriptorCount = 1;
    ssaoBindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    ssaoBindings[3].binding = 3;
    ssaoBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ssaoBindings[3].descriptorCount = 1;
    ssaoBindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(ssaoBindings.size());
    layoutInfo.pBindings = ssaoBindings.data();

    if (vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_SSAODescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSAO descriptor set layout");
    }

    // Blur pass: single input texture
    VkDescriptorSetLayoutBinding blurBinding{};
    blurBinding.binding = 0;
    blurBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    blurBinding.descriptorCount = 1;
    blurBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &blurBinding;

    if (vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_BlurDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSAO blur descriptor set layout");
    }

    // Apply pass: AO texture + scene color
    std::array<VkDescriptorSetLayoutBinding, 2> applyBindings{};
    applyBindings[0].binding = 0;
    applyBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    applyBindings[0].descriptorCount = 1;
    applyBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    applyBindings[1].binding = 1;
    applyBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    applyBindings[1].descriptorCount = 1;
    applyBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    layoutInfo.bindingCount = static_cast<uint32_t>(applyBindings.size());
    layoutInfo.pBindings = applyBindings.data();

    if (vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_ApplyDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSAO apply descriptor set layout");
    }
}

void SSAOPass::CreateDescriptorPool() {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 10;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = 2;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 5;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    if (vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSAO descriptor pool");
    }
}

void SSAOPass::AllocateDescriptorSets() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_DescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_SSAODescriptorSetLayout;

    if (vkAllocateDescriptorSets(m_Device, &allocInfo, &m_SSAODescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate SSAO descriptor set");
    }

    allocInfo.pSetLayouts = &m_BlurDescriptorSetLayout;
    if (vkAllocateDescriptorSets(m_Device, &allocInfo, &m_BlurHDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate SSAO blur H descriptor set");
    }

    if (vkAllocateDescriptorSets(m_Device, &allocInfo, &m_BlurVDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate SSAO blur V descriptor set");
    }

    allocInfo.pSetLayouts = &m_ApplyDescriptorSetLayout;
    if (vkAllocateDescriptorSets(m_Device, &allocInfo, &m_ApplyDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate SSAO apply descriptor set");
    }
}

void SSAOPass::CreatePipelines() {
    // SSAO pipeline layout
    VkPushConstantRange ssaoPushConstantRange{};
    ssaoPushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ssaoPushConstantRange.offset = 0;
    ssaoPushConstantRange.size = sizeof(SSAOPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_SSAODescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &ssaoPushConstantRange;

    if (vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_SSAOPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSAO pipeline layout");
    }

    // Blur pipeline layout
    VkPushConstantRange blurPushConstantRange{};
    blurPushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    blurPushConstantRange.offset = 0;
    blurPushConstantRange.size = sizeof(BlurPushConstants);

    pipelineLayoutInfo.pSetLayouts = &m_BlurDescriptorSetLayout;
    pipelineLayoutInfo.pPushConstantRanges = &blurPushConstantRange;

    if (vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_BlurPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSAO blur pipeline layout");
    }

    // Apply pipeline layout
    pipelineLayoutInfo.pSetLayouts = &m_ApplyDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;

    if (vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_ApplyPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSAO apply pipeline layout");
    }

    // Note: Actual pipeline creation requires shader modules
}

void SSAOPass::SetSampler(VkSampler linearSampler, VkSampler nearestSampler) {
    m_LinearSampler = linearSampler;
    m_NearestSampler = nearestSampler;
}

void SSAOPass::Execute(VkCommandBuffer cmd, FramebufferChain& chain,
                       const ECS::PostProcessSettings& settings) {
    if (!IsEnabled(settings)) return;

    SSAOSettings ssaoSettings;
    ssaoSettings.radius = settings.ssaoRadius;
    ssaoSettings.bias = settings.ssaoBias;
    ssaoSettings.intensity = settings.ssaoIntensity;
    ssaoSettings.kernelSize = settings.ssaoKernelSize;
    ssaoSettings.blurPasses = settings.ssaoBlurPasses;

    // 1. Calculate AO
    SSAOMainPass(cmd, ssaoSettings);

    // 2. Bilateral blur (horizontal + vertical)
    for (int i = 0; i < ssaoSettings.blurPasses; ++i) {
        BlurPass(cmd, true);   // Horizontal
        BlurPass(cmd, false);  // Vertical
    }

    // 3. Apply AO to scene
    ApplyPass(cmd, chain);
}

void SSAOPass::SSAOMainPass(VkCommandBuffer cmd, const SSAOSettings& settings) {
    TransitionImageLayout(cmd, m_AOImage,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_RenderPass;
    renderPassInfo.framebuffer = m_AOFramebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {m_Extent.width / 2, m_Extent.height / 2};

    VkClearValue clearValue{};
    clearValue.color = {{1.0f, 1.0f, 1.0f, 1.0f}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    if (m_SSAOPipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SSAOPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SSAOPipelineLayout,
                                0, 1, &m_SSAODescriptorSet, 0, nullptr);

        SSAOPushConstants pushConstants{};
        // Projection matrix would be set from camera
        pushConstants.radius = settings.radius;
        pushConstants.bias = settings.bias;
        pushConstants.intensity = settings.intensity;
        pushConstants.kernelSize = settings.kernelSize;
        pushConstants.screenSize[0] = static_cast<float>(m_Extent.width);
        pushConstants.screenSize[1] = static_cast<float>(m_Extent.height);
        pushConstants.nearPlane = 0.1f;  // Would come from camera
        pushConstants.farPlane = 1000.0f;

        vkCmdPushConstants(cmd, m_SSAOPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(SSAOPushConstants), &pushConstants);

        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
}

void SSAOPass::BlurPass(VkCommandBuffer cmd, bool horizontal) {
    VkImage srcImage = horizontal ? m_AOImage : m_BlurImage;
    VkImage dstImage = horizontal ? m_BlurImage : m_AOImage;
    VkFramebuffer framebuffer = horizontal ? m_BlurFramebuffer : m_AOFramebuffer;
    VkDescriptorSet descriptorSet = horizontal ? m_BlurHDescriptorSet : m_BlurVDescriptorSet;

    TransitionImageLayout(cmd, srcImage,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    TransitionImageLayout(cmd, dstImage,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_RenderPass;
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {m_Extent.width / 2, m_Extent.height / 2};

    VkClearValue clearValue{};
    clearValue.color = {{1.0f, 1.0f, 1.0f, 1.0f}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    if (m_BlurPipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_BlurPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_BlurPipelineLayout,
                                0, 1, &descriptorSet, 0, nullptr);

        BlurPushConstants pushConstants{};
        pushConstants.texelSize[0] = 2.0f / static_cast<float>(m_Extent.width);
        pushConstants.texelSize[1] = 2.0f / static_cast<float>(m_Extent.height);
        pushConstants.horizontal = horizontal ? 1 : 0;
        pushConstants.depthThreshold = 0.1f;

        vkCmdPushConstants(cmd, m_BlurPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(BlurPushConstants), &pushConstants);

        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
}

void SSAOPass::ApplyPass(VkCommandBuffer cmd, FramebufferChain& chain) {
    TransitionImageLayout(cmd, m_AOImage,
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

    if (m_ApplyPipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ApplyPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ApplyPipelineLayout,
                                0, 1, &m_ApplyDescriptorSet, 0, nullptr);

        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
}

void SSAOPass::TransitionImageLayout(VkCommandBuffer cmd, VkImage image,
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

void SSAOPass::Resize(VkExtent2D newExtent) {
    m_Extent = newExtent;

    // Recreate AO buffers
    if (m_AOFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_Device, m_AOFramebuffer, nullptr);
    }
    if (m_BlurFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_Device, m_BlurFramebuffer, nullptr);
    }
    if (m_AOView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_Device, m_AOView, nullptr);
    }
    if (m_BlurView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_Device, m_BlurView, nullptr);
    }
    if (m_AOImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_Device, m_AOImage, nullptr);
    }
    if (m_BlurImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_Device, m_BlurImage, nullptr);
    }
    if (m_AOMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_Device, m_AOMemory, nullptr);
    }
    if (m_BlurMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_Device, m_BlurMemory, nullptr);
    }

    CreateAOBuffer();
}

void SSAOPass::Cleanup() {
    if (m_AOFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_Device, m_AOFramebuffer, nullptr);
        m_AOFramebuffer = VK_NULL_HANDLE;
    }
    if (m_BlurFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_Device, m_BlurFramebuffer, nullptr);
        m_BlurFramebuffer = VK_NULL_HANDLE;
    }

    if (m_AOView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_Device, m_AOView, nullptr);
        m_AOView = VK_NULL_HANDLE;
    }
    if (m_BlurView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_Device, m_BlurView, nullptr);
        m_BlurView = VK_NULL_HANDLE;
    }
    if (m_NoiseView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_Device, m_NoiseView, nullptr);
        m_NoiseView = VK_NULL_HANDLE;
    }

    if (m_AOImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_Device, m_AOImage, nullptr);
        m_AOImage = VK_NULL_HANDLE;
    }
    if (m_BlurImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_Device, m_BlurImage, nullptr);
        m_BlurImage = VK_NULL_HANDLE;
    }
    if (m_NoiseImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_Device, m_NoiseImage, nullptr);
        m_NoiseImage = VK_NULL_HANDLE;
    }

    if (m_AOMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_Device, m_AOMemory, nullptr);
        m_AOMemory = VK_NULL_HANDLE;
    }
    if (m_BlurMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_Device, m_BlurMemory, nullptr);
        m_BlurMemory = VK_NULL_HANDLE;
    }
    if (m_NoiseMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_Device, m_NoiseMemory, nullptr);
        m_NoiseMemory = VK_NULL_HANDLE;
    }

    if (m_KernelBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_Device, m_KernelBuffer, nullptr);
        m_KernelBuffer = VK_NULL_HANDLE;
    }
    if (m_KernelMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_Device, m_KernelMemory, nullptr);
        m_KernelMemory = VK_NULL_HANDLE;
    }

    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }

    if (m_SSAODescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_Device, m_SSAODescriptorSetLayout, nullptr);
        m_SSAODescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (m_BlurDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_Device, m_BlurDescriptorSetLayout, nullptr);
        m_BlurDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (m_ApplyDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_Device, m_ApplyDescriptorSetLayout, nullptr);
        m_ApplyDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (m_SSAOPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_Device, m_SSAOPipeline, nullptr);
        m_SSAOPipeline = VK_NULL_HANDLE;
    }
    if (m_BlurPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_Device, m_BlurPipeline, nullptr);
        m_BlurPipeline = VK_NULL_HANDLE;
    }
    if (m_ApplyPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_Device, m_ApplyPipeline, nullptr);
        m_ApplyPipeline = VK_NULL_HANDLE;
    }

    if (m_SSAOPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_Device, m_SSAOPipelineLayout, nullptr);
        m_SSAOPipelineLayout = VK_NULL_HANDLE;
    }
    if (m_BlurPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_Device, m_BlurPipelineLayout, nullptr);
        m_BlurPipelineLayout = VK_NULL_HANDLE;
    }
    if (m_ApplyPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_Device, m_ApplyPipelineLayout, nullptr);
        m_ApplyPipelineLayout = VK_NULL_HANDLE;
    }
}

bool SSAOPass::IsEnabled(const ECS::PostProcessSettings& settings) const {
    return settings.ssaoEnabled && settings.ssaoIntensity > 0.0f;
}

uint32_t SSAOPass::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type for SSAO");
}

} // namespace Renderer
} // namespace Core
