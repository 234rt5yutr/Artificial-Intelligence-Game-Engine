#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <cstdint>

namespace Core {
namespace Renderer {

    struct FramebufferAttachment {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkExtent2D extent{};
    };

    class FramebufferChain {
    public:
        FramebufferChain() = default;
        ~FramebufferChain();

        // Non-copyable
        FramebufferChain(const FramebufferChain&) = delete;
        FramebufferChain& operator=(const FramebufferChain&) = delete;

        // Move semantics
        FramebufferChain(FramebufferChain&& other) noexcept;
        FramebufferChain& operator=(FramebufferChain&& other) noexcept;

        void Initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                       VkExtent2D extent, VkFormat format);
        void Resize(VkExtent2D newExtent);
        void Cleanup();

        // Ping-pong management
        void Swap();
        void ResetIndex() { m_CurrentIndex = 0; }

        VkImageView GetCurrentInputView() const { return m_Attachments[m_CurrentIndex].view; }
        VkImageView GetCurrentOutputView() const { return m_Attachments[1 - m_CurrentIndex].view; }
        VkImage GetCurrentInputImage() const { return m_Attachments[m_CurrentIndex].image; }
        VkImage GetCurrentOutputImage() const { return m_Attachments[1 - m_CurrentIndex].image; }
        VkFramebuffer GetCurrentFramebuffer() const { return m_Framebuffers[1 - m_CurrentIndex]; }

        VkExtent2D GetExtent() const { return m_Extent; }
        VkFormat GetFormat() const { return m_Format; }

        // For external input (scene HDR buffer)
        void SetExternalInput(VkImageView externalView) { m_ExternalInput = externalView; }
        VkImageView GetExternalInput() const { return m_ExternalInput; }

        // Downsample chain for bloom
        void InitializeDownsampleChain(int mipLevels);
        const std::vector<FramebufferAttachment>& GetDownsampleChain() const { return m_DownsampleChain; }
        std::vector<FramebufferAttachment>& GetDownsampleChain() { return m_DownsampleChain; }

        void CreateFramebuffers(VkRenderPass renderPass);

        // Access underlying attachments
        const std::array<FramebufferAttachment, 2>& GetAttachments() const { return m_Attachments; }
        bool IsInitialized() const { return m_Device != VK_NULL_HANDLE; }

    private:
        void CreateAttachment(FramebufferAttachment& attachment, VkExtent2D extent, VkFormat format);
        void DestroyAttachment(FramebufferAttachment& attachment);
        uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

        VkDevice m_Device = VK_NULL_HANDLE;
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkExtent2D m_Extent{};
        VkFormat m_Format = VK_FORMAT_R16G16B16A16_SFLOAT;

        std::array<FramebufferAttachment, 2> m_Attachments{};
        std::array<VkFramebuffer, 2> m_Framebuffers{ VK_NULL_HANDLE, VK_NULL_HANDLE };
        int m_CurrentIndex = 0;

        VkImageView m_ExternalInput = VK_NULL_HANDLE;

        // Downsample chain for bloom
        std::vector<FramebufferAttachment> m_DownsampleChain;
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
    };

} // namespace Renderer
} // namespace Core
