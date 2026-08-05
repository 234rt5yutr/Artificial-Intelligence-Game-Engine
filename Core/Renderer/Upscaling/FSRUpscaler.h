#pragma once

// FSR upscaling.
//
// `TemporalUpscalerManager` and `DynamicResolution` already model FSR/DLSS/XeSS
// as configuration - quality modes, render scales, telemetry - but every backend
// was an enum value with nothing behind it, so enabling FSR changed no pixels.
//
// This is the backend. It is an in-engine implementation of AMD's FidelityFX
// Super Resolution 1 pipeline (EASU edge-adaptive spatial upsample followed by
// RCAS robust contrast-adaptive sharpening), not a binding of AMD's SDK: the
// algorithm is published and the SDK is not a declared dependency of this
// project. Temporal stability comes from the engine's own jitter sequence and
// the GI/TAA history rather than from FSR2's motion-vector-driven accumulator.

#include "Core/Math/Math.h"
#include "Core/RHI/Vulkan/VulkanGpuResources.h"

#include <cstdint>
#include <string>

namespace Core {
namespace RHI { class VulkanContext; }

namespace Renderer {

    // Render-scale presets, matching the ratios FSR ships with.
    enum class FSRQualityMode : uint8_t {
        Off = 0,           // 1.00x - upscaler bypassed
        UltraQuality,      // 1.30x
        Quality,           // 1.50x
        Balanced,          // 1.70x
        Performance,       // 2.00x
        UltraPerformance   // 3.00x
    };

    float FSRRenderScaleFor(FSRQualityMode mode);
    const char* FSRQualityModeName(FSRQualityMode mode);
    bool FSRQualityModeFromString(const std::string& text, FSRQualityMode& out);

    struct FSRSettings {
        FSRQualityMode Quality = FSRQualityMode::Quality;
        // 0 = no sharpening, 1 = maximum. RCAS is skipped entirely at 0.
        float Sharpness = 0.25f;
        bool Enabled = true;
        // Sub-pixel jitter fed to the projection matrix so the temporal history
        // sees a different sample position each frame.
        bool JitterEnabled = true;
    };

    struct FSRStats {
        uint32_t RenderWidth = 0;
        uint32_t RenderHeight = 0;
        uint32_t DisplayWidth = 0;
        uint32_t DisplayHeight = 0;
        float RenderScale = 1.0f;
        bool SharpeningActive = false;
        bool Active = false;
    };

    class FSRUpscaler {
    public:
        FSRUpscaler() = default;
        ~FSRUpscaler();

        FSRUpscaler(const FSRUpscaler&) = delete;
        FSRUpscaler& operator=(const FSRUpscaler&) = delete;

        bool Initialize(RHI::VulkanContext* context);
        void Shutdown();
        bool IsInitialized() const { return m_Context != nullptr && m_EASUPipeline.IsValid(); }

        // Sizes the intermediate and output targets. `renderWidth/Height` is what
        // the scene renders at; `displayWidth/Height` is the swapchain.
        bool Resize(uint32_t renderWidth, uint32_t renderHeight,
                    uint32_t displayWidth, uint32_t displayHeight);

        // Upscales `sourceView` (already in SHADER_READ_ONLY) into the output
        // image, leaving it in SHADER_READ_ONLY.
        void Render(VkCommandBuffer cmd, VkImageView sourceView, VkSampler sampler);

        VkImageView GetOutputView() const { return m_Output.View; }
        VkImage GetOutputImage() const { return m_Output.Image; }
        VkExtent2D GetOutputExtent() const { return m_Output.Extent; }

        // Halton(2,3) jitter in pixels for the current frame, or zero when
        // jitter is off. The projection matrix applies it.
        Math::Vec2 GetJitter(uint64_t frameIndex) const;

        FSRSettings& GetSettings() { return m_Settings; }
        const FSRSettings& GetSettings() const { return m_Settings; }
        const FSRStats& GetStats() const { return m_Stats; }

    private:
        struct FSRConstants {
            Math::Vec4 SourceSize;   // xy = size, zw = 1/size
            Math::Vec4 TargetSize;   // xy = size, zw = 1/size
            Math::Vec4 Params;       // x = scale ratio, y = sharpness
        };

        bool CreatePipelines();
        bool CreateTargets(uint32_t width, uint32_t height);
        void DestroyTargets();

        RHI::VulkanContext* m_Context = nullptr;
        FSRSettings m_Settings{};
        FSRStats m_Stats{};

        RHI::ComputePipeline m_EASUPipeline{};
        RHI::ComputePipeline m_RCASPipeline{};
        VkDescriptorSetLayout m_SetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet m_EASUSet = VK_NULL_HANDLE;
        VkDescriptorSet m_RCASSet = VK_NULL_HANDLE;

        // EASU writes here; RCAS reads it and writes m_Output. With sharpening
        // off, EASU writes m_Output directly and m_Intermediate is unused.
        RHI::GpuImage m_Intermediate{};
        RHI::GpuImage m_Output{};
        RHI::GpuBuffer m_EASUConstants{};
        RHI::GpuBuffer m_RCASConstants{};
        VkSampler m_Sampler = VK_NULL_HANDLE;

        uint32_t m_RenderWidth = 0;
        uint32_t m_RenderHeight = 0;
        uint32_t m_DisplayWidth = 0;
        uint32_t m_DisplayHeight = 0;
    };

} // namespace Renderer
} // namespace Core
