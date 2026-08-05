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
#include <unordered_map>
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

    // Point lights get six tiles each, one per cube face. The cap is set by the
    // uniform block: six mat4 per light means two lights already cost more than
    // all the other light data put together.
    inline constexpr uint32_t kMaxPointShadows = 2;
    inline constexpr uint32_t kCubeFaceCount = 6;

    // Shadow views culled per frame: cascades occupy [0, kMaxShadowCascades),
    // spot atlas tiles follow. Each view owns a draw buffer and a descriptor set.
    // Cascade pages. A 2048 cascade at 128px pages is a 16x16 page grid, which
    // is fine enough that one moving object dirties a handful of pages rather
    // than the whole map.
    inline constexpr uint32_t kShadowPageSize = 128;
    inline constexpr uint32_t kMaxShadowPagesPerSide = 64;

    inline constexpr uint32_t kMaxShadowViews =
        kMaxShadowCascades + kMaxSpotShadows + kMaxPointShadows * kCubeFaceCount;

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

        // Redraw only the cascade pages whose contents changed. A static scene
        // then costs nothing after its first frame.
        bool CacheCascades = true;
        // Past this fraction of dirty pages, redrawing the whole cascade beats
        // issuing a scissored draw per dirty region.
        float CascadeFullRedrawFraction = 0.4f;
        // Hard cap on scissor rectangles per cascade; more than this and the
        // per-rect draw overhead outweighs the pages saved.
        uint32_t MaxCascadeDirtyRects = 16;

        // Punctual (spot) shadows share one atlas. One tile per light, sized
        // atlas / tilesPerRow.
        bool SpotShadowsEnabled = true;
        bool PointShadowsEnabled = true;
        uint32_t AtlasResolution = 4096;
        // 4x4 = 16 tiles: one point light's cube (6) plus ten spots, or two
        // cubes plus four spots.
        uint32_t AtlasTilesPerRow = 4;
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
        uint32_t PointShadowCount = 0;
        uint32_t AtlasTilesUsed = 0;
        uint32_t AtlasTilesTotal = 0;
        uint32_t AtlasResolution = 0;
        uint32_t AtlasTileSize = 0;

        // Cascade page cache, for the frame just recorded.
        uint32_t PagesPerSide = 0;
        uint32_t DirtyPages = 0;
        uint32_t TotalPages = 0;
        uint32_t CascadesRedrawn = 0;
        uint32_t CascadesSkipped = 0;
        uint32_t DirtyRects = 0;
        uint32_t MovedInstances = 0;
        // Monotonic. Per-frame counters are useless for observing a cache from
        // outside the engine: by the time a tool call reads them, the frame that
        // did the work is a hundred frames gone.
        uint64_t TotalCascadeRedraws = 0;
        uint64_t TotalCascadeSkips = 0;
        uint64_t TotalOccluderChanges = 0;
    };

    // One spot light's slot in the shadow atlas, as the lit shader needs it.
    struct SpotShadowSlot {
        Math::Mat4 ViewProjection{1.0f};
        // Which light in FrameRenderData's spot list this belongs to.
        int32_t LightIndex = -1;
        uint32_t Tile = 0;   // flat tile index into the atlas grid
    };

    // A point light's cube: six contiguous tiles, one per face, in the order
    // +X, -X, +Y, -Y, +Z, -Z. The shader picks a face by major axis and indexes
    // BaseTile + face, so the run has to stay contiguous.
    struct PointShadowSlot {
        Math::Mat4 FaceViewProjection[kCubeFaceCount];
        int32_t LightIndex = -1;
        uint32_t BaseTile = 0;
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
        const std::vector<PointShadowSlot>& GetPointSlots() const { return m_PointSlots; }
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

        // Forces every cascade page dirty for one frame. The failure mode of a
        // page cache is a stale page - a shadow that should have moved and did
        // not - so there has to be a way to rule the cache in or out from a tool
        // call rather than by rebuilding.
        void InvalidateCache() { m_ForceFullRedraw = true; }

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
        // Allocates atlas tiles for both punctual kinds. A point light needs a
        // contiguous run of six; a light that does not fit is skipped whole,
        // because a partial cube reads as fully lit on its missing faces.
        void FitPunctualShadows(const FrameRenderData& frame);
        // Shared by the cascade and atlas passes: writes one view's cull
        // descriptors and dispatches it.
        void DispatchCull(VkCommandBuffer cmd, GPUScene& scene, uint32_t viewIndex,
                          const Math::Mat4& viewProjection, const Math::Vec3& viewDirection);
        void FitCascades(const FrameRenderData& frame, const Math::Vec3& lightDirection);

        // Diffs this frame's occluders against last frame's and marks the pages
        // their shadows occupy. Instances have no stable id across frames, so
        // identity comes from hashing (mesh, transform): anything that appears
        // or disappears from that set moved, and both its old and new footprints
        // have to be redrawn.
        void UpdateCascadePages(const FrameRenderData& frame, GPUScene& scene);
        void MarkPagesForBounds(uint32_t cascade, const Math::Vec3& center, float radius);
        // Collapses the dirty page grid into row-run rectangles for scissoring.
        void BuildDirtyRects(uint32_t cascade, std::vector<VkRect2D>& outRects) const;

        struct OccluderBounds {
            Math::Vec3 Center{0.0f};
            float Radius = 0.0f;
        };

        struct CascadePageState {
            Math::Mat4 LastMatrix{0.0f};
            std::vector<uint8_t> DirtyPages;
            bool EverDrawn = false;
        };

        RHI::VulkanContext* m_Context = nullptr;
        ShadowSettings m_Settings{};
        ShadowStats m_Stats{};

        RHI::GpuImage m_CascadeArray{};
        std::vector<VkImageView> m_LayerViews;
        std::vector<VkFramebuffer> m_Framebuffers;
        RHI::GpuImage m_Atlas{};
        VkFramebuffer m_AtlasFramebuffer = VK_NULL_HANDLE;
        std::vector<SpotShadowSlot> m_SpotSlots;
        std::vector<PointShadowSlot> m_PointSlots;

        CascadePageState m_CascadePages[kMaxShadowCascades];
        std::unordered_map<uint64_t, OccluderBounds> m_PreviousOccluders;
        std::unordered_map<uint64_t, OccluderBounds> m_CurrentOccluders;
        uint32_t m_PagesPerSide = 0;
        bool m_ForceFullRedraw = true;
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        // Identical but for the load op: a partial cascade redraw must keep the
        // pages it is not touching.
        VkRenderPass m_RenderPassLoad = VK_NULL_HANDLE;
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
