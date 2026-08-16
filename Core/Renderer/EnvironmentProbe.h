#pragma once

// A captured environment cubemap for reflections and ambient specular.
//
// Reflections could only fall back to a two-colour analytic sky: anything not on
// screen reflected a gradient rather than the room it was standing in. A probe
// is what the screen-space trace misses.
//
// It is baked from the renderer's own output rather than a second render path.
// Six frames are rendered with the camera pointed down each face and copied into
// the cube, which costs six frames once instead of a duplicate pipeline forever.
// That also means the probe reflects exactly what the engine draws, including
// whatever the lighting and post chain do to it.
//
// Mips are a GGX prefilter, not a plain blur: each level convolves the captured
// environment with the specular lobe for the roughness that level stands for, by
// importance sampling the distribution. A box downsample is cheaper and looks
// approximately right, but it spreads energy the way a camera defocus does
// rather than the way a rough surface does, and the difference shows on anything
// midway between mirror and matte.

#include "Core/Math/Math.h"
#include "Core/RHI/Vulkan/VulkanGpuResources.h"

#include <cstdint>

namespace Core {
namespace RHI { class VulkanContext; }

namespace Renderer {

    struct EnvironmentProbeStats {
        bool Ready = false;
        bool Baking = false;
        uint32_t FacesCaptured = 0;
        uint32_t Resolution = 0;
        uint32_t MipLevels = 0;
        Math::Vec3 Position{0.0f};
    };

    class EnvironmentProbe {
    public:
        static constexpr uint32_t kFaceCount = 6;

        EnvironmentProbe() = default;
        ~EnvironmentProbe();

        EnvironmentProbe(const EnvironmentProbe&) = delete;
        EnvironmentProbe& operator=(const EnvironmentProbe&) = delete;

        bool Initialize(RHI::VulkanContext* context, uint32_t resolution = 256);
        void Shutdown();
        bool IsInitialized() const { return m_Context != nullptr && m_Cube.IsValid(); }

        // Starts a bake at `position`. The next six frames render the faces.
        void RequestBake(const Math::Vec3& position);
        bool IsBaking() const { return m_Baking; }
        bool IsReady() const { return m_Ready; }

        // View and projection for the face being captured this frame.
        void GetFaceView(Math::Mat4& view, Math::Mat4& projection) const;

        // Copies the frame's result into the current face and advances. Once the
        // sixth lands, mips are built and the probe goes live.
        void CaptureFace(VkCommandBuffer cmd, RHI::GpuImage& source);

        // Moves the cube out of UNDEFINED so it can be bound before the first
        // bake. A shader that declares the sampler needs a legal image behind
        // it whether or not the branch that reads it runs.
        void PrepareForSampling(VkCommandBuffer cmd);

        VkImageView GetCubeView() const { return m_Cube.View; }
        VkSampler GetSampler() const { return m_Sampler; }
        const EnvironmentProbeStats& GetStats() const { return m_Stats; }

    private:
        void GenerateMips(VkCommandBuffer cmd);
        void PrefilterMips(VkCommandBuffer cmd);
        bool CreatePrefilter();

        static constexpr uint32_t kMaxMips = 11;

        RHI::VulkanContext* m_Context = nullptr;
        RHI::GpuImage m_Cube{};
        VkSampler m_Sampler = VK_NULL_HANDLE;

        // Storage views, one per mip, each covering all six faces. A cube view
        // cannot be written through; a 2D array view over the same memory can.
        VkImageView m_MipViews[kMaxMips] = {};
        RHI::ComputePipeline m_PrefilterPipeline{};
        VkDescriptorSetLayout m_PrefilterSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_PrefilterPool = VK_NULL_HANDLE;
        VkDescriptorSet m_PrefilterSets[kMaxMips] = {};
        RHI::GpuBuffer m_PrefilterUniforms[kMaxMips] = {};

        uint32_t m_Resolution = 0;
        uint32_t m_MipLevels = 1;
        uint32_t m_Face = 0;
        bool m_Baking = false;
        bool m_Ready = false;
        Math::Vec3 m_Position{0.0f};
        EnvironmentProbeStats m_Stats{};
    };

} // namespace Renderer
} // namespace Core
