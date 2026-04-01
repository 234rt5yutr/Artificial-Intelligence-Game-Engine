#pragma once

#include "PostProcessPass.h"
#include <string>

namespace Core {
namespace Renderer {

    // Color grading settings for professional color correction
    struct ColorGradingSettings {
        float exposure = 1.0f;          // Exposure adjustment
        float contrast = 1.0f;          // Contrast multiplier
        float saturation = 1.0f;        // Saturation multiplier
        float temperature = 0.0f;       // White balance temperature (-1 to 1)
        float tint = 0.0f;              // White balance tint (-1 to 1)
        float gamma = 1.0f;             // Gamma correction
        float lift = 0.0f;              // Shadow adjustment
        float gain = 1.0f;              // Highlight adjustment
        int tonemapMode = 0;            // 0=ACES, 1=Reinhard, 2=Uncharted2
        std::string lutPath = "";       // 3D LUT texture path
    };

    class ColorGradingPass : public PostProcessPass {
    public:
        ColorGradingPass() = default;
        ~ColorGradingPass() override;

        // Non-copyable
        ColorGradingPass(const ColorGradingPass&) = delete;
        ColorGradingPass& operator=(const ColorGradingPass&) = delete;

        void Initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                       VkRenderPass renderPass, VkExtent2D extent) override;
        void Execute(VkCommandBuffer cmd, FramebufferChain& chain,
                    const ECS::PostProcessSettings& settings) override;
        void Resize(VkExtent2D newExtent) override;
        void Cleanup() override;
        bool IsEnabled(const ECS::PostProcessSettings& settings) const override;
        const char* GetName() const override { return "ColorGrading"; }

        void SetSampler(VkSampler linearSampler) { m_LinearSampler = linearSampler; }
        
        // Load a 3D LUT from file (.cube format)
        bool LoadLUT(const std::string& path);
        void SetDefaultLUT();

    private:
        struct ColorGradingPushConstants {
            alignas(4) float exposure;
            alignas(4) float contrast;
            alignas(4) float saturation;
            alignas(4) float temperature;
            alignas(4) float tint;
            alignas(4) float gamma;
            alignas(4) float lift;
            alignas(4) float gain;
            alignas(4) int tonemapMode;
            alignas(4) int useLUT;
            alignas(4) float lutSize;
            alignas(4) float vignetteIntensity;
            alignas(4) float vignetteSmoothness;
            alignas(4) float padding[3];
        };

        void CreateDefaultLUT();
        void CreatePipelines();
        void CreateDescriptorSetLayout();
        void CreateDescriptorPool();
        void AllocateDescriptorSets();
        void UpdateDescriptorSets();
        uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

        VkDevice m_Device = VK_NULL_HANDLE;
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkExtent2D m_Extent{};
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        VkSampler m_LinearSampler = VK_NULL_HANDLE;

        // 3D LUT texture (32x32x32 or 64x64x64)
        VkImage m_LUTImage = VK_NULL_HANDLE;
        VkDeviceMemory m_LUTMemory = VK_NULL_HANDLE;
        VkImageView m_LUTView = VK_NULL_HANDLE;
        VkSampler m_LUTSampler = VK_NULL_HANDLE;
        int m_LUTSize = 32;
        bool m_HasLUT = false;

        // Pipelines
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_ColorGradingPipeline = VK_NULL_HANDLE;

        // Descriptors
        VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;
    };

} // namespace Renderer
} // namespace Core
