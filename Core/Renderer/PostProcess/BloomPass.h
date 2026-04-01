#pragma once

#include "PostProcessPass.h"
#include <array>
#include <vector>

namespace Core {
namespace Renderer {

    // Bloom settings for physically-based bloom
    struct BloomSettings {
        float threshold = 1.0f;      // HDR threshold for bloom extraction
        float softKnee = 0.5f;       // Soft threshold transition
        float intensity = 1.0f;      // Final bloom strength
        int mipLevels = 6;           // Number of blur iterations
        float scatter = 0.7f;        // Energy distribution across mips
    };

    class BloomPass : public PostProcessPass {
    public:
        BloomPass() = default;
        ~BloomPass() override;

        // Non-copyable
        BloomPass(const BloomPass&) = delete;
        BloomPass& operator=(const BloomPass&) = delete;

        void Initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                       VkRenderPass renderPass, VkExtent2D extent) override;
        void Execute(VkCommandBuffer cmd, FramebufferChain& chain,
                    const ECS::PostProcessSettings& settings) override;
        void Resize(VkExtent2D newExtent) override;
        void Cleanup() override;
        bool IsEnabled(const ECS::PostProcessSettings& settings) const override;
        const char* GetName() const override { return "Bloom"; }

        void SetSampler(VkSampler linearSampler) { m_LinearSampler = linearSampler; }

    private:
        static constexpr int MAX_MIP_LEVELS = 8;

        struct MipLevel {
            VkImage image = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            VkExtent2D extent{};
        };

        struct BloomPushConstants {
            alignas(16) float threshold;
            alignas(4) float softKnee;
            alignas(4) float intensity;
            alignas(4) float scatter;
            alignas(8) float texelSize[2];
            alignas(4) int mipLevel;
            alignas(4) int isUpsampling;
        };

        void CreateMipChain();
        void CreatePipelines();
        void CreateDescriptorSetLayout();
        void CreateDescriptorPool();
        void AllocateDescriptorSets();
        void DestroyMipChain();
        uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

        void DownsamplePass(VkCommandBuffer cmd, int srcMip, int dstMip, const BloomSettings& bloomSettings);
        void UpsamplePass(VkCommandBuffer cmd, int srcMip, int dstMip, const BloomSettings& bloomSettings);
        void CompositePass(VkCommandBuffer cmd, FramebufferChain& chain, const BloomSettings& bloomSettings);

        void TransitionImageLayout(VkCommandBuffer cmd, VkImage image,
                                   VkImageLayout oldLayout, VkImageLayout newLayout);

        VkDevice m_Device = VK_NULL_HANDLE;
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkExtent2D m_Extent{};
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        VkSampler m_LinearSampler = VK_NULL_HANDLE;

        // Mip chain for downsample/upsample
        std::vector<MipLevel> m_MipChain;
        int m_MipLevels = 6;

        // Pipelines
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_DownsamplePipeline = VK_NULL_HANDLE;
        VkPipeline m_UpsamplePipeline = VK_NULL_HANDLE;
        VkPipeline m_CompositePipeline = VK_NULL_HANDLE;

        // Descriptors
        VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_DescriptorSets;
    };

} // namespace Renderer
} // namespace Core
