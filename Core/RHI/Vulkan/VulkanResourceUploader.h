#pragma once

#include "Core/RHI/RHIResourceUploader.h"
#include "VulkanContext.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <memory>

namespace Core {
namespace RHI {

    class VulkanResourceUploader : public RHIResourceUploader {
    public:
        VulkanResourceUploader(VulkanContext* context, std::size_t stagingBufferSize = 64 * 1024 * 1024); // Default 64MB staging buffer
        ~VulkanResourceUploader() override;

        void Begin() override;
        void UploadBufferData(std::shared_ptr<RHIBuffer> buffer, const void* data, std::size_t size, std::size_t offset = 0) override;
        void UploadTextureData(std::shared_ptr<RHITexture> texture, const void* data, std::size_t size) override;
        bool EndAndSubmit() override;

    private:
        void FlushStagingBuffer();

    private:
        VulkanContext* m_Context;
        
        VkBuffer m_StagingBuffer = VK_NULL_HANDLE;
        VmaAllocation m_StagingAllocation = VK_NULL_HANDLE;
        void* m_StagingMappedData = nullptr;
        std::size_t m_StagingBufferSize = 0;
        std::size_t m_CurrentOffset = 0;

        VkCommandBuffer m_TransferCommandBuffer = VK_NULL_HANDLE;
        VkFence m_UploadFence = VK_NULL_HANDLE;
        bool m_IsRecording = false;

        // Queue of commands to execute when flushed
        struct BufferCopyCmd {
            std::shared_ptr<RHIBuffer> destBuffer;
            std::size_t destOffset;
            std::size_t size;
            std::size_t stagingOffset;
        };
        std::vector<BufferCopyCmd> m_BufferCopies;

        struct TextureCopyCmd {
            std::shared_ptr<RHITexture> destTexture;
            std::size_t stagingOffset;
            std::size_t size;
        };
        std::vector<TextureCopyCmd> m_TextureCopies;
    };

} // namespace RHI
} // namespace Core