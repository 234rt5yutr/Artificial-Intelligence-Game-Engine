#include "PostProcessManager.h"
#include "Core/ECS/Components/PostProcessComponent.h"
#include <stdexcept>
#include <algorithm>
#include <cstring>

namespace Core {
namespace Renderer {

    PostProcessManager::~PostProcessManager() {
        Cleanup();
    }

    void PostProcessManager::Initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                        VkExtent2D extent) {
        m_Device = device;
        m_PhysicalDevice = physicalDevice;
        m_Extent = extent;

        CreateRenderPass();
        CreateSamplers();

        m_FramebufferChain.Initialize(device, physicalDevice, extent,
                                      VK_FORMAT_R16G16B16A16_SFLOAT);
        m_FramebufferChain.CreateFramebuffers(m_RenderPass);
    }

    void PostProcessManager::CreateRenderPass() {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        // Dependency to ensure proper synchronization
        std::array<VkSubpassDependency, 2> dependencies{};

        // External -> Subpass: wait for previous reads to complete
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        // Subpass -> External: make writes visible for subsequent reads
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfo.pDependencies = dependencies.data();

        if (vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_RenderPass) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create post-process render pass");
        }
    }

    void PostProcessManager::CreateSamplers() {
        // Linear sampler for most effects
        VkSamplerCreateInfo linearInfo{};
        linearInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        linearInfo.magFilter = VK_FILTER_LINEAR;
        linearInfo.minFilter = VK_FILTER_LINEAR;
        linearInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        linearInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        linearInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        linearInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        linearInfo.mipLodBias = 0.0f;
        linearInfo.anisotropyEnable = VK_FALSE;
        linearInfo.maxAnisotropy = 1.0f;
        linearInfo.compareEnable = VK_FALSE;
        linearInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        linearInfo.minLod = 0.0f;
        linearInfo.maxLod = 1.0f;
        linearInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        linearInfo.unnormalizedCoordinates = VK_FALSE;

        if (vkCreateSampler(m_Device, &linearInfo, nullptr, &m_LinearSampler) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create linear sampler");
        }

        // Nearest sampler for pixel-accurate sampling
        VkSamplerCreateInfo nearestInfo = linearInfo;
        nearestInfo.magFilter = VK_FILTER_NEAREST;
        nearestInfo.minFilter = VK_FILTER_NEAREST;
        nearestInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

        if (vkCreateSampler(m_Device, &nearestInfo, nullptr, &m_NearestSampler) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create nearest sampler");
        }
    }

    void PostProcessManager::AddPass(std::unique_ptr<PostProcessPass> pass) {
        if (!pass) return;

        pass->Initialize(m_Device, m_PhysicalDevice, m_RenderPass, m_Extent);
        m_Passes.push_back(std::move(pass));
    }

    void PostProcessManager::RemovePass(const char* name) {
        auto it = std::remove_if(m_Passes.begin(), m_Passes.end(),
            [name](const std::unique_ptr<PostProcessPass>& pass) {
                return std::strcmp(pass->GetName(), name) == 0;
            });

        for (auto removeIt = it; removeIt != m_Passes.end(); ++removeIt) {
            (*removeIt)->Cleanup();
        }

        m_Passes.erase(it, m_Passes.end());
    }

    PostProcessPass* PostProcessManager::GetPass(size_t index) const {
        if (index >= m_Passes.size()) return nullptr;
        return m_Passes[index].get();
    }

    PostProcessPass* PostProcessManager::FindPass(const char* name) const {
        for (const auto& pass : m_Passes) {
            if (std::strcmp(pass->GetName(), name) == 0) {
                return pass.get();
            }
        }
        return nullptr;
    }

    void PostProcessManager::Execute(VkCommandBuffer cmd, const ECS::PostProcessSettings& settings,
                                     VkImageView sceneColorInput, VkImageView sceneDepthInput) {
        (void)sceneDepthInput;  // May be used by SSAO, DoF passes

        m_FramebufferChain.SetExternalInput(sceneColorInput);
        m_FramebufferChain.ResetIndex();

        for (auto& pass : m_Passes) {
            if (pass->IsEnabled(settings)) {
                pass->Execute(cmd, m_FramebufferChain, settings);
                m_FramebufferChain.Swap();
            }
        }
    }

    void PostProcessManager::Resize(VkExtent2D newExtent) {
        m_Extent = newExtent;
        m_FramebufferChain.Resize(newExtent);

        for (auto& pass : m_Passes) {
            pass->Resize(newExtent);
        }
    }

    void PostProcessManager::Cleanup() {
        if (m_Device == VK_NULL_HANDLE) return;

        for (auto& pass : m_Passes) {
            pass->Cleanup();
        }
        m_Passes.clear();

        m_FramebufferChain.Cleanup();

        if (m_LinearSampler != VK_NULL_HANDLE) {
            vkDestroySampler(m_Device, m_LinearSampler, nullptr);
            m_LinearSampler = VK_NULL_HANDLE;
        }
        if (m_NearestSampler != VK_NULL_HANDLE) {
            vkDestroySampler(m_Device, m_NearestSampler, nullptr);
            m_NearestSampler = VK_NULL_HANDLE;
        }
        if (m_RenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
            m_RenderPass = VK_NULL_HANDLE;
        }

        m_Device = VK_NULL_HANDLE;
    }

} // namespace Renderer
} // namespace Core
