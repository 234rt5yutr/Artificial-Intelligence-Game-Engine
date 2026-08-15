#pragma once

// Temporal antialiasing.
//
// The frame was already being jittered every frame for the upscaler, and
// nothing ever resolved it: each frame sampled a different sub-pixel offset and
// went straight to the screen, so the image crawled instead of antialiasing.
// The jitter was pure cost. This is the pass that spends it.
//
// History is reprojected from depth against the previous unjittered
// view-projection, then constrained to the current 3x3 neighbourhood in YCoCg
// before blending. The clamp is what keeps the result from smearing: anything
// the history holds that this frame's pixels do not support gets pulled back
// into range.
//
// ponytail: there is no velocity target, so reprojection is camera-only and a
// moving object leans entirely on the neighbourhood clamp - correct for static
// geometry, slightly soft on fast movers. The upgrade is a velocity attachment
// on the scene pass plus a previous transform per instance.

#include "Core/Math/Math.h"
#include "Core/RHI/Vulkan/VulkanGpuResources.h"

#include <cstdint>

namespace Core {
namespace RHI { class VulkanContext; }

namespace Renderer {

    struct TAAInputs {
        VkImageView DepthView = VK_NULL_HANDLE;
        VkSampler Sampler = VK_NULL_HANDLE;
        // Both unjittered: reprojection must not chase the sub-pixel offset it
        // is there to resolve.
        Math::Mat4 InverseViewProjection{1.0f};
        Math::Mat4 PreviousViewProjection{1.0f};
        uint64_t FrameIndex = 0;
        // Set when the camera teleported, so the history is dropped rather than
        // smeared across the cut.
        bool ResetHistory = false;
    };

    struct TAAStats {
        bool Enabled = true;
        bool Active = false;
        bool HistoryValid = false;
        uint32_t Width = 0;
        uint32_t Height = 0;
        float Feedback = 0.0f;
    };

    class ComputeTAA {
    public:
        ComputeTAA() = default;
        ~ComputeTAA();

        ComputeTAA(const ComputeTAA&) = delete;
        ComputeTAA& operator=(const ComputeTAA&) = delete;

        bool Initialize(RHI::VulkanContext* context);
        void Shutdown();
        bool IsInitialized() const { return m_Context != nullptr && m_Pipeline.IsValid(); }

        bool Resize(uint32_t width, uint32_t height);

        void SetEnabled(bool enabled);
        bool IsEnabled() const { return m_Stats.Enabled; }
        // How much of the history survives each frame. Higher is steadier and
        // slower to react; past about 0.98 a moving edge never catches up.
        void SetFeedback(float feedback);
        float GetFeedback() const { return m_Feedback; }

        // Reads `source`, writes this frame's history, and leaves it in
        // SHADER_READ_ONLY. The result is the image the rest of the post chain
        // should consume.
        void Render(VkCommandBuffer cmd, RHI::GpuImage& source, const TAAInputs& inputs);

        RHI::GpuImage& GetOutputImage() { return m_History[m_WriteIndex]; }
        VkImageView GetOutputView() const { return m_History[m_WriteIndex].View; }
        const TAAStats& GetStats() const { return m_Stats; }

        // Next frame starts from nothing. Called on resize and on a camera cut.
        void InvalidateHistory() { m_HistoryValid = false; }

    private:
        struct TAAUniforms {
            Math::Mat4 InverseViewProjection;
            Math::Mat4 PreviousViewProjection;
            Math::Vec4 Resolution;   // xy size, zw 1/size
            Math::Vec4 Params;       // x feedback, y history valid, z frame index
        };

        bool CreatePipeline();
        bool CreateTargets(uint32_t width, uint32_t height);
        void DestroyTargets();

        RHI::VulkanContext* m_Context = nullptr;
        RHI::ComputePipeline m_Pipeline{};
        VkDescriptorSetLayout m_SetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        // One set per history slot: the set is rewritten each frame, but the two
        // slots swap roles, so keeping them apart avoids rewriting a set that is
        // still in flight.
        VkDescriptorSet m_Sets[2] = {};
        RHI::GpuBuffer m_Uniforms[2] = {};

        // Ping-pong, because a pass cannot sample the image it writes.
        RHI::GpuImage m_History[2] = {};
        uint32_t m_WriteIndex = 0;
        bool m_HistoryValid = false;

        VkSampler m_Sampler = VK_NULL_HANDLE;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        float m_Feedback = 0.92f;
        TAAStats m_Stats{};
    };

} // namespace Renderer
} // namespace Core
