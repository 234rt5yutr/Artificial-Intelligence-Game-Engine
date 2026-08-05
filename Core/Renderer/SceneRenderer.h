#pragma once

// SceneRenderer owns the frame.
//
// Before this, `VulkanContext::DrawFrame` rendered straight into the swapchain
// with one unlit pipeline. That leaves nowhere to put GI, no resolution to
// upscale from, and nothing for an editor viewport to display. Everything here
// hangs off one decision: the scene renders into offscreen targets at a render
// resolution that is independent of the display resolution.
//
//   cull (compute)  ->  slim G-buffer pass  ->  HZB  ->  late cull + pass
//        -> GI (compute)  ->  resolve (compute)  ->  FSR  ->  composite
//
// VulkanContext still owns the device, swapchain, and submission; this owns
// everything between "a command buffer has begun" and "the swapchain pass is
// about to end".

// FrameRenderData stores these by value, so the definitions have to be here
// rather than forward declared.
#include "Core/ECS/Systems/LightSystem.h"
#include "Core/ECS/Systems/RenderSystem.h"
#include "Core/Math/Math.h"
#include "Core/RHI/Vulkan/VulkanGpuResources.h"
#include "Core/Renderer/GI/DynamicGlobalIllumination.h"
#include "Core/Renderer/GPUDriven/GPUDrivenCuller.h"
#include "Core/Renderer/GPUDriven/GPUScene.h"
#include "Core/Renderer/Shadows/ShadowRenderer.h"
#include "Core/Renderer/Textures/TextureLibrary.h"
#include "Core/Renderer/Upscaling/FSRUpscaler.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Core {
namespace RHI { class VulkanContext; }

namespace Renderer {

    // Everything the renderer needs for one frame, copied out of the simulation
    // so the render thread never reads live ECS state.
    struct FrameRenderData {
        std::vector<ECS::DrawCommand> DrawCommands;
        std::vector<ECS::DirectionalLightData> DirectionalLights;
        std::vector<ECS::PointLightData> PointLights;
        std::vector<ECS::SpotLightData> SpotLights;
        Math::Mat4 View{1.0f};
        Math::Mat4 Projection{1.0f};
        Math::Mat4 ViewProjection{1.0f};
        Math::Vec3 CameraPosition{0.0f};
        float TimeSeconds = 0.0f;
        uint64_t FrameIndex = 0;

        void Clear() {
            DrawCommands.clear();
            DirectionalLights.clear();
            PointLights.clear();
            SpotLights.clear();
        }
    };

    struct SceneRendererStats {
        uint32_t RenderWidth = 0;
        uint32_t RenderHeight = 0;
        uint32_t DisplayWidth = 0;
        uint32_t DisplayHeight = 0;
        uint32_t IndirectDraws = 0;      // material batches issued through the GPU path
        uint32_t DirectDraws = 0;        // meshes that fell back to a per-mesh draw
        uint32_t SkippedDraws = 0;
        uint32_t MaterialPipelines = 0;
        uint32_t DirectionalLights = 0;
        uint32_t PointLights = 0;
        uint32_t SpotLights = 0;
        bool GPUDrivenActive = false;
    };

    class SceneRenderer {
    public:
        SceneRenderer() = default;
        ~SceneRenderer();

        SceneRenderer(const SceneRenderer&) = delete;
        SceneRenderer& operator=(const SceneRenderer&) = delete;

        bool Initialize(RHI::VulkanContext* context);
        void Shutdown();
        bool IsInitialized() const { return m_Context != nullptr && m_ScenePassClear != VK_NULL_HANDLE; }

        // Display resolution changed (swapchain recreate). Recomputes the render
        // resolution from the FSR quality mode and rebuilds every target.
        bool OnDisplayResize(uint32_t width, uint32_t height);

        // Called on the render side before recording; takes ownership of nothing.
        void BeginFrame(const FrameRenderData& frame);

        // Records everything that happens outside the swapchain render pass.
        void RecordOffscreen(VkCommandBuffer cmd);
        // Records the fullscreen composite; must be called inside the swapchain
        // render pass, before the UI draws over it.
        void RecordComposite(VkCommandBuffer cmd);

        // Jitter for this frame, in NDC units, to be folded into the projection.
        Math::Vec2 GetProjectionJitter(uint64_t frameIndex) const;

        // What the editor viewport samples. Valid after the first RecordOffscreen.
        VkImageView GetViewportImageView() const;
        VkSampler GetViewportSampler() const { return m_LinearSampler; }

        ShadowRenderer& GetShadowRenderer() { return m_Shadows; }
        GPUScene& GetGPUScene() { return m_GPUScene; }
        GPUDrivenCuller& GetCuller() { return m_Culler; }
        DynamicGlobalIllumination& GetGlobalIllumination() { return m_GI; }
        FSRUpscaler& GetUpscaler() { return m_FSR; }
        const SceneRendererStats& GetStats() const { return m_Stats; }

        void SetGPUDrivenEnabled(bool enabled) { m_GPUDrivenEnabled = enabled; }
        bool IsGPUDrivenEnabled() const { return m_GPUDrivenEnabled; }
        // Recomputes render resolution and rebuilds targets after a quality
        // change. Cheap enough to call from a tool handler.
        bool ApplyUpscalerQuality();

    private:
        // std140 image of the scene uniform block. Every offset here has a
        // matching declaration in the shader template; changing one without the
        // other silently corrupts lighting.
        struct alignas(16) SceneUniforms {
            Math::Mat4 ViewProjection;
            Math::Mat4 View;
            Math::Mat4 InverseViewProjection;
            Math::Vec4 CameraPosition;
            Math::Vec4 Resolution;
            Math::Vec4 AmbientColor;
            Math::Vec4 DirectionalDirection[4];
            Math::Vec4 DirectionalColor[4];
            Math::Vec4 PointPositionRadius[16];
            Math::Vec4 PointColorIntensity[16];
            Math::UVec4 LightCounts;
            float TimeSeconds;
            float Pad0;
            float Pad1;
            float Pad2;
            Math::Vec4 SpotPositionRadius[kMaxSpotShadows];
            Math::Vec4 SpotDirectionInner[kMaxSpotShadows];   // xyz dir, w cos(inner)
            Math::Vec4 SpotColorOuter[kMaxSpotShadows];       // rgb colour, w cos(outer)
            // x = intensity, y = atlas slot (-1 = lit but unshadowed)
            Math::Vec4 SpotIntensitySlot[kMaxSpotShadows];
            Math::Mat4 SpotShadowMatrix[kMaxSpotShadows];
            // xy = atlas size, z = tile size, w = 1 / atlas size
            Math::Vec4 AtlasParams;
            Math::Mat4 CascadeViewProjection[kMaxShadowCascades];
            Math::Vec4 CascadeSplits;
            // x = cascade count, y = PCF texel step, z = normal bias,
            // w = index of the shadow-casting directional light (-1 for none)
            Math::Vec4 ShadowParams;
        };

        struct MaterialPipeline {
            VkPipeline Pipeline = VK_NULL_HANDLE;
            // Each material owns its texture set: slot 0 of one material is not
            // slot 0 of another, so a single shared set would cross-bind them.
            VkDescriptorSet TextureSet = VK_NULL_HANDLE;
            uint64_t GraphHash = 0;
            bool Valid = false;
        };

        bool CreateRenderPasses();
        bool CreateTargets(uint32_t width, uint32_t height);
        void DestroyTargets();
        bool CreateSharedResources();
        bool CreateCompositePipeline();
        bool CreateResolvePipeline();
        bool CreateDummyTexture();
        bool CreateDummyShadow();
        void EnsureMaterialPipelines();
        // Resolves each material's texture slot names against the library and
        // rewrites its descriptor set. Slots with no matching texture fall back
        // to the white placeholder.
        void UpdateMaterialTextureSets();
        VkDescriptorSet GetTextureSetForMaterial(uint32_t materialIndex);
        VkPipeline GetPipelineForMaterial(uint32_t materialIndex);
        void RecordGeometry(VkCommandBuffer cmd, bool latePhase);
        void RecordDirectDraws(VkCommandBuffer cmd);
        void UpdateSceneUniforms(const FrameRenderData& frame);

        RHI::VulkanContext* m_Context = nullptr;

        VkRenderPass m_ScenePassClear = VK_NULL_HANDLE;
        VkRenderPass m_ScenePassLoad = VK_NULL_HANDLE;
        VkFramebuffer m_SceneFramebuffer = VK_NULL_HANDLE;

        RHI::GpuImage m_SceneColor{};    // direct lighting, HDR
        RHI::GpuImage m_SceneAlbedo{};   // rgb base colour, a roughness
        RHI::GpuImage m_SceneNormal{};   // rgb world normal (encoded), a metallic
        RHI::GpuImage m_SceneDepth{};
        RHI::GpuImage m_Resolved{};      // direct + GI, pre-upscale

        RHI::GpuImage m_DummyWhite{};
        // The lit shader statically references the cascade sampler, so its
        // descriptor must be valid even when shadows are off or failed to
        // initialize. A 1x1 depth array costs nothing and keeps that true.
        RHI::GpuImage m_DummyShadow{};
        RHI::GpuImage m_DummyAtlas{};
        bool m_DummyInitialized = false;

        VkSampler m_LinearSampler = VK_NULL_HANDLE;
        VkSampler m_PointSampler = VK_NULL_HANDLE;
        VkSampler m_ShadowFallbackSampler = VK_NULL_HANDLE;

        VkDescriptorSetLayout m_SceneSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_MaterialTextureSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_ResolveSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_CompositeSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet m_SceneSet = VK_NULL_HANDLE;
        VkDescriptorSet m_MaterialTextureSet = VK_NULL_HANDLE;
        VkDescriptorSet m_ResolveSet = VK_NULL_HANDLE;
        VkDescriptorSet m_CompositeSet = VK_NULL_HANDLE;

        VkPipelineLayout m_GeometryLayout = VK_NULL_HANDLE;
        VkPipelineLayout m_CompositeLayout = VK_NULL_HANDLE;
        VkPipeline m_CompositePipeline = VK_NULL_HANDLE;
        RHI::ComputePipeline m_ResolvePipeline{};

        RHI::GpuBuffer m_SceneUniformBuffer{};
        RHI::GpuBuffer m_ResolveUniformBuffer{};

        std::unordered_map<uint32_t, MaterialPipeline> m_MaterialPipelines;
        uint64_t m_MaterialLibraryRevision = UINT64_MAX;
        uint64_t m_TextureLibraryRevision = UINT64_MAX;
        VkDescriptorPool m_MaterialTexturePool = VK_NULL_HANDLE;

        GPUScene m_GPUScene;
        GPUDrivenCuller m_Culler;
        ShadowRenderer m_Shadows;
        DynamicGlobalIllumination m_GI;
        FSRUpscaler m_FSR;

        FrameRenderData m_Frame;
        Math::Mat4 m_PreviousViewProjection{1.0f};
        std::vector<const ECS::DrawCommand*> m_DirectDraws;

        uint32_t m_DisplayWidth = 0;
        uint32_t m_DisplayHeight = 0;
        uint32_t m_RenderWidth = 0;
        uint32_t m_RenderHeight = 0;
        bool m_GPUDrivenEnabled = true;
        bool m_WarnedSkinned = false;

        SceneRendererStats m_Stats{};
    };

} // namespace Renderer
} // namespace Core
