#pragma once

#include "PostProcessPass.h"

namespace Core {
namespace Renderer {

    // Motion blur settings
    struct MotionBlurSettings {
        float scale = 1.0f;          // Velocity scale multiplier
        int samples = 8;             // Number of blur samples
        float maxVelocity = 32.0f;   // Maximum velocity in pixels
        float shutterAngle = 0.5f;   // Shutter angle (0-1)
    };

    class MotionBlurPass : public PostProcessPass {
    public:
        MotionBlurPass() = default;
        ~MotionBlurPass() override;

        // Non-copyable
        MotionBlurPass(const MotionBlurPass&) = delete;
        MotionBlurPass& operator=(const MotionBlurPass&) = delete;

        void Initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                       VkRenderPass renderPass, VkExtent2D extent) override;
        void Execute(VkCommandBuffer cmd, FramebufferChain& chain,
                    const ECS::PostProcessSettings& settings) override;
        void Resize(VkExtent2D newExtent) override;
        void Cleanup() override;
        bool IsEnabled(const ECS::PostProcessSettings& settings) const override;
        const char* GetName() const override { return "MotionBlur"; }

        void SetSampler(VkSampler linearSampler) { m_LinearSampler = linearSampler; }
        void SetVelocityView(VkImageView velocityView) { m_VelocityView = velocityView; }
        void SetDepthView(VkImageView depthView) { m_DepthView = depthView; }

        // Camera motion data for per-pixel velocity calculation
        struct CameraMotionData {
            float currentViewProj[16];
            float previousViewProj[16];
            float shutterSpeed;
            float padding[3];
        };

        void UpdateCameraMotion(const CameraMotionData& data);

    private:
        struct MotionBlurPushConstants {
            alignas(4) float scale;
            alignas(4) int samples;
            alignas(4) float maxVelocity;
            alignas(8) float texelSize[2];
            alignas(4) float centerWeight;
            alignas(4) float padding;
        };

        void CreateVelocityBuffer();
        void CreatePipelines();
        void CreateDescriptorSetLayout();
        void CreateDescriptorPool();
        void AllocateDescriptorSets();
        void TransitionImageLayout(VkCommandBuffer cmd, VkImage image,
                                   VkImageLayout oldLayout, VkImageLayout newLayout);
        uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

        VkDevice m_Device = VK_NULL_HANDLE;
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkExtent2D m_Extent{};
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        VkSampler m_LinearSampler = VK_NULL_HANDLE;

        VkImageView m_VelocityView = VK_NULL_HANDLE;
        VkImageView m_DepthView = VK_NULL_HANDLE;

        // Camera motion uniform buffer
        VkBuffer m_CameraBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_CameraMemory = VK_NULL_HANDLE;

        // Tile max velocity buffer (for optimization)
        VkImage m_TileMaxImage = VK_NULL_HANDLE;
        VkDeviceMemory m_TileMaxMemory = VK_NULL_HANDLE;
        VkImageView m_TileMaxView = VK_NULL_HANDLE;
        VkFramebuffer m_TileMaxFramebuffer = VK_NULL_HANDLE;

        // Pipelines
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_TileMaxPipeline = VK_NULL_HANDLE;
        VkPipeline m_MotionBlurPipeline = VK_NULL_HANDLE;

        // Descriptors
        VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet m_TileMaxDescriptorSet = VK_NULL_HANDLE;
        VkDescriptorSet m_MotionBlurDescriptorSet = VK_NULL_HANDLE;
    };

} // namespace Renderer
} // namespace Core
