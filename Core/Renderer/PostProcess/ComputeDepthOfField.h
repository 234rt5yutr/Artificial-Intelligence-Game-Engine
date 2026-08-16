#pragma once

// Depth of field, as a compute pass over the lit image and the G-buffer depth.
//
// `PostProcessComponent` has carried dofEnabled, dofFocalDistance, dofFocalRange
// and dofMaxBlur since the beginning and nothing ever read them: the old
// DepthOfFieldPass could not run, because `PostProcessPass` had no parameter
// through which to receive a depth buffer. The settings were reachable over MCP
// and did nothing at all.
//
// It runs after the temporal pass. Blurring first and resolving afterwards would
// feed a blurred history back into a sharp frame, which reads as smearing rather
// than defocus.
//
// ponytail: one gather with a fixed disc, radius scaled by circle of confusion.
// That means no separate near and far fields, so a sharp foreground object does
// not bleed onto the blurred background behind it the way a real lens makes it.
// The upgrade is a two-field split with its own near-field dilate.

#include "Core/Math/Math.h"
#include "Core/RHI/Vulkan/VulkanGpuResources.h"

#include <cstdint>

namespace Core {
namespace ECS { struct PostProcessSettings; }
namespace RHI { class VulkanContext; }

namespace Renderer {

    struct DepthOfFieldInputs {
        VkImageView DepthView = VK_NULL_HANDLE;
        VkSampler Sampler = VK_NULL_HANDLE;
        Math::Mat4 View{1.0f};
        Math::Mat4 InverseViewProjection{1.0f};
    };

    struct DepthOfFieldStats {
        bool Active = false;
        uint32_t Width = 0;
        uint32_t Height = 0;
        float FocalDistance = 0.0f;
        float FocalRange = 0.0f;
        float MaxBlurPixels = 0.0f;
    };

    class ComputeDepthOfField {
    public:
        ComputeDepthOfField() = default;
        ~ComputeDepthOfField();

        ComputeDepthOfField(const ComputeDepthOfField&) = delete;
        ComputeDepthOfField& operator=(const ComputeDepthOfField&) = delete;

        bool Initialize(RHI::VulkanContext* context);
        void Shutdown();
        bool IsInitialized() const { return m_Context != nullptr && m_Pipeline.IsValid(); }

        bool Resize(uint32_t width, uint32_t height);

        // Reads `source`, writes the defocused image, and leaves it in
        // SHADER_READ_ONLY. Inactive when the settings disable it, in which case
        // the caller keeps its own image.
        void Render(VkCommandBuffer cmd, RHI::GpuImage& source,
                    const DepthOfFieldInputs& inputs, const ECS::PostProcessSettings& settings);

        RHI::GpuImage& GetOutputImage() { return m_Output; }
        const DepthOfFieldStats& GetStats() const { return m_Stats; }

    private:
        struct DofUniforms {
            Math::Mat4 View;
            Math::Mat4 InverseViewProjection;
            Math::Vec4 Resolution;   // xy size, zw 1/size
            Math::Vec4 Params;       // x focal distance, y focal range, z max blur px
        };

        bool CreatePipeline();
        bool CreateTargets(uint32_t width, uint32_t height);
        void DestroyTargets();

        RHI::VulkanContext* m_Context = nullptr;
        RHI::ComputePipeline m_Pipeline{};
        VkDescriptorSetLayout m_SetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet m_Set = VK_NULL_HANDLE;
        RHI::GpuBuffer m_Uniforms{};

        RHI::GpuImage m_Output{};
        VkSampler m_Sampler = VK_NULL_HANDLE;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        DepthOfFieldStats m_Stats{};
    };

} // namespace Renderer
} // namespace Core
