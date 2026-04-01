#include "FramebufferChain.h"
#include <stdexcept>
#include <algorithm>
#include <utility>

namespace Core {
namespace Renderer {

    FramebufferChain::~FramebufferChain() {
        Cleanup();
    }

    FramebufferChain::FramebufferChain(FramebufferChain&& other) noexcept
        : m_Device(other.m_Device)
        , m_PhysicalDevice(other.m_PhysicalDevice)
        , m_Extent(other.m_Extent)
        , m_Format(other.m_Format)
        , m_Attachments(std::move(other.m_Attachments))
        , m_Framebuffers(std::move(other.m_Framebuffers))
        , m_CurrentIndex(other.m_CurrentIndex)
        , m_ExternalInput(other.m_ExternalInput)
        , m_DownsampleChain(std::move(other.m_DownsampleChain))
        , m_RenderPass(other.m_RenderPass)
    {
        other.m_Device = VK_NULL_HANDLE;
        other.m_PhysicalDevice = VK_NULL_HANDLE;
        other.m_ExternalInput = VK_NULL_HANDLE;
        other.m_RenderPass = VK_NULL_HANDLE;
        other.m_Framebuffers = { VK_NULL_HANDLE, VK_NULL_HANDLE };
        for (auto& att : other.m_Attachments) {
            att = FramebufferAttachment{};
        }
    }

    FramebufferChain& FramebufferChain::operator=(FramebufferChain&& other) noexcept {
        if (this != &other) {
            Cleanup();

            m_Device = other.m_Device;
            m_PhysicalDevice = other.m_PhysicalDevice;
            m_Extent = other.m_Extent;
            m_Format = other.m_Format;
            m_Attachments = std::move(other.m_Attachments);
            m_Framebuffers = std::move(other.m_Framebuffers);
            m_CurrentIndex = other.m_CurrentIndex;
            m_ExternalInput = other.m_ExternalInput;
            m_DownsampleChain = std::move(other.m_DownsampleChain);
            m_RenderPass = other.m_RenderPass;

            other.m_Device = VK_NULL_HANDLE;
            other.m_PhysicalDevice = VK_NULL_HANDLE;
            other.m_ExternalInput = VK_NULL_HANDLE;
            other.m_RenderPass = VK_NULL_HANDLE;
            other.m_Framebuffers = { VK_NULL_HANDLE, VK_NULL_HANDLE };
            for (auto& att : other.m_Attachments) {
                att = FramebufferAttachment{};
            }
        }
        return *this;
    }

    void FramebufferChain::Initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                      VkExtent2D extent, VkFormat format) {
        m_Device = device;
        m_PhysicalDevice = physicalDevice;
        m_Extent = extent;
        m_Format = format;

        for (auto& attachment : m_Attachments) {
            CreateAttachment(attachment, extent, format);
        }
    }

    void FramebufferChain::CreateAttachment(FramebufferAttachment& attachment,
                                            VkExtent2D extent, VkFormat format) {
        attachment.extent = extent;
        attachment.format = format;

        // Create image
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = { extent.width, extent.height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (vkCreateImage(m_Device, &imageInfo, nullptr, &attachment.image) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create framebuffer image");
        }

        // Allocate memory
        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(m_Device, attachment.image, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &attachment.memory) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate framebuffer memory");
        }

        if (vkBindImageMemory(m_Device, attachment.image, attachment.memory, 0) != VK_SUCCESS) {
            throw std::runtime_error("Failed to bind framebuffer image memory");
        }

        // Create image view
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = attachment.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_Device, &viewInfo, nullptr, &attachment.view) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create framebuffer image view");
        }
    }

    void FramebufferChain::DestroyAttachment(FramebufferAttachment& attachment) {
        if (m_Device == VK_NULL_HANDLE) return;

        if (attachment.view != VK_NULL_HANDLE) {
            vkDestroyImageView(m_Device, attachment.view, nullptr);
            attachment.view = VK_NULL_HANDLE;
        }
        if (attachment.image != VK_NULL_HANDLE) {
            vkDestroyImage(m_Device, attachment.image, nullptr);
            attachment.image = VK_NULL_HANDLE;
        }
        if (attachment.memory != VK_NULL_HANDLE) {
            vkFreeMemory(m_Device, attachment.memory, nullptr);
            attachment.memory = VK_NULL_HANDLE;
        }
    }

    void FramebufferChain::InitializeDownsampleChain(int mipLevels) {
        // Clean up existing downsample chain
        for (auto& attachment : m_DownsampleChain) {
            DestroyAttachment(attachment);
        }

        m_DownsampleChain.resize(static_cast<size_t>(mipLevels));

        VkExtent2D mipExtent = m_Extent;
        for (int i = 0; i < mipLevels; ++i) {
            mipExtent.width = std::max(1u, mipExtent.width / 2);
            mipExtent.height = std::max(1u, mipExtent.height / 2);
            CreateAttachment(m_DownsampleChain[static_cast<size_t>(i)], mipExtent, m_Format);
        }
    }

    void FramebufferChain::CreateFramebuffers(VkRenderPass renderPass) {
        m_RenderPass = renderPass;

        for (int i = 0; i < 2; ++i) {
            if (m_Framebuffers[i] != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(m_Device, m_Framebuffers[i], nullptr);
                m_Framebuffers[i] = VK_NULL_HANDLE;
            }

            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass = renderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &m_Attachments[i].view;
            fbInfo.width = m_Extent.width;
            fbInfo.height = m_Extent.height;
            fbInfo.layers = 1;

            if (vkCreateFramebuffer(m_Device, &fbInfo, nullptr, &m_Framebuffers[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create ping-pong framebuffer");
            }
        }
    }

    void FramebufferChain::Resize(VkExtent2D newExtent) {
        if (m_Device == VK_NULL_HANDLE) return;

        // Destroy framebuffers first
        for (auto& fb : m_Framebuffers) {
            if (fb != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(m_Device, fb, nullptr);
                fb = VK_NULL_HANDLE;
            }
        }

        // Destroy attachments
        for (auto& attachment : m_Attachments) {
            DestroyAttachment(attachment);
        }

        m_Extent = newExtent;

        // Recreate attachments
        for (auto& attachment : m_Attachments) {
            CreateAttachment(attachment, newExtent, m_Format);
        }

        // Recreate downsample chain if it existed
        if (!m_DownsampleChain.empty()) {
            int mipLevels = static_cast<int>(m_DownsampleChain.size());
            for (auto& attachment : m_DownsampleChain) {
                DestroyAttachment(attachment);
            }
            m_DownsampleChain.clear();
            InitializeDownsampleChain(mipLevels);
        }

        // Recreate framebuffers if render pass was set
        if (m_RenderPass != VK_NULL_HANDLE) {
            CreateFramebuffers(m_RenderPass);
        }
    }

    void FramebufferChain::Cleanup() {
        if (m_Device == VK_NULL_HANDLE) return;

        for (auto& fb : m_Framebuffers) {
            if (fb != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(m_Device, fb, nullptr);
                fb = VK_NULL_HANDLE;
            }
        }

        for (auto& attachment : m_Attachments) {
            DestroyAttachment(attachment);
        }

        for (auto& attachment : m_DownsampleChain) {
            DestroyAttachment(attachment);
        }
        m_DownsampleChain.clear();

        m_Device = VK_NULL_HANDLE;
        m_RenderPass = VK_NULL_HANDLE;
    }

    void FramebufferChain::Swap() {
        m_CurrentIndex = 1 - m_CurrentIndex;
    }

    uint32_t FramebufferChain::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        throw std::runtime_error("Failed to find suitable memory type");
    }

} // namespace Renderer
} // namespace Core
