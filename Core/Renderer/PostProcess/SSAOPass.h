#pragma once

#include "PostProcessPass.h"
#include <array>
#include <vector>

namespace Core {
namespace Renderer {

    // SSAO settings for screen-space ambient occlusion
    struct SSAOSettings {
        float radius = 0.5f;         // Sample radius in world units
        float bias = 0.025f;         // Depth comparison bias
        float intensity = 1.0f;      // AO strength
        int kernelSize = 16;         // Sample count (16/32/64)
        int noiseSize = 4;           // Noise texture dimension
        int blurPasses = 2;          // Bilateral blur iterations
    };

    class SSAOPass : public PostProcessPass {
    public:
        SSAOPass() = default;
        ~SSAOPass() override;

        // Non-copyable
        SSAOPass(const SSAOPass&) = delete;
        SSAOPass& operator=(const SSAOPass&) = delete;

        void Initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                       VkRenderPass renderPass, VkExtent2D extent) override;
        void Execute(VkCommandBuffer cmd, FramebufferChain& chain,
                    const ECS::PostProcessSettings& settings) override;
        void Resize(VkExtent2D newExtent) override;
        void Cleanup() override;
        bool IsEnabled(const ECS::PostProcessSettings& settings) const override;
        const char* GetName() const override { return "SSAO"; }

        void SetSampler(VkSampler linearSampler, VkSampler nearestSampler);
        void SetDepthView(VkImageView depthView) { m_DepthView = depthView; }
        void SetNormalView(VkImageView normalView) { m_NormalView = normalView; }

    private:
        static constexpr int MAX_KERNEL_SIZE = 64;

        struct SSAOPushConstants {
            alignas(16) float projectionMatrix[16];
            alignas(4) float radius;
            alignas(4) float bias;
            alignas(4) float intensity;
            alignas(4) int kernelSize;
            alignas(8) float screenSize[2];
            alignas(4) float nearPlane;
            alignas(4) float farPlane;
        };

        struct BlurPushConstants {
            alignas(8) float texelSize[2];
            alignas(4) int horizontal;
            alignas(4) float depthThreshold;
        };

        void CreateNoiseTexture();
        void CreateKernelBuffer();
        void CreateAOBuffer();
        void CreatePipelines();
        void CreateDescriptorSetLayout();
        void CreateDescriptorPool();
        void AllocateDescriptorSets();
        void UpdateDescriptorSets();
        uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

        void SSAOMainPass(VkCommandBuffer cmd, const SSAOSettings& settings);
        void BlurPass(VkCommandBuffer cmd, bool horizontal);
        void ApplyPass(VkCommandBuffer cmd, FramebufferChain& chain);

        void TransitionImageLayout(VkCommandBuffer cmd, VkImage image,
                                   VkImageLayout oldLayout, VkImageLayout newLayout);

        VkDevice m_Device = VK_NULL_HANDLE;
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkExtent2D m_Extent{};
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        VkSampler m_LinearSampler = VK_NULL_HANDLE;
        VkSampler m_NearestSampler = VK_NULL_HANDLE;

        // Input views from G-buffer
        VkImageView m_DepthView = VK_NULL_HANDLE;
        VkImageView m_NormalView = VK_NULL_HANDLE;

        // Noise texture (4x4 random rotation vectors)
        VkImage m_NoiseImage = VK_NULL_HANDLE;
        VkDeviceMemory m_NoiseMemory = VK_NULL_HANDLE;
        VkImageView m_NoiseView = VK_NULL_HANDLE;

        // Sample kernel uniform buffer
        VkBuffer m_KernelBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_KernelMemory = VK_NULL_HANDLE;

        // AO result buffer (R8 format for efficiency)
        VkImage m_AOImage = VK_NULL_HANDLE;
        VkDeviceMemory m_AOMemory = VK_NULL_HANDLE;
        VkImageView m_AOView = VK_NULL_HANDLE;
        VkFramebuffer m_AOFramebuffer = VK_NULL_HANDLE;

        // Blur intermediate buffer
        VkImage m_BlurImage = VK_NULL_HANDLE;
        VkDeviceMemory m_BlurMemory = VK_NULL_HANDLE;
        VkImageView m_BlurView = VK_NULL_HANDLE;
        VkFramebuffer m_BlurFramebuffer = VK_NULL_HANDLE;

        // Pipelines
        VkPipelineLayout m_SSAOPipelineLayout = VK_NULL_HANDLE;
        VkPipelineLayout m_BlurPipelineLayout = VK_NULL_HANDLE;
        VkPipelineLayout m_ApplyPipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_SSAOPipeline = VK_NULL_HANDLE;
        VkPipeline m_BlurPipeline = VK_NULL_HANDLE;
        VkPipeline m_ApplyPipeline = VK_NULL_HANDLE;

        // Descriptors
        VkDescriptorSetLayout m_SSAODescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_BlurDescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_ApplyDescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet m_SSAODescriptorSet = VK_NULL_HANDLE;
        VkDescriptorSet m_BlurHDescriptorSet = VK_NULL_HANDLE;
        VkDescriptorSet m_BlurVDescriptorSet = VK_NULL_HANDLE;
        VkDescriptorSet m_ApplyDescriptorSet = VK_NULL_HANDLE;

        // Pre-computed sample kernel
        std::array<float, MAX_KERNEL_SIZE * 4> m_Kernel;  // vec4 for each sample
    };

} // namespace Renderer
} // namespace Core
