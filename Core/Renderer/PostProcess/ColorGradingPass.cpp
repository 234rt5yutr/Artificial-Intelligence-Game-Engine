#include "ColorGradingPass.h"
#include "Core/ECS/Components/PostProcessComponent.h"
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>
#include <cmath>

namespace Core {
namespace Renderer {

ColorGradingPass::~ColorGradingPass() {
    Cleanup();
}

void ColorGradingPass::Initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                   VkRenderPass renderPass, VkExtent2D extent) {
    m_Device = device;
    m_PhysicalDevice = physicalDevice;
    m_RenderPass = renderPass;
    m_Extent = extent;

    CreateDefaultLUT();
    CreateDescriptorSetLayout();
    CreateDescriptorPool();
    AllocateDescriptorSets();
    CreatePipelines();
}

void ColorGradingPass::CreateDefaultLUT() {
    // Create identity LUT (no color change)
    m_LUTSize = 32;
    std::vector<float> lutData(m_LUTSize * m_LUTSize * m_LUTSize * 4);

    for (int b = 0; b < m_LUTSize; ++b) {
        for (int g = 0; g < m_LUTSize; ++g) {
            for (int r = 0; r < m_LUTSize; ++r) {
                int index = (b * m_LUTSize * m_LUTSize + g * m_LUTSize + r) * 4;
                lutData[index + 0] = static_cast<float>(r) / static_cast<float>(m_LUTSize - 1);
                lutData[index + 1] = static_cast<float>(g) / static_cast<float>(m_LUTSize - 1);
                lutData[index + 2] = static_cast<float>(b) / static_cast<float>(m_LUTSize - 1);
                lutData[index + 3] = 1.0f;
            }
        }
    }

    // Create 3D image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_3D;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.extent = {static_cast<uint32_t>(m_LUTSize), 
                        static_cast<uint32_t>(m_LUTSize), 
                        static_cast<uint32_t>(m_LUTSize)};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_Device, &imageInfo, nullptr, &m_LUTImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create LUT 3D image");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_Device, m_LUTImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_LUTMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate LUT memory");
    }

    vkBindImageMemory(m_Device, m_LUTImage, m_LUTMemory, 0);

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_LUTImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_LUTView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create LUT image view");
    }

    // Create LUT sampler with trilinear filtering
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.maxLod = 1.0f;

    if (vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_LUTSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create LUT sampler");
    }

    // Note: Actual data upload requires staging buffer and command buffer
    m_HasLUT = true;
}

bool ColorGradingPass::LoadLUT(const std::string& path) {
    // Parse .cube file format
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::vector<float> lutData;
    int lutSize = 0;
    std::string line;

    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;

        // Parse LUT size
        if (line.find("LUT_3D_SIZE") != std::string::npos) {
            std::istringstream iss(line);
            std::string token;
            iss >> token >> lutSize;
            lutData.reserve(lutSize * lutSize * lutSize * 4);
            continue;
        }

        // Skip other metadata
        if (line.find("TITLE") != std::string::npos ||
            line.find("DOMAIN") != std::string::npos) {
            continue;
        }

        // Parse color data
        std::istringstream iss(line);
        float r, g, b;
        if (iss >> r >> g >> b) {
            lutData.push_back(r);
            lutData.push_back(g);
            lutData.push_back(b);
            lutData.push_back(1.0f);
        }
    }

    if (lutSize == 0 || lutData.empty()) {
        return false;
    }

    // Cleanup existing LUT
    if (m_LUTView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_Device, m_LUTView, nullptr);
    }
    if (m_LUTImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_Device, m_LUTImage, nullptr);
    }
    if (m_LUTMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_Device, m_LUTMemory, nullptr);
    }

    m_LUTSize = lutSize;

    // Create new 3D image with loaded data
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_3D;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.extent = {static_cast<uint32_t>(m_LUTSize),
                        static_cast<uint32_t>(m_LUTSize),
                        static_cast<uint32_t>(m_LUTSize)};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_Device, &imageInfo, nullptr, &m_LUTImage) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_Device, m_LUTImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_LUTMemory) != VK_SUCCESS) {
        return false;
    }

    vkBindImageMemory(m_Device, m_LUTImage, m_LUTMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_LUTImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_LUTView) != VK_SUCCESS) {
        return false;
    }

    // Note: Actual data upload would need staging buffer
    m_HasLUT = true;
    return true;
}

void ColorGradingPass::SetDefaultLUT() {
    // Reset to identity LUT
    if (m_LUTView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_Device, m_LUTView, nullptr);
        m_LUTView = VK_NULL_HANDLE;
    }
    if (m_LUTImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_Device, m_LUTImage, nullptr);
        m_LUTImage = VK_NULL_HANDLE;
    }
    if (m_LUTMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_Device, m_LUTMemory, nullptr);
        m_LUTMemory = VK_NULL_HANDLE;
    }

    CreateDefaultLUT();
    UpdateDescriptorSets();
}

void ColorGradingPass::CreateDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};

    // Scene color
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // 3D LUT
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create color grading descriptor set layout");
    }
}

void ColorGradingPass::CreateDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 4;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 2;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    if (vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create color grading descriptor pool");
    }
}

void ColorGradingPass::AllocateDescriptorSets() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_DescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_DescriptorSetLayout;

    if (vkAllocateDescriptorSets(m_Device, &allocInfo, &m_DescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate color grading descriptor set");
    }
}

void ColorGradingPass::UpdateDescriptorSets() {
    // Update descriptor set with LUT sampler
    if (m_LUTView != VK_NULL_HANDLE && m_LUTSampler != VK_NULL_HANDLE) {
        VkDescriptorImageInfo lutInfo{};
        lutInfo.sampler = m_LUTSampler;
        lutInfo.imageView = m_LUTView;
        lutInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_DescriptorSet;
        write.dstBinding = 1;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &lutInfo;

        vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
    }
}

void ColorGradingPass::CreatePipelines() {
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(ColorGradingPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_DescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create color grading pipeline layout");
    }

    // Note: Actual pipeline creation requires shader modules
}

void ColorGradingPass::Execute(VkCommandBuffer cmd, FramebufferChain& chain,
                                const ECS::PostProcessSettings& settings) {
    if (!IsEnabled(settings)) return;

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

    if (m_ColorGradingPipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ColorGradingPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout,
                                0, 1, &m_DescriptorSet, 0, nullptr);

        ColorGradingPushConstants pushConstants{};
        pushConstants.exposure = settings.exposure;
        pushConstants.contrast = settings.contrast;
        pushConstants.saturation = settings.saturation;
        pushConstants.temperature = settings.temperature;
        pushConstants.tint = settings.tint;
        pushConstants.gamma = 1.0f;
        pushConstants.lift = 0.0f;
        pushConstants.gain = 1.0f;
        pushConstants.tonemapMode = settings.tonemapOperator;
        pushConstants.useLUT = m_HasLUT && !settings.lutTexturePath.empty() ? 1 : 0;
        pushConstants.lutSize = static_cast<float>(m_LUTSize);
        pushConstants.vignetteIntensity = settings.vignetteEnabled ? settings.vignetteIntensity : 0.0f;
        pushConstants.vignetteSmoothness = settings.vignetteSmoothness;

        vkCmdPushConstants(cmd, m_PipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(ColorGradingPushConstants), &pushConstants);

        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
}

void ColorGradingPass::Resize(VkExtent2D newExtent) {
    m_Extent = newExtent;
    // LUT doesn't need to resize - it's independent of screen resolution
}

void ColorGradingPass::Cleanup() {
    if (m_LUTSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_Device, m_LUTSampler, nullptr);
        m_LUTSampler = VK_NULL_HANDLE;
    }

    if (m_LUTView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_Device, m_LUTView, nullptr);
        m_LUTView = VK_NULL_HANDLE;
    }

    if (m_LUTImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_Device, m_LUTImage, nullptr);
        m_LUTImage = VK_NULL_HANDLE;
    }

    if (m_LUTMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_Device, m_LUTMemory, nullptr);
        m_LUTMemory = VK_NULL_HANDLE;
    }

    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }

    if (m_DescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
        m_DescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (m_ColorGradingPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_Device, m_ColorGradingPipeline, nullptr);
        m_ColorGradingPipeline = VK_NULL_HANDLE;
    }

    if (m_PipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
        m_PipelineLayout = VK_NULL_HANDLE;
    }
}

bool ColorGradingPass::IsEnabled(const ECS::PostProcessSettings& settings) const {
    return settings.colorGradingEnabled;
}

uint32_t ColorGradingPass::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type for color grading");
}

} // namespace Renderer
} // namespace Core
