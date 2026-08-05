#pragma once

// Clustered (froxel) light culling.
//
// The lit shader iterated fixed uniform arrays capped at 16 point and 8 spot
// lights, and every pixel tested every light. That is both a hard ceiling and
// quadratic waste - and the ceiling is what makes "many shadow-casting lights"
// impossible, because you cannot have many of something you cannot have sixteen
// of.
//
// `ForwardPlusLightData` declared the right constants for this over RHI buffers
// that carry no compute path, so nothing ever culled. Those constants are
// honoured here, in the Vulkan path the frame actually uses.
//
// The grid is `ceil(w/16) x ceil(h/16) x 32`, exponential in Z so the slices
// track perspective rather than screen depth. One compute thread per froxel
// tests every light against the froxel's view-space bounds and appends the
// survivors to a shared index list; the fragment shader then iterates only its
// own froxel's list.

#include "Core/Math/Math.h"
#include "Core/RHI/Vulkan/VulkanGpuResources.h"

#include <cstdint>
#include <vector>

namespace Core {
namespace RHI { class VulkanContext; }

namespace Renderer {

    struct FrameRenderData;

    // Matches the constants ForwardPlusLightData declared.
    inline constexpr uint32_t kLightTileSize = 16;
    inline constexpr uint32_t kLightGridSlices = 32;
    inline constexpr uint32_t kMaxLightsPerCluster = 256;
    inline constexpr uint32_t kMaxClusteredLights = 4096;

    // One punctual light as the cull pass and the lit shader both see it.
    // Directional lights are not in here: there are at most a handful, they
    // affect every froxel, and culling them would cost more than it saves.
    struct alignas(16) GpuPunctualLight {
        Math::Vec4 PositionRadius{0.0f};      // xyz world position, w radius
        Math::Vec4 ColorIntensity{0.0f};      // rgb colour, w intensity
        Math::Vec4 DirectionCosInner{0.0f};   // xyz spot direction, w cos(inner)
        // x = cos(outer), y = type (0 point, 1 spot), z = shadow slot (-1 none),
        // w = cube matrix base for point shadows
        Math::Vec4 ConeShadowParams{0.0f};
    };
    static_assert(sizeof(GpuPunctualLight) == 64, "GpuPunctualLight must match the shader layout");

    struct ClusteredLightStats {
        uint32_t GridX = 0;
        uint32_t GridY = 0;
        uint32_t GridZ = 0;
        uint32_t ClusterCount = 0;
        uint32_t LightCount = 0;
        // Written by the cull pass; read one frame late, like the cull counters.
        uint32_t VisibleAssignments = 0;
        uint32_t MaxLightsInCluster = 0;
        uint32_t OverflowedClusters = 0;
        bool Active = false;
    };

    class ClusteredLightCuller {
    public:
        ClusteredLightCuller() = default;
        ~ClusteredLightCuller();

        ClusteredLightCuller(const ClusteredLightCuller&) = delete;
        ClusteredLightCuller& operator=(const ClusteredLightCuller&) = delete;

        bool Initialize(RHI::VulkanContext* context);
        void Shutdown();
        bool IsInitialized() const { return m_Context != nullptr && m_CullPipeline.IsValid(); }

        // Rebuilds the grid for a new render resolution. The froxel bounds are
        // rebuilt on the next Render, since they depend on the projection.
        bool Resize(uint32_t renderWidth, uint32_t renderHeight);

        // Uploads this frame's light list. `lights` is built by the caller from
        // the frame's point and spot lights, already carrying shadow slots.
        void SetLights(const std::vector<GpuPunctualLight>& lights);

        // Rebuilds froxel bounds when the projection changed, then culls.
        void Render(VkCommandBuffer cmd, const FrameRenderData& frame);

        VkBuffer GetLightBuffer() const { return m_LightBuffer.Buffer; }
        VkBuffer GetLightGridBuffer() const { return m_LightGrid.Buffer; }
        VkBuffer GetLightIndexBuffer() const { return m_LightIndices.Buffer; }

        // xyz = grid dimensions, w = tile size. The shader needs both to map a
        // fragment to its froxel.
        Math::Vec4 GetGridParams() const;
        // x = z-slice scale, y = z-slice bias, z = near, w = far.
        Math::Vec4 GetDepthParams() const { return m_DepthParams; }

        void RefreshStats();
        const ClusteredLightStats& GetStats() const { return m_Stats; }

    private:
        struct ClusterUniforms {
            Math::Mat4 InverseProjection;
            Math::Mat4 View;
            Math::Vec4 GridParams;    // xyz grid, w tile size
            Math::Vec4 DepthParams;   // x scale, y bias, z near, w far
            Math::Vec4 ScreenParams;  // xy render size, zw 1/size
            Math::UVec4 Counts;       // x light count, y cluster count
        };

        bool CreatePipelines();
        bool CreateBuffers();
        void DestroyBuffers();

        RHI::VulkanContext* m_Context = nullptr;

        RHI::ComputePipeline m_BuildPipeline{};
        RHI::ComputePipeline m_CullPipeline{};
        VkDescriptorSetLayout m_SetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet m_Set = VK_NULL_HANDLE;

        RHI::GpuBuffer m_ClusterBounds{};
        RHI::GpuBuffer m_LightBuffer{};
        RHI::GpuBuffer m_LightGrid{};
        RHI::GpuBuffer m_LightIndices{};
        RHI::GpuBuffer m_Counters{};
        RHI::GpuBuffer m_Uniforms{};

        uint32_t m_GridX = 0;
        uint32_t m_GridY = 0;
        uint32_t m_ClusterCount = 0;
        uint32_t m_LightCount = 0;
        uint32_t m_RenderWidth = 0;
        uint32_t m_RenderHeight = 0;

        Math::Vec4 m_DepthParams{0.0f};
        // Froxel bounds only change when the projection does, so they are not
        // rebuilt every frame.
        Math::Mat4 m_BoundsProjection{0.0f};
        bool m_BoundsValid = false;

        ClusteredLightStats m_Stats{};
    };

} // namespace Renderer
} // namespace Core
