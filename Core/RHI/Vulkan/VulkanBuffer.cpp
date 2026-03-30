#include "VulkanBuffer.h"
#include <stdexcept>
#include <string>

namespace Core {
namespace RHI {

    VulkanBuffer::VulkanBuffer(VmaAllocator allocator, const BufferDescriptor& desc)
        : m_Allocator(allocator), m_Size(desc.size), m_Mapped(desc.mapped) {

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = desc.size;

        switch (desc.usage) {
            case BufferUsage::Vertex:  
                bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT; 
                break;
            case BufferUsage::Index:   
                bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT; 
                break;
            case BufferUsage::Uniform: 
                bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT; 
                break;
            case BufferUsage::Storage: 
                bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT; 
                break;
            case BufferUsage::Staging: 
                bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT; 
                break;
            default:
                throw std::runtime_error("Unknown buffer usage");
        }

        VmaAllocationCreateInfo allocInfo{};
        // If it's a staging buffer or explicitly requested to be mapped, we should ideally use sequential write or auto host visible.
        if (desc.usage == BufferUsage::Staging || desc.usage == BufferUsage::Uniform) {
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            if (desc.mapped) {
                allocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
            }
        } else {
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        }

        VkResult res = vmaCreateBuffer(m_Allocator, &bufferInfo, &allocInfo, &m_Buffer, &m_Allocation, nullptr);
        if (res != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan buffer");
        }

        if (desc.mapped) {
            VmaAllocationInfo allocInfoData;
            vmaGetAllocationInfo(m_Allocator, m_Allocation, &allocInfoData);
            m_MappedData = allocInfoData.pMappedData;
        }
    }

    VulkanBuffer::~VulkanBuffer() {
        if (m_Mapped && !m_MappedData) {
            // Already handled, but let's be safe. If unmap wasn't called manually we don't strictly need to do it if VMA_ALLOCATION_CREATE_MAPPED_BIT was used, 
            // but if we used Map() manually:
            Unmap();
        }
        if (m_Buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_Allocator, m_Buffer, m_Allocation);
            m_Buffer = VK_NULL_HANDLE;
            m_Allocation = VK_NULL_HANDLE;
        }
    }

    void VulkanBuffer::Map(void** outData) {
        if (!m_Mapped) {
            vmaMapMemory(m_Allocator, m_Allocation, &m_MappedData);
            m_Mapped = true;
        }
        if (outData) {
            *outData = m_MappedData;
        }
    }

    void VulkanBuffer::Unmap() {
        if (m_Mapped) {
            vmaUnmapMemory(m_Allocator, m_Allocation);
            m_Mapped = false;
            m_MappedData = nullptr;
        }
    }

} // namespace RHI
} // namespace Core
