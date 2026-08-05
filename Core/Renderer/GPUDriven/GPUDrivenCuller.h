#pragma once

// Two-phase GPU-driven cluster culling.
//
// The engine's `FrustumCulling` runs on the CPU over whole meshes. This runs on
// the GPU over clusters, and the CPU never learns the result: the compute pass
// writes `VkDrawIndexedIndirectCommand`s straight into a buffer the draw call
// consumes.
//
// Phase 1 tests every cluster against the frustum, its backface cone, and the
// *previous* frame's hierarchical depth buffer, and draws what survives. The HZB
// is then rebuilt from the depth those draws just produced, and phase 2 re-tests
// only the clusters phase 1 rejected for occlusion. That is what stops a camera
// cut from popping geometry for a frame, and it is the part that separates this
// from a plain single-pass occlusion test.

#include "Core/Math/Math.h"
#include "Core/RHI/Vulkan/VulkanGpuResources.h"

#include <cstdint>
#include <vector>

namespace Core {
namespace RHI { class VulkanContext; }

namespace Renderer {

    class GPUScene;

    struct GpuCullStats {
        uint32_t ClusterSlots = 0;
        uint32_t VisibleEarly = 0;
        uint32_t VisibleLate = 0;
        uint32_t FrustumCulled = 0;
        uint32_t ConeCulled = 0;
        uint32_t OcclusionCulled = 0;
        uint32_t HZBMipCount = 0;
        bool OcclusionEnabled = true;
        bool ConeCullingEnabled = true;
        bool TwoPhaseEnabled = true;
    };

    struct GpuCullView {
        Math::Mat4 View{1.0f};
        Math::Mat4 Projection{1.0f};
        Math::Mat4 ViewProjection{1.0f};
        Math::Vec3 CameraPosition{0.0f};
    };

    class GPUDrivenCuller {
    public:
        GPUDrivenCuller() = default;
        ~GPUDrivenCuller();

        GPUDrivenCuller(const GPUDrivenCuller&) = delete;
        GPUDrivenCuller& operator=(const GPUDrivenCuller&) = delete;

        bool Initialize(RHI::VulkanContext* context, uint32_t maxClusterSlots);
        void Shutdown();
        bool IsInitialized() const { return m_Context != nullptr && m_CullPipeline.IsValid(); }

        // Sizes the HZB for a new render resolution. Safe to call every frame;
        // it only rebuilds when the dimensions actually changed.
        bool Resize(uint32_t renderWidth, uint32_t renderHeight);

        // Zeroes the counters and binds this frame's scene buffers. Must be
        // called before either cull phase.
        void BeginFrame(VkCommandBuffer cmd, GPUScene& scene, const GpuCullView& view);

        // Phase 1: frustum + cone + previous-frame occlusion.
        void CullEarly(VkCommandBuffer cmd);
        // Phase 2: re-test only what phase 1 rejected for occlusion, against the
        // HZB built from this frame's depth. No-op when two-phase is disabled.
        void CullLate(VkCommandBuffer cmd);

        // Rebuilds the HZB pyramid from a depth image already transitioned to
        // SHADER_READ_ONLY. Leaves the HZB in SHADER_READ_ONLY.
        void BuildHZB(VkCommandBuffer cmd, VkImageView depthView, VkSampler depthSampler);

        VkBuffer GetEarlyDrawBuffer() const { return m_EarlyDraws.Buffer; }
        VkBuffer GetLateDrawBuffer() const { return m_LateDraws.Buffer; }
        static constexpr uint32_t kDrawCommandStride = 20; // VkDrawIndexedIndirectCommand

        void SetOcclusionEnabled(bool enabled) { m_OcclusionEnabled = enabled; }
        void SetConeCullingEnabled(bool enabled) { m_ConeCullingEnabled = enabled; }
        void SetTwoPhaseEnabled(bool enabled) { m_TwoPhaseEnabled = enabled; }
        bool IsOcclusionEnabled() const { return m_OcclusionEnabled; }
        bool IsConeCullingEnabled() const { return m_ConeCullingEnabled; }
        bool IsTwoPhaseEnabled() const { return m_TwoPhaseEnabled; }

        // Counters from the frame that has finished on the GPU, so they lag the
        // CPU by up to one frame. Cheap, and nothing depends on them being exact.
        const GpuCullStats& GetStats() const { return m_Stats; }
        void RefreshStats();

    private:
        struct CullUniforms {
            Math::Mat4 View;
            Math::Mat4 Projection;
            Math::Mat4 ViewProjection;
            Math::Vec4 FrustumPlanes[6];
            Math::Vec4 CameraPosition;
            Math::Vec4 HZBParams;   // x=width y=height z=mipCount w=phase
            Math::Vec4 Flags;       // x=occlusion y=cone z=unused w=unused
            Math::UVec4 Counts;     // x=clusterSlots
        };

        bool CreatePipelines();
        bool CreateBuffers(uint32_t maxClusterSlots);
        bool CreateHZB(uint32_t width, uint32_t height);
        void DestroyHZB();
        void WriteCullDescriptors(VkDescriptorSet set, VkBuffer drawBuffer, VkBuffer uniformBuffer);
        void Dispatch(VkCommandBuffer cmd, VkDescriptorSet set, VkBuffer drawBuffer);

        RHI::VulkanContext* m_Context = nullptr;
        uint32_t m_MaxClusterSlots = 0;

        RHI::ComputePipeline m_CullPipeline{};
        RHI::ComputePipeline m_HZBCopyPipeline{};
        RHI::ComputePipeline m_HZBReducePipeline{};

        VkDescriptorSetLayout m_CullSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_HZBCopySetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_HZBReduceSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet m_EarlySet = VK_NULL_HANDLE;
        VkDescriptorSet m_LateSet = VK_NULL_HANDLE;
        VkDescriptorSet m_HZBCopySet = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_HZBReduceSets;

        RHI::GpuBuffer m_EarlyDraws{};
        RHI::GpuBuffer m_LateDraws{};
        RHI::GpuBuffer m_RetestFlags{};
        RHI::GpuBuffer m_Counters{};
        RHI::GpuBuffer m_EarlyUniforms{};
        RHI::GpuBuffer m_LateUniforms{};

        RHI::GpuImage m_HZB{};
        VkSampler m_HZBSampler = VK_NULL_HANDLE;

        // Borrowed from GPUScene for the current frame; the scene owns them.
        VkBuffer m_SceneInstances = VK_NULL_HANDLE;
        VkBuffer m_SceneClusters = VK_NULL_HANDLE;
        VkBuffer m_SceneOffsets = VK_NULL_HANDLE;

        uint32_t m_RenderWidth = 0;
        uint32_t m_RenderHeight = 0;
        uint32_t m_ClusterSlotsThisFrame = 0;
        bool m_HZBValid = false;

        bool m_OcclusionEnabled = true;
        bool m_ConeCullingEnabled = true;
        bool m_TwoPhaseEnabled = true;

        GpuCullStats m_Stats{};
    };

} // namespace Renderer
} // namespace Core
