#pragma once

#include "Core/RHI/RHITexture.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace Core {
namespace RHI {

    class VulkanTexture : public RHITexture {
    public:
        VulkanTexture(VmaAllocator allocator, const TextureDescriptor& desc);
        virtual ~VulkanTexture();

        TextureDimension GetDimension() const override { return m_Descriptor.dimension; }
        uint32_t GetWidth() const override { return m_Descriptor.width; }
        uint32_t GetHeight() const override { return m_Descriptor.height; }
        uint32_t GetDepth() const override { return m_Descriptor.depth; }
        uint32_t GetMipLevels() const override { return m_Descriptor.mipLevels; }
        uint32_t GetArrayLayers() const override { return m_Descriptor.arrayLayers; }
        TextureFormat GetFormat() const override { return m_Descriptor.format; }
        TextureUsage GetUsage() const override { return m_Descriptor.usage; }

        VkImage GetImage() const { return m_Image; }

    private:
        VmaAllocator m_Allocator;
        TextureDescriptor m_Descriptor;
        VkImage m_Image = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = VK_NULL_HANDLE;
    };

} // namespace RHI
} // namespace Core