#pragma once

// Screen-space reflections.
//
// Every surface in the engine was purely diffuse plus a direct specular
// highlight: a polished floor showed the light, never the room. The G-buffer
// already carries what a reflection needs - depth, a world normal with metallic
// in its alpha, and albedo with roughness in its alpha - so this is a march over
// buffers that already exist rather than a second view of the scene.
//
// It runs after the resolve, so what it reflects is the fully lit image
// including indirect light, and its result is what the temporal pass then
// stabilises.
//
// ponytail: screen space only. Anything off-screen or hidden behind what is on
// screen cannot be reflected, and the pass fades toward the edges rather than
// pretending otherwise. Reflection probes are the fix, and are a different
// feature, not a bigger version of this one.

#include "Core/Math/Math.h"
#include "Core/RHI/Vulkan/VulkanGpuResources.h"

#include <cstdint>

namespace Core {
namespace RHI { class VulkanContext; }

namespace Renderer {

    struct SSRInputs {
        VkImageView DepthView = VK_NULL_HANDLE;
        VkImageView NormalView = VK_NULL_HANDLE;   // xyz world normal, w metallic
        VkImageView AlbedoView = VK_NULL_HANDLE;   // rgb base colour, a roughness
        VkSampler Sampler = VK_NULL_HANDLE;
        Math::Mat4 View{1.0f};
        Math::Mat4 Projection{1.0f};
        Math::Mat4 InverseViewProjection{1.0f};
        Math::Vec3 CameraPosition{0.0f};
        // The same two colours the geometry pass shaded ambient specular with.
        // A traced hit replaces that term, so this pass has to be able to
        // reproduce exactly what it is replacing.
        Math::Vec3 SkyColor{0.0f};
        Math::Vec3 GroundColor{0.0f};
        // The same probe the geometry pass sampled. A traced hit replaces the
        // ambient specular term, and it can only subtract what it can reproduce
        // - if one pass reads a probe and the other an analytic sky, every
        // reflection edge gets a seam.
        VkImageView ProbeView = VK_NULL_HANDLE;
        VkSampler ProbeSampler = VK_NULL_HANDLE;
        bool ProbeReady = false;
        uint32_t ProbeMipLevels = 1;
        Math::Vec4 ProbePosition{0.0f};
        uint64_t FrameIndex = 0;
    };

    struct SSRSettings {
        bool Enabled = true;
        // Longest ray, in world units. Past this a reflection is not worth the
        // marching, and the pass fades it out.
        float MaxDistance = 40.0f;
        uint32_t StepCount = 32;
        // Refinement halvings after a hit; each one quarters the depth error at
        // the cost of one texture fetch.
        uint32_t RefineSteps = 5;
        // How far behind a depth sample still counts as a hit. Too small and
        // rays pass through thin geometry; too large and they hit the air in
        // front of it.
        float Thickness = 0.5f;
        float Intensity = 1.0f;
        // Above this roughness the surface scatters too widely for a single
        // mirror ray to mean anything.
        float RoughnessCutoff = 0.6f;
    };

    struct SSRStats {
        bool Enabled = true;
        bool Active = false;
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint32_t StepCount = 0;
        float MaxDistance = 0.0f;
    };

    class ComputeSSR {
    public:
        ComputeSSR() = default;
        ~ComputeSSR();

        ComputeSSR(const ComputeSSR&) = delete;
        ComputeSSR& operator=(const ComputeSSR&) = delete;

        bool Initialize(RHI::VulkanContext* context);
        void Shutdown();
        bool IsInitialized() const { return m_Context != nullptr && m_Pipeline.IsValid(); }

        bool Resize(uint32_t width, uint32_t height);

        SSRSettings& GetSettings() { return m_Settings; }
        const SSRSettings& GetSettings() const { return m_Settings; }

        // Reads `source` and writes the reflected image, leaving it in
        // SHADER_READ_ONLY. The output is what the rest of the chain should use;
        // when the pass is off or unavailable it stays inactive and the caller
        // keeps its own image.
        void Render(VkCommandBuffer cmd, RHI::GpuImage& source, const SSRInputs& inputs);

        RHI::GpuImage& GetOutputImage() { return m_Output; }
        const SSRStats& GetStats() const { return m_Stats; }

    private:
        struct SSRUniforms {
            Math::Mat4 View;
            Math::Mat4 Projection;
            Math::Mat4 InverseViewProjection;
            Math::Vec4 Resolution;    // xy size, zw 1/size
            Math::Vec4 CameraPosition;
            Math::Vec4 Params;        // x maxDistance, y stepCount, z thickness, w intensity
            Math::Vec4 Params2;       // x refineSteps, y roughnessCutoff, z frameIndex
            Math::Vec4 SkyColor;
            Math::Vec4 GroundColor;
            Math::Vec4 ProbeParams;   // x ready, y mip count
            Math::Vec4 ProbePosition;
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
        SSRSettings m_Settings{};
        SSRStats m_Stats{};
    };

} // namespace Renderer
} // namespace Core
