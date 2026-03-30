#include "VulkanTexture.h"
#include "Core/Assert.h"

namespace Core {
namespace RHI {

    static VkFormat GetVulkanFormat(TextureFormat format) {
        switch (format) {
            case TextureFormat::RGBA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
            case TextureFormat::R32_FLOAT: return VK_FORMAT_R32_SFLOAT;
            case TextureFormat::D32_SFLOAT: return VK_FORMAT_D32_SFLOAT;
            default: ENGINE_CORE_ASSERT(false, "Unknown texture format!"); return VK_FORMAT_UNDEFINED;
        }
    }

    static VkImageUsageFlags GetVulkanUsage(TextureUsage usage) {
        switch (usage) {
            case TextureUsage::Sampled: return VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            case TextureUsage::Storage: return VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            case TextureUsage::RenderTarget: return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            case TextureUsage::DepthStencil: return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            default: return VK_IMAGE_USAGE_SAMPLED_BIT;
        }
    }

    static VkImageType GetVulkanImageType(TextureDimension dimension) {
        switch (dimension) {
            case TextureDimension::Texture1D: return VK_IMAGE_TYPE_1D;
            case TextureDimension::Texture2D: return VK_IMAGE_TYPE_2D;
            case TextureDimension::Texture3D: return VK_IMAGE_TYPE_3D;
            case TextureDimension::TextureCube: return VK_IMAGE_TYPE_2D;
            default: return VK_IMAGE_TYPE_2D;
        }
    }

    VulkanTexture::VulkanTexture(VmaAllocator allocator, const TextureDescriptor& desc)
        : m_Allocator(allocator), m_Descriptor(desc) {
            
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = GetVulkanImageType(desc.dimension);
        imageInfo.extent.width = desc.width;
        imageInfo.extent.height = desc.height;
        imageInfo.extent.depth = desc.depth;
        imageInfo.mipLevels = desc.mipLevels;
        imageInfo.arrayLayers = desc.arrayLayers;
        imageInfo.format = GetVulkanFormat(desc.format);
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = GetVulkanUsage(desc.usage);
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (desc.dimension == TextureDimension::TextureCube) {
            imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        }

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateImage(m_Allocator, &imageInfo, &allocInfo, &m_Image, &m_Allocation, nullptr) != VK_SUCCESS) {
            ENGINE_CORE_ASSERT(false, "Failed to create Vulkan Image!");
        }
    }

    VulkanTexture::~VulkanTexture() {
        if (m_Image) {
            vmaDestroyImage(m_Allocator, m_Image, m_Allocation);
        }
    }

} // namespace RHI
} // namespace Core