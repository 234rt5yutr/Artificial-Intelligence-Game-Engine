#pragma once

// GPU skinning into the merged geometry arena.
//
// Skeletal meshes did not render at all. `GPUScene` stores one vertex layout,
// and a skinned vertex is a different size, so `EnsureResident` rejected them
// and `SceneRenderer` skipped them with a warning. The engine could animate a
// skeleton and never draw it.
//
// The fix is not a second draw path. Each skinned instance gets a slice of the
// same merged vertex arena, and a compute pass writes posed `Vertex` data into
// it every frame. From the culler's point of view the result is ordinary static
// geometry: same arena, same clusters, same indirect draws, same shadow passes.
//
// The cost is that a skinned instance owns arena space per instance rather than
// per mesh - two copies of a character are two slices, because they hold
// different poses.

#include "Core/Math/Math.h"
#include "Core/RHI/Vulkan/VulkanGpuResources.h"

#include <cstdint>
#include <vector>

namespace Core {
namespace RHI { class VulkanContext; }

namespace Renderer {

    class GPUScene;

    // Per-instance skinning work for one frame.
    struct SkinningDispatch {
        uint32_t SourceVertexOffset = 0;   // into the skinned source buffer
        uint32_t TargetVertexOffset = 0;   // into the merged arena
        uint32_t VertexCount = 0;
        uint32_t BoneOffset = 0;           // into the bone matrix buffer
        uint32_t BoneCount = 0;
    };

    struct SkinningStats {
        uint32_t Instances = 0;
        uint32_t Vertices = 0;
        uint32_t Bones = 0;
        uint32_t DroppedInstances = 0;
        bool Active = false;
    };

    class GPUSkinningPass {
    public:
        GPUSkinningPass() = default;
        ~GPUSkinningPass();

        GPUSkinningPass(const GPUSkinningPass&) = delete;
        GPUSkinningPass& operator=(const GPUSkinningPass&) = delete;

        bool Initialize(RHI::VulkanContext* context, uint32_t maxBones);
        void Shutdown();
        bool IsInitialized() const { return m_Context != nullptr && m_Pipeline.IsValid(); }

        // Resets the frame's bone and dispatch lists.
        void BeginFrame();
        // Appends one instance's bone matrices; returns the offset they landed
        // at, or UINT32_MAX when the buffer is full.
        uint32_t PushBones(const Math::Mat4* matrices, uint32_t count);
        void PushDispatch(const SkinningDispatch& dispatch);

        // Uploads the frame's bones and runs one dispatch per skinned instance.
        // Must be recorded before any pass that reads the arena.
        void Render(VkCommandBuffer cmd, GPUScene& scene);

        const SkinningStats& GetStats() const { return m_Stats; }

    private:
        struct SkinningUniforms {
            Math::UVec4 Params;   // x source offset, y target offset, z vertex count, w bone offset
        };

        bool CreatePipeline();

        RHI::VulkanContext* m_Context = nullptr;
        RHI::ComputePipeline m_Pipeline{};
        VkDescriptorSetLayout m_SetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        // One set per skinned instance in flight; sharing one would rebind
        // before the earlier dispatch had consumed it.
        std::vector<VkDescriptorSet> m_Sets;
        std::vector<RHI::GpuBuffer> m_Uniforms;

        RHI::GpuBuffer m_BoneBuffer{};
        std::vector<Math::Mat4> m_FrameBones;
        std::vector<SkinningDispatch> m_Dispatches;
        uint32_t m_MaxBones = 0;

        SkinningStats m_Stats{};
    };

} // namespace Renderer
} // namespace Core
