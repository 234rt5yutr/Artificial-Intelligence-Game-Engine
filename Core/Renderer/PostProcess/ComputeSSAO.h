#pragma once

// Screen-space ambient occlusion, as compute passes over the G-buffer.
//
// `SSAOPass` in this directory cannot run and never could: it writes no
// descriptor sets, and `PostProcessPass` has no parameter through which to
// receive the depth buffer it needs - `PostProcessManager::Execute` discards the
// depth input it is handed. The inputs were never reachable, so no amount of
// fixing the pass body would have helped.
//
// The G-buffer already carries exactly what SSAO wants: a depth target and a
// world-space normal target. This reads both directly.
//
// The result modulates ambient and indirect light in the resolve pass rather
// than being multiplied over the final image. Occlusion is a property of how
// much sky and bounce light reaches a point; applying it to direct lighting as
// well darkens surfaces the sun is plainly hitting.

#include "Core/Math/Math.h"
#include "Core/RHI/Vulkan/VulkanGpuResources.h"

#include <cstdint>

namespace Core {
namespace ECS { struct PostProcessSettings; }
namespace RHI { class VulkanContext; }

namespace Renderer {

    struct SSAOInputs {
        VkImageView DepthView = VK_NULL_HANDLE;
        VkImageView NormalView = VK_NULL_HANDLE;
        VkSampler Sampler = VK_NULL_HANDLE;
        Math::Mat4 View{1.0f};
        Math::Mat4 Projection{1.0f};
        Math::Mat4 InverseViewProjection{1.0f};
        uint64_t FrameIndex = 0;
    };

    struct SSAOStats {
        bool Active = false;
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint32_t KernelSize = 0;
        uint32_t BlurPasses = 0;
    };

    class ComputeSSAO {
    public:
        ComputeSSAO() = default;
        ~ComputeSSAO();

        ComputeSSAO(const ComputeSSAO&) = delete;
        ComputeSSAO& operator=(const ComputeSSAO&) = delete;

        bool Initialize(RHI::VulkanContext* context);
        void Shutdown();
        bool IsInitialized() const { return m_Context != nullptr && m_OcclusionPipeline.IsValid(); }

        // AO runs at half the render resolution; the blur hides the difference
        // and it is the single cheapest win available here.
        bool Resize(uint32_t renderWidth, uint32_t renderHeight);

        // Leaves the AO target in SHADER_READ_ONLY.
        void Render(VkCommandBuffer cmd, const SSAOInputs& inputs,
                    const ECS::PostProcessSettings& settings);

        VkImageView GetOcclusionView() const { return m_Occlusion.View; }
        const SSAOStats& GetStats() const { return m_Stats; }

    private:
        struct SSAOUniforms {
            Math::Mat4 View;
            Math::Mat4 Projection;
            Math::Mat4 InverseViewProjection;
            Math::Vec4 Resolution;   // xy AO size, zw 1/size
            Math::Vec4 Params;       // x radius, y bias, z intensity, w kernel size
            Math::Vec4 BlurParams;   // x = horizontal, y = depth sigma, z = frame index
        };

        bool CreatePipelines();
        bool CreateTargets(uint32_t width, uint32_t height);
        void DestroyTargets();

        RHI::VulkanContext* m_Context = nullptr;

        RHI::ComputePipeline m_OcclusionPipeline{};
        RHI::ComputePipeline m_BlurPipeline{};
        VkDescriptorSetLayout m_SetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        // Occlusion plus one per blur direction; each needs its own set and
        // constants, because they are in flight together.
        VkDescriptorSet m_Sets[3] = {};
        RHI::GpuBuffer m_Uniforms[3] = {};

        RHI::GpuImage m_Occlusion{};
        RHI::GpuImage m_Scratch{};
        VkSampler m_Sampler = VK_NULL_HANDLE;

        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        SSAOStats m_Stats{};
    };

} // namespace Renderer
} // namespace Core
