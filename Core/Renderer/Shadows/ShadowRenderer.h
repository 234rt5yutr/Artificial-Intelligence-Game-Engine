#pragma once

// Cascaded shadow maps for the directional light.
//
// `Core/Renderer/ShadowPass.*` declares a single shadow map over the RHI's
// render-pass and pipeline-state interfaces — the ones `VulkanDevice`
// deliberately leaves unimplemented — so it could never render anything, and
// nothing called it. It has no cascades either, which caps a directional shadow
// at either coarse-and-far or sharp-and-tiny.
//
// This renders for real, through the same route the rest of the frame uses, and
// reuses the GPU scene: the cascade depth passes cull with the same cluster
// shader as the main view and draw from the same merged geometry arena, so a
// mesh is never resident twice or clustered differently for its own shadow.

#include "Core/Math/Math.h"
#include "Core/RHI/Vulkan/VulkanGpuResources.h"

#include <cstdint>
#include <vector>

namespace Core {
namespace RHI { class VulkanContext; }

namespace Renderer {

    class GPUScene;
    struct FrameRenderData;

    // Four is the usual ceiling: past that the split ratios stop buying
    // resolution and each extra cascade is another full depth pass.
    inline constexpr uint32_t kMaxShadowCascades = 4;

    // Spot lights that can have a shadow tile in the atlas at once. Lights past
    // this are lit but unshadowed rather than dropped, so the scene degrades
    // instead of flickering as the selection changes.
    inline constexpr uint32_t kMaxSpotShadows = 8;

    // Shadow views culled per frame: cascades occupy [0, kMaxShadowCascades),
    // spot atlas tiles follow. Each view owns a draw buffer and a descriptor set.
    inline constexpr uint32_t kMaxShadowViews = kMaxShadowCascades + kMaxSpotShadows;

    struct ShadowSettings {
        bool Enabled = true;
        uint32_t CascadeCount = 4;
        uint32_t CascadeResolution = 2048;
        // Beyond this the directional light stops casting. Cascades are fitted
        // to this rather than the camera's far plane, which is usually far
        // enough away to waste every cascade on empty sky.
        float MaxShadowDistance = 150.0f;
        // 0 = uniform splits, 1 = fully logarithmic. Practical split scheme.
        float CascadeSplitLambda = 0.85f;
        float DepthBias = 1.5f;
        float SlopeBias = 2.5f;
        // Offset along the surface normal before projecting into light space.
        // Fixes acne on surfaces at a grazing angle to the light, which depth
        // bias alone cannot without introducing peter-panning.
        float NormalBias = 0.035f;
        // Half-width of the PCF kernel in texels: 1 gives 3x3 hardware-filtered
        // taps, 2 gives 5x5.
        uint32_t PcfRadius = 1;
        // Snap each cascade's centre to a texel grid so the shadow does not
        // shimmer as the camera moves.
        bool StabilizeCascades = true;

        // Punctual (spot) shadows share one atlas. One tile per light, sized
        // atlas / tilesPerRow.
        bool SpotShadowsEnabled = true;
        uint32_t AtlasResolution = 4096;
        uint32_t AtlasTilesPerRow = 3;   // 3x3 = 9 tiles, one spare over the cap
    };

    struct ShadowStats {
        bool Active = false;
        uint32_t CascadeCount = 0;
        uint32_t Resolution = 0;
        uint32_t ClusterSlots = 0;
        uint32_t VisibleClusters[kMaxShadowCascades] = {0, 0, 0, 0};
        float CascadeSplits[kMaxShadowCascades] = {0.0f, 0.0f, 0.0f, 0.0f};
        int32_t ShadowLightIndex = -1;
        uint32_t SpotShadowCount = 0;
        uint32_t AtlasResolution = 0;
        uint32_t AtlasTileSize = 0;
    };

    // One spot light's slot in the shadow atlas, as the lit shader needs it.
    struct SpotShadowSlot {
        Math::Mat4 ViewProjection{1.0f};
        // Which light in FrameRenderData's spot list this belongs to.
        int32_t LightIndex = -1;
        uint32_t TileX = 0;
        uint32_t TileY = 0;
    };

    class ShadowRenderer {
    public:
        ShadowRenderer() = default;
        ~ShadowRenderer();

        ShadowRenderer(const ShadowRenderer&) = delete;
        ShadowRenderer& operator=(const ShadowRenderer&) = delete;

        bool Initialize(RHI::VulkanContext* context, uint32_t maxClusterSlots);
        void Shutdown();
        bool IsInitialized() const { return m_Context != nullptr && m_Pipeline != VK_NULL_HANDLE; }

        // Picks the shadow-casting directional light and fits the cascades to
        // this frame's camera. Returns false when nothing casts shadows, in
        // which case Render() is a no-op and the shader is told to skip.
        bool BeginFrame(const FrameRenderData& frame, GPUScene& scene);

        // Culls and renders one depth pass per cascade. Leaves the array in
        // DEPTH_STENCIL_READ_ONLY_OPTIMAL.
        void Render(VkCommandBuffer cmd, GPUScene& scene);

        VkImageView GetCascadeArrayView() const { return m_CascadeArray.View; }
        VkImageView GetAtlasView() const { return m_Atlas.View; }
        const std::vector<SpotShadowSlot>& GetSpotSlots() const { return m_SpotSlots; }
        // xy = atlas size in texels, z = tile size in texels, w = 1 / atlas size.
        Math::Vec4 GetAtlasParams() const;
        // Comparison sampler, so a single tap is already 2x2 hardware PCF.
        VkSampler GetComparisonSampler() const { return m_ComparisonSampler; }

        const Math::Mat4* GetCascadeMatrices() const { return m_CascadeMatrices; }
        const Math::Vec4& GetCascadeSplits() const { return m_CascadeSplitsView; }
        int32_t GetShadowLightIndex() const { return m_ShadowLightIndex; }
        float GetTexelSize() const;

        // Rebuilds the cascade array after a resolution or cascade-count change.
        bool ApplySettings();

        ShadowSettings& GetSettings() { return m_Settings; }
        const ShadowSettings& GetSettings() const { return m_Settings; }
        const ShadowStats& GetStats() const { return m_Stats; }

    private:
        struct CullUniforms {
            Math::Mat4 View;
            Math::Mat4 Projection;
            Math::Mat4 ViewProjection;
            Math::Vec4 FrustumPlanes[6];
            Math::Vec4 CameraPosition;
            Math::Vec4 HZBParams;
            Math::Vec4 Flags;
            Math::UVec4 Counts;
        };

        bool CreateCullResources(uint32_t maxClusterSlots);
        bool CreateRenderPass();
        bool CreatePipeline();
        bool CreateCascadeTargets();
        void DestroyCascadeTargets();
        bool CreateAtlasTargets();
        void DestroyAtlasTargets();
        void FitSpotShadows(const FrameRenderData& frame);
        // Shared by the cascade and atlas passes: writes one view's cull
        // descriptors and dispatches it.
        void DispatchCull(VkCommandBuffer cmd, GPUScene& scene, uint32_t viewIndex,
                          const Math::Mat4& viewProjection, const Math::Vec3& viewDirection);
        void FitCascades(const FrameRenderData& frame, const Math::Vec3& lightDirection);

        RHI::VulkanContext* m_Context = nullptr;
        ShadowSettings m_Settings{};
        ShadowStats m_Stats{};

        RHI::GpuImage m_CascadeArray{};
        std::vector<VkImageView> m_LayerViews;
        std::vector<VkFramebuffer> m_Framebuffers;
        RHI::GpuImage m_Atlas{};
        VkFramebuffer m_AtlasFramebuffer = VK_NULL_HANDLE;
        std::vector<SpotShadowSlot> m_SpotSlots;
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_Pipeline = VK_NULL_HANDLE;
        VkSampler m_ComparisonSampler = VK_NULL_HANDLE;
        VkSampler m_DummySampler = VK_NULL_HANDLE;

        VkDescriptorSetLayout m_DrawSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_CullSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet m_DrawSet = VK_NULL_HANDLE;
        VkDescriptorSet m_CullSets[kMaxShadowViews] = {};

        RHI::ComputePipeline m_CullPipeline{};
        RHI::GpuBuffer m_DrawBuffers[kMaxShadowViews] = {};
        RHI::GpuBuffer m_CullUniforms[kMaxShadowViews] = {};
        RHI::GpuBuffer m_RetestFlags{};
        RHI::GpuBuffer m_Counters{};
        // The cull shader always binds an HZB sampler; shadow views never read
        // it, but the descriptor still has to point at a live image.
        RHI::GpuImage m_DummyHZB{};

        Math::Mat4 m_CascadeMatrices[kMaxShadowCascades] = {
            Math::Mat4(1.0f), Math::Mat4(1.0f), Math::Mat4(1.0f), Math::Mat4(1.0f)
        };
        Math::Vec4 m_CascadeSplitsView{0.0f};
        Math::Vec3 m_LightDirection{0.0f, -1.0f, 0.0f};
        int32_t m_ShadowLightIndex = -1;
        uint32_t m_ClusterSlots = 0;
        uint32_t m_MaxClusterSlots = 0;
        bool m_TargetsDirty = false;
    };

} // namespace Renderer
} // namespace Core
