#pragma once

#include "Core/RHI/RHIBuffer.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace Core {
namespace RHI {

    class VulkanBuffer : public RHIBuffer {
    public:
        VulkanBuffer(VmaAllocator allocator, const BufferDescriptor& desc);
        virtual ~VulkanBuffer();

        void Map(void** outData) override;
        void Unmap() override;
        std::size_t GetSize() const override { return m_Size; }

        VkBuffer GetBuffer() const { return m_Buffer; }

    private:
        VmaAllocator m_Allocator;
        VkBuffer m_Buffer = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = VK_NULL_HANDLE;
        std::size_t m_Size = 0;
        bool m_Mapped = false;
        void* m_MappedData = nullptr;
    };

} // namespace RHI
} // namespace Core
