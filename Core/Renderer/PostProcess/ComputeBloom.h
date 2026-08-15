#pragma once

// Bloom, as compute passes in the path the rest of the frame uses.
//
// `PostProcessManager` registers five passes and none of them can run:
// `SSAOPass`, `BloomPass`, `DepthOfFieldPass`, and `MotionBlurPass` contain no
// `vkUpdateDescriptorSets` call anywhere, so their descriptor sets are never
// written and binding one crashes. SSAO and depth of field could not work even
// if that were fixed - `PostProcessPass` has no way to receive the depth buffer,
// and `PostProcessManager::Execute` discards the `sceneDepthInput` it is handed.
//
// This is the one effect that only needs scene colour, done in a way that can be
// verified. It is the standard progressive-blur bloom: threshold, halve down a
// mip chain, then upsample back accumulating as it goes, which gives a wide soft
// falloff for far less work than a single large kernel.

#include "Core/Math/Math.h"
#include "Core/RHI/Vulkan/VulkanGpuResources.h"

#include <cstdint>
#include <vector>

namespace Core {
namespace ECS { struct PostProcessSettings; }
namespace RHI { class VulkanContext; }

namespace Renderer {

    struct BloomStats {
        bool Active = false;
        uint32_t MipCount = 0;
        uint32_t Width = 0;
        uint32_t Height = 0;
    };

    class ComputeBloom {
    public:
        ComputeBloom() = default;
        ~ComputeBloom();

        ComputeBloom(const ComputeBloom&) = delete;
        ComputeBloom& operator=(const ComputeBloom&) = delete;

        bool Initialize(RHI::VulkanContext* context);
        void Shutdown();
        bool IsInitialized() const { return m_Context != nullptr && m_DownsamplePipeline.IsValid(); }

        // Sizes the mip chain for a new render resolution.
        bool Resize(uint32_t renderWidth, uint32_t renderHeight);

        // Reads `sceneColor` (left in SHADER_READ_ONLY) and writes the composited
        // result to its own output, also left in SHADER_READ_ONLY. The scene
        // image is never written, so there is no read-write hazard on it.
        void Render(VkCommandBuffer cmd, RHI::GpuImage& sceneColor,
                    const ECS::PostProcessSettings& settings);

        VkImageView GetOutputView() const { return m_Output.View; }
        RHI::GpuImage& GetOutputImage() { return m_Output; }
        const BloomStats& GetStats() const { return m_Stats; }

    private:
        struct BloomConstants {
            Math::Vec4 SourceSize;   // xy size, zw 1/size
            Math::Vec4 TargetSize;
            Math::Vec4 Params;       // x threshold, y soft knee, z intensity, w scatter
        };

        bool CreatePipelines();
        bool CreateTargets(uint32_t width, uint32_t height);
        void DestroyTargets();
        void Dispatch(VkCommandBuffer cmd, const RHI::ComputePipeline& pipeline,
                      uint32_t setIndex, VkImageView source, RHI::GpuImage& target,
                      const BloomConstants& constants);

        RHI::VulkanContext* m_Context = nullptr;

        RHI::ComputePipeline m_ThresholdPipeline{};
        RHI::ComputePipeline m_DownsamplePipeline{};
        RHI::ComputePipeline m_UpsamplePipeline{};
        RHI::ComputePipeline m_CompositePipeline{};

        VkDescriptorSetLayout m_SetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        // One set per dispatch in a frame: threshold, one per downsample, one per
        // upsample, and the composite. Reusing a set within a frame would
        // overwrite bindings the earlier dispatch has not consumed yet.
        std::vector<VkDescriptorSet> m_Sets;
        std::vector<RHI::GpuBuffer> m_Constants;
        uint32_t m_SetCursor = 0;

        // Separate images rather than mips of one: a compute shader cannot read
        // and write the same image safely, and separate images keep every
        // barrier a whole-resource transition.
        std::vector<RHI::GpuImage> m_Chain;
        RHI::GpuImage m_Output{};
        VkSampler m_Sampler = VK_NULL_HANDLE;

        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        BloomStats m_Stats{};
    };

} // namespace Renderer
} // namespace Core
