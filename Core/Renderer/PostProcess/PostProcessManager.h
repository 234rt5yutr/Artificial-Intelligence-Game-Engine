#pragma once

#include "PostProcessPass.h"
#include "FramebufferChain.h"
#include <vector>
#include <memory>

namespace Core {

namespace ECS {
    struct PostProcessSettings;
}

namespace Renderer {

    class PostProcessManager {
    public:
        PostProcessManager() = default;
        ~PostProcessManager();

        // Non-copyable
        PostProcessManager(const PostProcessManager&) = delete;
        PostProcessManager& operator=(const PostProcessManager&) = delete;

        void Initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                       VkExtent2D extent);
        void AddPass(std::unique_ptr<PostProcessPass> pass);
        void RemovePass(const char* name);
        void Execute(VkCommandBuffer cmd, const ECS::PostProcessSettings& settings,
                    VkImageView sceneColorInput, VkImageView sceneDepthInput);
        void Resize(VkExtent2D newExtent);
        void Cleanup();

        // For final output
        VkImageView GetFinalOutput() const { return m_FramebufferChain.GetCurrentInputView(); }

        VkRenderPass GetRenderPass() const { return m_RenderPass; }
        FramebufferChain& GetFramebufferChain() { return m_FramebufferChain; }
        const FramebufferChain& GetFramebufferChain() const { return m_FramebufferChain; }

        VkSampler GetLinearSampler() const { return m_LinearSampler; }
        VkSampler GetNearestSampler() const { return m_NearestSampler; }

        // Query
        size_t GetPassCount() const { return m_Passes.size(); }
        PostProcessPass* GetPass(size_t index) const;
        PostProcessPass* FindPass(const char* name) const;
        bool IsInitialized() const { return m_Device != VK_NULL_HANDLE; }

    private:
        void CreateRenderPass();
        void CreateSamplers();

        VkDevice m_Device = VK_NULL_HANDLE;
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkExtent2D m_Extent{};

        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        VkSampler m_LinearSampler = VK_NULL_HANDLE;
        VkSampler m_NearestSampler = VK_NULL_HANDLE;

        FramebufferChain m_FramebufferChain;
        std::vector<std::unique_ptr<PostProcessPass>> m_Passes;
    };

} // namespace Renderer
} // namespace Core
