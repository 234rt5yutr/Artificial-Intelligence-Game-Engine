#pragma once

#include "PostProcessPass.h"

namespace Core {
namespace Renderer {

    // Depth of Field settings
    struct DepthOfFieldSettings {
        float focalDistance = 10.0f;   // Distance to focus plane
        float focalRange = 5.0f;       // Range of sharp focus
        float maxBlur = 1.0f;          // Maximum blur radius
        float aperture = 2.8f;         // F-stop for bokeh size
        float focalLength = 50.0f;     // Lens focal length (mm)
    };

    class DepthOfFieldPass : public PostProcessPass {
    public:
        DepthOfFieldPass() = default;
        ~DepthOfFieldPass() override;

        // Non-copyable
        DepthOfFieldPass(const DepthOfFieldPass&) = delete;
        DepthOfFieldPass& operator=(const DepthOfFieldPass&) = delete;

        void Initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                       VkRenderPass renderPass, VkExtent2D extent) override;
        void Execute(VkCommandBuffer cmd, FramebufferChain& chain,
                    const ECS::PostProcessSettings& settings) override;
        void Resize(VkExtent2D newExtent) override;
        void Cleanup() override;
        bool IsEnabled(const ECS::PostProcessSettings& settings) const override;
        const char* GetName() const override { return "DepthOfField"; }

        void SetSampler(VkSampler linearSampler) { m_LinearSampler = linearSampler; }
        void SetDepthView(VkImageView depthView) { m_DepthView = depthView; }

    private:
        struct DoFPushConstants {
            alignas(4) float focalDistance;
            alignas(4) float focalRange;
            alignas(4) float maxBlur;
            alignas(4) float aperture;
            alignas(8) float texelSize[2];
            alignas(4) float nearPlane;
            alignas(4) float farPlane;
            alignas(4) int horizontal;
            alignas(4) float padding;
        };

        void CreateCoCBuffer();
        void CreatePipelines();
        void CreateDescriptorSetLayout();
        void CreateDescriptorPool();
        void AllocateDescriptorSets();
        void TransitionImageLayout(VkCommandBuffer cmd, VkImage image,
                                   VkImageLayout oldLayout, VkImageLayout newLayout);
        uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

        void CoCPass(VkCommandBuffer cmd, const DepthOfFieldSettings& settings);
        void BlurPass(VkCommandBuffer cmd, bool horizontal);
        void CompositePass(VkCommandBuffer cmd, FramebufferChain& chain);

        VkDevice m_Device = VK_NULL_HANDLE;
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkExtent2D m_Extent{};
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        VkSampler m_LinearSampler = VK_NULL_HANDLE;

        VkImageView m_DepthView = VK_NULL_HANDLE;

        // Circle of Confusion buffer
        VkImage m_CoCImage = VK_NULL_HANDLE;
        VkDeviceMemory m_CoCMemory = VK_NULL_HANDLE;
        VkImageView m_CoCView = VK_NULL_HANDLE;
        VkFramebuffer m_CoCFramebuffer = VK_NULL_HANDLE;

        // Near field blur buffer (half resolution)
        VkImage m_NearImage = VK_NULL_HANDLE;
        VkDeviceMemory m_NearMemory = VK_NULL_HANDLE;
        VkImageView m_NearView = VK_NULL_HANDLE;
        VkFramebuffer m_NearFramebuffer = VK_NULL_HANDLE;

        // Far field blur buffer (half resolution)
        VkImage m_FarImage = VK_NULL_HANDLE;
        VkDeviceMemory m_FarMemory = VK_NULL_HANDLE;
        VkImageView m_FarView = VK_NULL_HANDLE;
        VkFramebuffer m_FarFramebuffer = VK_NULL_HANDLE;

        // Pipelines
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_CoCPipeline = VK_NULL_HANDLE;
        VkPipeline m_BlurPipeline = VK_NULL_HANDLE;
        VkPipeline m_CompositePipeline = VK_NULL_HANDLE;

        // Descriptors
        VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet m_CoCDescriptorSet = VK_NULL_HANDLE;
        VkDescriptorSet m_BlurDescriptorSet = VK_NULL_HANDLE;
        VkDescriptorSet m_CompositeDescriptorSet = VK_NULL_HANDLE;
    };

} // namespace Renderer
} // namespace Core
