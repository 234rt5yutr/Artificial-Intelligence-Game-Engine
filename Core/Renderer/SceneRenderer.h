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
#include "Core/Renderer/GPUDriven/GPUSkinningPass.h"
#include "Core/Renderer/GPUDriven/GPUScene.h"
#include "Core/ECS/Components/PostProcessComponent.h"
#include "Core/Renderer/Lighting/ClusteredLightCuller.h"
#include "Core/Renderer/PostProcess/ComputeBloom.h"
#include "Core/Renderer/PostProcess/ComputeSSAO.h"
#include "Core/Renderer/PostProcess/ComputeDepthOfField.h"
#include "Core/Renderer/PostProcess/ComputeMotionBlur.h"
#include "Core/Renderer/PostProcess/ComputeSSR.h"
#include "Core/Renderer/PostProcess/ComputeTAA.h"
#include "Core/Renderer/Shadows/ShadowRenderer.h"
#include "Core/Renderer/Textures/TextureLibrary.h"
#include "Core/Renderer/Upscaling/FSRUpscaler.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <mutex>
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
        // Skinning matrices for every skeletal draw command, indexed by
        // DrawCommand::BoneOffset. Copied by value: the render thread must not
        // read poses the sim thread is still writing.
        std::vector<Math::Mat4> BoneMatrices;
        Math::Mat4 View{1.0f};
        Math::Mat4 Projection{1.0f};
        Math::Mat4 ViewProjection{1.0f};
        Math::Vec3 CameraPosition{0.0f};
        float TimeSeconds = 0.0f;
        uint64_t FrameIndex = 0;
        // Copied from the scene's active post-process volume, or left at its
        // defaults when the scene has none.
        // The sub-pixel offset folded into Projection this frame. Carried rather
        // than recomputed, so the temporal passes reproject against exactly what
        // was rendered.
        Math::Vec2 ProjectionJitter{0.0f};
        ECS::PostProcessSettings PostProcess{};
        bool PostProcessEnabled = true;

        void Clear() {
            DrawCommands.clear();
            DirectionalLights.clear();
            PointLights.clear();
            SpotLights.clear();
            BoneMatrices.clear();
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
        uint32_t PostProcessPasses = 0;
        bool PostProcessActive = false;
        uint32_t DirectionalLights = 0;
        uint32_t PointLights = 0;
        uint32_t SpotLights = 0;
        uint32_t PunctualLights = 0;
        bool GPUDrivenActive = false;
        uint32_t SkinnedInstances = 0;
        uint32_t SkinnedVertices = 0;
        uint32_t SkinnedDropped = 0;
        bool MotionBlurActive = false;
        float MotionBlurStrength = 0.0f;
        bool DepthOfFieldActive = false;
        float DepthOfFieldBlurPixels = 0.0f;
        bool SSREnabled = false;
        bool SSRActive = false;
        uint32_t SSRSteps = 0;
        bool TAAEnabled = false;
        bool TAAActive = false;
        float TAAFeedback = 0.0f;
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

        // Writes whatever the frame last produced to a PNG. Nothing could see the
        // rendered image at all before this, which left every visual feature
        // unverifiable except by a human looking at the window.
        // `target` names which image to write: empty or "final" for whatever the
        // frame ended on, or a G-buffer name. Being able to look at velocity or
        // normals directly is the difference between debugging a pass and
        // guessing at it from the lit result.
        bool CaptureToFile(const std::string& path, std::string& error,
                           const std::string& target = std::string());

        const ComputeDepthOfField& GetDepthOfField() const { return m_DepthOfField; }
        const ComputeMotionBlur& GetMotionBlur() const { return m_MotionBlur; }

        ComputeSSR& GetSSR() { return m_SSR; }
        const ComputeSSR& GetSSR() const { return m_SSR; }

        ComputeTAA& GetTAA() { return m_TAA; }
        const ComputeTAA& GetTAA() const { return m_TAA; }

        // What the editor viewport samples. Valid after the first RecordOffscreen.
        VkImageView GetViewportImageView() const;
        VkSampler GetViewportSampler() const { return m_LinearSampler; }

        ShadowRenderer& GetShadowRenderer() { return m_Shadows; }
        ClusteredLightCuller& GetLightCuller() { return m_LightCuller; }
        ComputeBloom& GetBloom() { return m_Bloom; }
        ComputeSSAO& GetSSAO() { return m_SSAO; }
        // Renderer-owned rather than per-frame: nothing in the ECS drives these
        // yet, and a tool that sets them should not have its change overwritten
        // by the next frame packet.
        ECS::PostProcessSettings& GetPostProcessSettings() { return m_PostProcessSettings; }
        const ECS::PostProcessSettings& GetPostProcessSettings() const { return m_PostProcessSettings; }
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
            // Unjittered, both of them: velocity must describe where geometry
            // moved, not the sub-pixel offset the frame was rendered with.
            Math::Mat4 UnjitteredViewProjection;
            Math::Mat4 PreviousViewProjection;
            Math::Vec4 CameraPosition;
            Math::Vec4 Resolution;
            Math::Vec4 AmbientColor;
            // Sky radiance for the environment term, taken from the GI settings
            // so there is one sky in the frame rather than two that disagree.
            Math::Vec4 SkyColor;
            Math::Vec4 DirectionalDirection[4];
            Math::Vec4 DirectionalColor[4];
            Math::UVec4 LightCounts;
            float TimeSeconds;
            float Pad0;
            float Pad1;
            float Pad2;
            // Punctual lights moved to a storage buffer culled per froxel; only
            // the shadow matrices, which are few and indexed by slot, stay here.
            Math::Mat4 SpotShadowMatrix[kMaxSpotShadows];
            // Six faces per point light, laid out flat. x of PointBaseTile is
            // the first atlas tile of the cube, y is -1 when the light has none.
            Math::Mat4 PointShadowMatrix[kMaxPointShadows * kCubeFaceCount];
            // xyz = grid dimensions, w = tile size
            Math::Vec4 LightGridParams;
            // x = z-slice scale, y = z-slice bias, z = near, w = far
            Math::Vec4 LightDepthParams;
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
            // The graph hash alone is not the key. Alpha mode, its cutoff, and
            // double-sidedness all change the pipeline without touching a single
            // node, and keying on the graph meant editing any of them silently
            // kept the old pipeline.
            uint64_t StateHash = 0;
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
        // Poses every skinned instance the GPU scene queued this frame.
        void DispatchSkinning(VkCommandBuffer cmd);

        void UpdateSceneUniforms(const FrameRenderData& frame);

        RHI::VulkanContext* m_Context = nullptr;

        VkRenderPass m_ScenePassClear = VK_NULL_HANDLE;
        VkRenderPass m_ScenePassLoad = VK_NULL_HANDLE;
        VkFramebuffer m_SceneFramebuffer = VK_NULL_HANDLE;

        RHI::GpuImage m_SceneColor{};    // direct lighting, HDR
        RHI::GpuImage m_SceneAlbedo{};   // rgb base colour, a roughness
        RHI::GpuImage m_SceneNormal{};
        // Screen-space motion in UV units, written by the geometry pass.
        RHI::GpuImage m_SceneVelocity{};   // rgb world normal (encoded), a metallic
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
        GPUSkinningPass m_Skinning;
        ShadowRenderer m_Shadows;
        ClusteredLightCuller m_LightCuller;
        ComputeBloom m_Bloom;
        ComputeSSAO m_SSAO;
        ComputeDepthOfField m_DepthOfField;
        ComputeMotionBlur m_MotionBlur;
        ComputeSSR m_SSR;
        ComputeTAA m_TAA;
        ECS::PostProcessSettings m_PostProcessSettings{};
        // False until the chain runs at least once; the upscaler reads the
        // resolved image directly until then.
        bool m_PostProcessRanThisFrame = false;
        // What the upscaler and composite should read this frame.
        VkImageView m_UpscaleSourceView = VK_NULL_HANDLE;
        // Rebuilt each frame from the frame's point and spot lights, already
        // carrying their shadow slots, then uploaded to the cull pass.
        std::vector<GpuPunctualLight> m_PunctualLights;
        DynamicGlobalIllumination m_GI;
        FSRUpscaler m_FSR;

        FrameRenderData m_Frame;
        Math::Mat4 m_PreviousViewProjection{1.0f};
        // Jitter removed: reprojection must not chase the offset it exists to
        // resolve, and the jittered pair differs by a sub-pixel shift every
        // frame.
        Math::Mat4 m_PreviousUnjitteredViewProjection{1.0f};

        // The image the frame ended on, whichever pass produced it, so a capture
        // does not have to re-derive the post chain.
        RHI::GpuImage* m_FinalImage = nullptr;
        // ponytail: one uncontended lock per frame, so a capture cannot read an
        // image while the render thread is mid-transition. Recording the copy
        // inside the frame would avoid it; captures are far too rare to care.
        std::mutex m_CaptureMutex;
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
