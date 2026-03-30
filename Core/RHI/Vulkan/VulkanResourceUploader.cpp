#include "VulkanResourceUploader.h"
#include "VulkanBuffer.h"
#include "VulkanTexture.h"
#include "Core/Assert.h"
#include <cstring>

namespace Core {
namespace RHI {

    VulkanResourceUploader::VulkanResourceUploader(VulkanContext* context, std::size_t stagingBufferSize)
        : m_Context(context), m_StagingBufferSize(stagingBufferSize) {
        
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = stagingBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        if (vmaCreateBuffer(context->GetAllocator(), &bufferInfo, &allocInfo, &m_StagingBuffer, &m_StagingAllocation, nullptr) != VK_SUCCESS) {
            ENGINE_CORE_ASSERT(false, "Failed to create Staging Buffer!");
        }

        VmaAllocationInfo allocResultInfo;
        vmaGetAllocationInfo(context->GetAllocator(), m_StagingAllocation, &allocResultInfo);
        m_StagingMappedData = allocResultInfo.pMappedData;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (vkCreateFence(context->GetDevice(), &fenceInfo, nullptr, &m_UploadFence) != VK_SUCCESS) {
            ENGINE_CORE_ASSERT(false, "Failed to create Upload Fence!");
        }
    }

    VulkanResourceUploader::~VulkanResourceUploader() {
        if (m_UploadFence) {
            vkDestroyFence(m_Context->GetDevice(), m_UploadFence, nullptr);
        }
        if (m_StagingBuffer) {
            vmaDestroyBuffer(m_Context->GetAllocator(), m_StagingBuffer, m_StagingAllocation);
        }
        if (m_TransferCommandBuffer && m_Context->GetTransferCommandPool()) {
            vkFreeCommandBuffers(m_Context->GetDevice(), m_Context->GetTransferCommandPool(), 1, &m_TransferCommandBuffer);
        }
    }

    void VulkanResourceUploader::Begin() {
        ENGINE_CORE_ASSERT(!m_IsRecording, "Resource uploader is already recording!");
        m_IsRecording = true;
        m_CurrentOffset = 0;
        m_BufferCopies.clear();
        m_TextureCopies.clear();

        if (m_TransferCommandBuffer == VK_NULL_HANDLE) {
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            // For now, just borrow the main command pool if the transfer one wasn't created properly
            allocInfo.commandPool = m_Context->GetTransferCommandPool() ? m_Context->GetTransferCommandPool() : m_Context->GetCommandPool();
            allocInfo.commandBufferCount = 1;

            if (vkAllocateCommandBuffers(m_Context->GetDevice(), &allocInfo, &m_TransferCommandBuffer) != VK_SUCCESS) {
                ENGINE_CORE_ASSERT(false, "Failed to allocate transfer command buffer");
            }
        } else {
            vkResetCommandBuffer(m_TransferCommandBuffer, 0);
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(m_TransferCommandBuffer, &beginInfo);
    }

    void VulkanResourceUploader::UploadBufferData(std::shared_ptr<RHIBuffer> buffer, const void* data, std::size_t size, std::size_t offset) {
        ENGINE_CORE_ASSERT(m_IsRecording, "Resource uploader must be in Begin() state");

        if (m_CurrentOffset + size > m_StagingBufferSize) {
            FlushStagingBuffer();
            ENGINE_CORE_ASSERT(size <= m_StagingBufferSize, "Resource size exceeds staging buffer size");
        }

        void* destPtr = static_cast<char*>(m_StagingMappedData) + m_CurrentOffset;
        std::memcpy(destPtr, data, size);

        m_BufferCopies.push_back({buffer, offset, size, m_CurrentOffset});
        m_CurrentOffset += size;

        // Ensure alignment for subsequent copies (e.g., 256 bytes)
        std::size_t alignment = 256;
        m_CurrentOffset = (m_CurrentOffset + alignment - 1) & ~(alignment - 1);
    }

    void VulkanResourceUploader::UploadTextureData(std::shared_ptr<RHITexture> texture, const void* data, std::size_t size) {
         ENGINE_CORE_ASSERT(m_IsRecording, "Resource uploader must be in Begin() state");

         if (m_CurrentOffset + size > m_StagingBufferSize) {
             FlushStagingBuffer();
             ENGINE_CORE_ASSERT(size <= m_StagingBufferSize, "Resource size exceeds staging buffer size");
         }

         void* destPtr = static_cast<char*>(m_StagingMappedData) + m_CurrentOffset;
         std::memcpy(destPtr, data, size);

         m_TextureCopies.push_back({texture, m_CurrentOffset, size});
         m_CurrentOffset += size;

         std::size_t alignment = 256;
         m_CurrentOffset = (m_CurrentOffset + alignment - 1) & ~(alignment - 1);
    }

    void VulkanResourceUploader::FlushStagingBuffer() {
        if (m_BufferCopies.empty() && m_TextureCopies.empty()) return;

        // Record all copies
        for (const auto& copy : m_BufferCopies) {
            auto vkBuffer = std::static_pointer_cast<VulkanBuffer>(copy.destBuffer);
            VkBufferCopy copyRegion{};
            copyRegion.srcOffset = copy.stagingOffset;
            copyRegion.dstOffset = copy.destOffset;
            copyRegion.size = copy.size;
            vkCmdCopyBuffer(m_TransferCommandBuffer, m_StagingBuffer, vkBuffer->GetBuffer(), 1, &copyRegion);
        }

        for (const auto& copy : m_TextureCopies) {
            auto vkTexture = std::static_pointer_cast<VulkanTexture>(copy.destTexture);

            // Barrier to transfer destination optimal
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = vkTexture->GetImage();
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = vkTexture->GetMipLevels();
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = vkTexture->GetArrayLayers();
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            vkCmdPipelineBarrier(m_TransferCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);

            VkBufferImageCopy region{};
            region.bufferOffset = copy.stagingOffset;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {0, 0, 0};
            region.imageExtent = {vkTexture->GetWidth(), vkTexture->GetHeight(), vkTexture->GetDepth()};

            vkCmdCopyBufferToImage(m_TransferCommandBuffer, m_StagingBuffer, vkTexture->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            // Barrier to shader read optimal
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(m_TransferCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        vkEndCommandBuffer(m_TransferCommandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_TransferCommandBuffer;

        vkResetFences(m_Context->GetDevice(), 1, &m_UploadFence);
        VkQueue uploadQueue = m_Context->GetTransferQueue() ? m_Context->GetTransferQueue() : m_Context->GetGraphicsQueue();
        if (vkQueueSubmit(uploadQueue, 1, &submitInfo, m_UploadFence) != VK_SUCCESS) {
            ENGINE_CORE_ASSERT(false, "Failed to submit transfer command buffer");
        }

        vkWaitForFences(m_Context->GetDevice(), 1, &m_UploadFence, VK_TRUE, UINT64_MAX);

        // Reset state for continued recording if needed
        m_BufferCopies.clear();
        m_TextureCopies.clear();
        m_CurrentOffset = 0;

        // Restart recording
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(m_TransferCommandBuffer, &beginInfo);
    }

    bool VulkanResourceUploader::EndAndSubmit() {
        ENGINE_CORE_ASSERT(m_IsRecording, "Resource uploader is not recording!");
        
        if (!m_BufferCopies.empty() || !m_TextureCopies.empty()) {
            FlushStagingBuffer();
        } else {
            // Nothing to do, just end
            vkEndCommandBuffer(m_TransferCommandBuffer);
        }

        m_IsRecording = false;
        return true;
    }

} // namespace RHI
} // namespace Core