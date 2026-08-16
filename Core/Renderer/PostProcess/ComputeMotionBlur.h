#pragma once

// Motion blur, gathered along the velocity buffer.
//
// `PostProcessComponent` has carried motionBlurEnabled and its strength from the
// start, and the old `MotionBlurPass` could never run: it wrote no descriptor
// sets, and there was no velocity buffer for it to read even if it had. The
// geometry pass now writes one, so this is a gather along it.
//
// It runs last in the chain, after the temporal resolve and depth of field.
// Blurring before the temporal pass would push a smeared frame into the history
// and drag the next one toward it, and blurring before defocus would let motion
// smear across a depth boundary that defocus had not yet softened.
//
// The gather follows the largest velocity in the pixel's neighbourhood rather
// than the pixel's own. A background pixel beside a fast object has no velocity
// of its own, so gathering along it would leave the object blurring strictly
// inside its silhouette with a hard edge against a sharp background - which is
// not what a shutter does. Two small passes reduce velocity to a per-tile
// maximum and then to a maximum over each tile's neighbours, and the blur reads
// that.

#include "Core/Math/Math.h"
#include "Core/RHI/Vulkan/VulkanGpuResources.h"

#include <cstdint>

namespace Core {
namespace ECS { struct PostProcessSettings; }
namespace RHI { class VulkanContext; }

namespace Renderer {

    struct MotionBlurInputs {
        VkImageView VelocityView = VK_NULL_HANDLE;
        VkSampler Sampler = VK_NULL_HANDLE;
        // Frame time over the target frame time. Velocity is per frame, so a
        // frame that took twice as long has to blur half as far to describe the
        // same shutter.
        float FrameScale = 1.0f;
    };

    struct MotionBlurStats {
        bool Active = false;
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint32_t SampleCount = 0;
        float Strength = 0.0f;
    };

    class ComputeMotionBlur {
    public:
        ComputeMotionBlur() = default;
        ~ComputeMotionBlur();

        ComputeMotionBlur(const ComputeMotionBlur&) = delete;
        ComputeMotionBlur& operator=(const ComputeMotionBlur&) = delete;

        bool Initialize(RHI::VulkanContext* context);
        void Shutdown();
        bool IsInitialized() const { return m_Context != nullptr && m_Pipeline.IsValid(); }

        bool Resize(uint32_t width, uint32_t height);

        // Reads `source` and leaves the blurred result in SHADER_READ_ONLY.
        // Inactive when the settings disable it or nothing moved.
        void Render(VkCommandBuffer cmd, RHI::GpuImage& source, const MotionBlurInputs& inputs,
                    const ECS::PostProcessSettings& settings);

        RHI::GpuImage& GetOutputImage() { return m_Output; }
        // The two reductions, for inspection. The dilation between them is the
        // mechanism that lets a blur reach past the geometry that caused it, and
        // it is not visible in the final image on its own.
        RHI::GpuImage& GetTileMaxImage() { return m_TileMax; }
        RHI::GpuImage& GetNeighbourMaxImage() { return m_NeighbourMax; }
        const MotionBlurStats& GetStats() const { return m_Stats; }

    private:
        struct MotionBlurUniforms {
            Math::Vec4 Resolution;   // xy size, zw 1/size
            Math::Vec4 Params;       // x strength, y sample count, z frame scale
            Math::Vec4 TileParams;   // x tile size, y tile columns, z tile rows
        };

        static constexpr uint32_t kTileSize = 16;

        bool CreatePipeline();
        bool CreateTilePipelines();
        void BuildTiles(VkCommandBuffer cmd, const MotionBlurInputs& inputs);
        bool CreateTargets(uint32_t width, uint32_t height);
        void DestroyTargets();

        RHI::VulkanContext* m_Context = nullptr;
        RHI::ComputePipeline m_Pipeline{};
        VkDescriptorSetLayout m_SetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet m_Set = VK_NULL_HANDLE;
        RHI::GpuBuffer m_Uniforms{};

        RHI::GpuImage m_Output{};
        // The largest velocity in each tile, then the largest across each tile's
        // neighbours. Two steps rather than one wide search per pixel.
        RHI::GpuImage m_TileMax{};
        RHI::GpuImage m_NeighbourMax{};
        RHI::ComputePipeline m_TilePipeline{};
        RHI::ComputePipeline m_NeighbourPipeline{};
        VkDescriptorSetLayout m_TileSetLayout = VK_NULL_HANDLE;
        VkDescriptorSet m_TileSet = VK_NULL_HANDLE;
        VkDescriptorSet m_NeighbourSet = VK_NULL_HANDLE;
        RHI::GpuBuffer m_TileUniforms{};
        uint32_t m_TileColumns = 0;
        uint32_t m_TileRows = 0;
        VkSampler m_Sampler = VK_NULL_HANDLE;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        MotionBlurStats m_Stats{};
    };

} // namespace Renderer
} // namespace Core
