#pragma once

// Dynamic global illumination: screen-space radiance gathering backed by a
// world-space irradiance cache.
//
// `GlobalIllumination.h` in the renderer root declares SSGI/VXGI/RTGI/probe
// paths; none of them produce light in a frame. This is the one path that does,
// and it is the one the gap analysis argued for given the iGPU target: screen
// traces where the information exists, a persistent world probe grid where it
// does not, and temporal accumulation over both so a single frame's ray budget
// stays small.
//
// It is Lumen-shaped rather than Lumen: screen traces + a world radiance cache
// with a temporal feedback loop, no surface cache, no hardware ray tracing, no
// distance fields. Those are the upgrade path, not a different design.

#include "Core/Math/Math.h"
#include "Core/RHI/Vulkan/VulkanGpuResources.h"

#include <cstdint>

namespace Core {
namespace RHI { class VulkanContext; }

namespace Renderer {

    struct GISettings {
        bool Enabled = true;
        // Screen-space rays per half-resolution pixel per frame. The temporal
        // filter is what turns this into a usable signal, so keep it low.
        uint32_t RaysPerPixel = 4;
        uint32_t StepsPerRay = 12;
        float MaxTraceDistance = 12.0f;
        float Intensity = 1.0f;
        // Fraction of the new frame blended into history. Lower is smoother and
        // laggier; 0.1 is roughly a 10-frame window.
        float TemporalAlpha = 0.12f;
        float ProbeSpacing = 2.0f;
        Math::Vec3 SkyColor{0.32f, 0.42f, 0.58f};
        float SkyIntensity = 1.0f;
        bool ProbeCacheEnabled = true;
    };

    struct GIStats {
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint32_t ProbeCount = 0;
        uint32_t FramesAccumulated = 0;
        bool Ready = false;
    };

    struct GIFrameInputs {
        VkImageView DepthView = VK_NULL_HANDLE;
        VkImageView NormalView = VK_NULL_HANDLE;
        VkImageView ColorView = VK_NULL_HANDLE;
        VkSampler LinearSampler = VK_NULL_HANDLE;
        Math::Mat4 View{1.0f};
        Math::Mat4 Projection{1.0f};
        Math::Mat4 ViewProjection{1.0f};
        Math::Mat4 PreviousViewProjection{1.0f};
        Math::Vec3 CameraPosition{0.0f};
        uint64_t FrameIndex = 0;
    };

    class DynamicGlobalIllumination {
    public:
        DynamicGlobalIllumination() = default;
        ~DynamicGlobalIllumination();

        DynamicGlobalIllumination(const DynamicGlobalIllumination&) = delete;
        DynamicGlobalIllumination& operator=(const DynamicGlobalIllumination&) = delete;

        bool Initialize(RHI::VulkanContext* context);
        void Shutdown();
        bool IsInitialized() const { return m_Context != nullptr && m_TracePipeline.IsValid(); }

        // GI runs at half the render resolution.
        bool Resize(uint32_t renderWidth, uint32_t renderHeight);

        // Records the probe injection and screen-trace dispatches. Leaves the
        // radiance target in SHADER_READ_ONLY for the resolve pass.
        void Render(VkCommandBuffer cmd, const GIFrameInputs& inputs);

        VkImageView GetRadianceView() const { return m_Radiance.View; }
        VkImageLayout GetRadianceLayout() const { return m_Radiance.Layout; }

        GISettings& GetSettings() { return m_Settings; }
        const GISettings& GetSettings() const { return m_Settings; }
        const GIStats& GetStats() const { return m_Stats; }

    private:
        struct GIUniforms {
            Math::Mat4 InverseViewProjection;
            Math::Mat4 ViewProjection;
            Math::Mat4 PreviousViewProjection;
            Math::Vec4 CameraPosition;
            Math::Vec4 Resolution;      // xy = GI size, zw = 1/size
            Math::Vec4 ProbeOrigin;     // xyz = grid origin, w = spacing
            Math::UVec4 ProbeGrid;      // xyz = counts, w = frame index
            Math::Vec4 TraceParams;     // x=rays y=steps z=maxDistance w=temporalAlpha
            Math::Vec4 SkyColor;        // rgb + intensity
            Math::Vec4 Flags;           // x=intensity y=probeCacheEnabled z=historyValid
        };

        bool CreatePipelines();
        bool CreateTargets(uint32_t width, uint32_t height);
        void DestroyTargets();
        void WriteDescriptors(const GIFrameInputs& inputs);

        RHI::VulkanContext* m_Context = nullptr;
        GISettings m_Settings{};
        GIStats m_Stats{};

        RHI::ComputePipeline m_ProbePipeline{};
        RHI::ComputePipeline m_TracePipeline{};
        VkDescriptorSetLayout m_SetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet m_Set = VK_NULL_HANDLE;

        RHI::GpuImage m_Radiance{};
        RHI::GpuImage m_History{};
        RHI::GpuBuffer m_Probes{};
        RHI::GpuBuffer m_Uniforms{};
        VkSampler m_Sampler = VK_NULL_HANDLE;

        // 32 x 16 x 32 probes around the camera. At the default 2 m spacing that
        // is a 64 x 32 x 64 m cache, which comfortably covers an indoor scene and
        // degrades to sky elsewhere.
        static constexpr uint32_t kProbeGridX = 32;
        static constexpr uint32_t kProbeGridY = 16;
        static constexpr uint32_t kProbeGridZ = 32;

        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        uint32_t m_FramesAccumulated = 0;
        bool m_HistoryValid = false;
    };

} // namespace Renderer
} // namespace Core
