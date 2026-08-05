#include "Core/RHI/Vulkan/VulkanDevice.h"
#include "Core/RHI/Vulkan/VulkanBuffer.h"
#include "Core/RHI/Vulkan/VulkanTexture.h"
#include "Core/Log.h"
#include "Core/Profile.h"

#include <algorithm>

namespace Core {
namespace RHI {

    namespace {

        VkFilter ToVkFilter(FilterMode mode) {
            return mode == FilterMode::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        }

        VkSamplerMipmapMode ToVkMipmapMode(FilterMode mode) {
            return mode == FilterMode::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST
                                               : VK_SAMPLER_MIPMAP_MODE_LINEAR;
        }

        VkSamplerAddressMode ToVkAddressMode(AddressMode mode) {
            switch (mode) {
                case AddressMode::Repeat:         return VK_SAMPLER_ADDRESS_MODE_REPEAT;
                case AddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                case AddressMode::ClampToEdge:    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                case AddressMode::ClampToBorder:  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            }
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }

        // Owns the VkSampler handle so callers get RAII through the RHI type.
        class VulkanSampler : public RHISampler {
        public:
            VulkanSampler(VkDevice device, VkSampler sampler)
                : m_Device(device), m_Sampler(sampler) {}

            ~VulkanSampler() override {
                if (m_Sampler != VK_NULL_HANDLE && m_Device != VK_NULL_HANDLE) {
                    vkDestroySampler(m_Device, m_Sampler, nullptr);
                }
            }

            VulkanSampler(const VulkanSampler&) = delete;
            VulkanSampler& operator=(const VulkanSampler&) = delete;

            VkSampler GetSampler() const { return m_Sampler; }

        private:
            VkDevice m_Device = VK_NULL_HANDLE;
            VkSampler m_Sampler = VK_NULL_HANDLE;
        };

    } // namespace

    VulkanDevice::VulkanDevice(VulkanContext* context)
        : m_Context(context) {
        ENGINE_CORE_INFO("VulkanDevice created (RHI resource creation is now available)");
    }

    VulkanDevice::~VulkanDevice() = default;

    std::shared_ptr<RHIBuffer> VulkanDevice::CreateBuffer(const BufferDescriptor& desc) {
        PROFILE_FUNCTION();

        if (!m_Context || m_Context->GetAllocator() == VK_NULL_HANDLE) {
            ENGINE_CORE_ERROR("VulkanDevice::CreateBuffer called without an initialized context");
            return nullptr;
        }
        if (desc.size == 0) {
            ENGINE_CORE_ERROR("VulkanDevice::CreateBuffer called with size 0");
            return nullptr;
        }

        // VulkanBuffer reports allocation failure by throwing. This function is
        // documented to return null instead, and callers check for null - so a
        // device out of memory must not take the process down.
        try {
            return std::make_shared<VulkanBuffer>(m_Context->GetAllocator(), desc);
        } catch (const std::exception& error) {
            ENGINE_CORE_ERROR("VulkanDevice::CreateBuffer failed for {} bytes: {}",
                              desc.size, error.what());
            return nullptr;
        }
    }

    std::shared_ptr<RHITexture> VulkanDevice::CreateTexture(const TextureDescriptor& desc) {
        PROFILE_FUNCTION();

        if (!m_Context || m_Context->GetAllocator() == VK_NULL_HANDLE) {
            ENGINE_CORE_ERROR("VulkanDevice::CreateTexture called without an initialized context");
            return nullptr;
        }
        if (desc.width == 0 || desc.height == 0) {
            ENGINE_CORE_ERROR("VulkanDevice::CreateTexture called with a zero dimension");
            return nullptr;
        }

        return std::make_shared<VulkanTexture>(m_Context->GetAllocator(), desc);
    }

    std::shared_ptr<RHISampler> VulkanDevice::CreateSampler(const SamplerDescriptor& desc) {
        PROFILE_FUNCTION();

        if (!m_Context || m_Context->GetDevice() == VK_NULL_HANDLE) {
            ENGINE_CORE_ERROR("VulkanDevice::CreateSampler called without an initialized context");
            return nullptr;
        }

        VkSamplerCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.minFilter = ToVkFilter(desc.minFilter);
        info.magFilter = ToVkFilter(desc.magFilter);
        info.mipmapMode = ToVkMipmapMode(desc.mipmapMode);
        info.addressModeU = ToVkAddressMode(desc.addressModeU);
        info.addressModeV = ToVkAddressMode(desc.addressModeV);
        info.addressModeW = ToVkAddressMode(desc.addressModeW);
        info.mipLodBias = desc.mipLodBias;
        // Anisotropy above 1 requires the feature to be enabled on the device;
        // requesting it unconditionally is a validation error.
        info.anisotropyEnable = desc.maxAnisotropy > 1.0f ? VK_TRUE : VK_FALSE;
        info.maxAnisotropy = std::max(1.0f, desc.maxAnisotropy);
        info.minLod = desc.minLod;
        info.maxLod = desc.maxLod;
        info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        info.unnormalizedCoordinates = VK_FALSE;
        info.compareEnable = VK_FALSE;
        info.compareOp = VK_COMPARE_OP_ALWAYS;

        VkSampler sampler = VK_NULL_HANDLE;
        if (vkCreateSampler(m_Context->GetDevice(), &info, nullptr, &sampler) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("VulkanDevice::CreateSampler failed");
            return nullptr;
        }

        return std::make_shared<VulkanSampler>(m_Context->GetDevice(), sampler);
    }

    void VulkanDevice::WaitIdle() {
        if (m_Context && m_Context->GetDevice() != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_Context->GetDevice());
        }
    }

    void VulkanDevice::WarnUnimplemented(const char* what) {
        std::lock_guard lock(m_WarnMutex);
        if (std::find(m_WarnedAbout.begin(), m_WarnedAbout.end(), what) != m_WarnedAbout.end()) {
            return;
        }
        m_WarnedAbout.push_back(what);
        ENGINE_CORE_WARN("VulkanDevice::{} is not implemented: the engine still submits "
                         "through VulkanContext directly rather than through the RHI.", what);
    }

    std::shared_ptr<RHICommandList> VulkanDevice::CreateCommandList() {
        WarnUnimplemented("CreateCommandList");
        return nullptr;
    }

    std::shared_ptr<RHIPipelineState> VulkanDevice::CreateGraphicsPipelineState(
        const GraphicsPipelineDescriptor&) {
        WarnUnimplemented("CreateGraphicsPipelineState");
        return nullptr;
    }

    std::shared_ptr<RHIRenderPass> VulkanDevice::CreateRenderPass(const RenderPassDescriptor&) {
        WarnUnimplemented("CreateRenderPass");
        return nullptr;
    }

    void VulkanDevice::SubmitCommandList(std::shared_ptr<RHICommandList>) {
        WarnUnimplemented("SubmitCommandList");
    }

    void VulkanDevice::ExecuteRenderPass(std::shared_ptr<RHIRenderPass>) {
        WarnUnimplemented("ExecuteRenderPass");
    }

} // namespace RHI
} // namespace Core
