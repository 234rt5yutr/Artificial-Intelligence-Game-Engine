#include "ClusteredLightCuller.h"

#include "Core/Log.h"
#include "Core/RHI/Vulkan/VulkanContext.h"
#include "Core/Renderer/SceneRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Core {
namespace Renderer {

    namespace {

        constexpr uint32_t kBuildGroupSize = 4;   // 4x4x4 froxels per workgroup
        constexpr uint32_t kCullGroupSize = 64;
        constexpr uint32_t kCounterSlots = 8;

        // Shared declarations. Both passes address the same grid, and a
        // disagreement about the froxel layout between build and cull would show
        // up as lights lit in the wrong part of the screen.
        const char* kClusterCommon = R"GLSL(
#version 450

struct PunctualLight {
    vec4 positionRadius;
    vec4 colorIntensity;
    vec4 directionCosInner;
    vec4 coneShadowParams;
};

struct ClusterBounds {
    vec4 minPoint;
    vec4 maxPoint;
};

layout(std430, binding = 0) buffer Bounds   { ClusterBounds clusters[]; };
layout(std430, binding = 1) readonly buffer Lights { PunctualLight lights[]; };
layout(std430, binding = 2) buffer Grid     { uvec2 lightGrid[]; };
layout(std430, binding = 3) buffer Indices  { uint lightIndices[]; };
layout(std430, binding = 4) buffer Counters { uint counters[]; };
layout(binding = 5) uniform ClusterParams {
    mat4 inverseProjection;
    mat4 view;
    vec4 gridParams;     // xyz grid dimensions, w tile size
    vec4 depthParams;    // x slice scale, y slice bias, z near, w far
    vec4 screenParams;   // xy render size, zw 1/size
    uvec4 counts;        // x light count, y cluster count
} cp;

// Screen pixel -> view space at the near plane, then scaled along the ray to a
// given view depth. Everything the grid does is built on this.
vec3 ScreenToView(vec2 pixel, float viewDepth) {
    vec2 ndc = (pixel * cp.screenParams.zw) * 2.0 - 1.0;
    vec4 clip = vec4(ndc, 1.0, 1.0);
    vec4 view = cp.inverseProjection * clip;
    vec3 direction = view.xyz / max(view.w, 1e-6);
    // The projection was built with a flipped Y for Vulkan, so the recovered
    // ray already points the right way; only the depth scale is applied here.
    return direction * (viewDepth / max(-direction.z, 1e-6));
}
)GLSL";

        const char* kBuildShader = R"GLSL(
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

// One view-space AABB per froxel. Only rebuilt when the projection changes,
// because that is the only thing these depend on.
void main() {
    uvec3 grid = uvec3(cp.gridParams.xyz);
    if (gl_GlobalInvocationID.x >= grid.x ||
        gl_GlobalInvocationID.y >= grid.y ||
        gl_GlobalInvocationID.z >= grid.z) {
        return;
    }

    uint clusterIndex = gl_GlobalInvocationID.x +
                        gl_GlobalInvocationID.y * grid.x +
                        gl_GlobalInvocationID.z * grid.x * grid.y;

    float tileSize = cp.gridParams.w;
    vec2 minPixel = vec2(gl_GlobalInvocationID.xy) * tileSize;
    vec2 maxPixel = minPixel + vec2(tileSize);

    // Exponential slice distribution: uniform slices would spend almost all of
    // them on distant geometry, where a froxel covers a huge volume anyway.
    float nearPlane = cp.depthParams.z;
    float farPlane = cp.depthParams.w;
    float ratio = farPlane / nearPlane;
    float sliceNear = nearPlane * pow(ratio, float(gl_GlobalInvocationID.z) / cp.gridParams.z);
    float sliceFar = nearPlane * pow(ratio, float(gl_GlobalInvocationID.z + 1u) / cp.gridParams.z);

    vec3 corners[8];
    corners[0] = ScreenToView(minPixel, sliceNear);
    corners[1] = ScreenToView(vec2(maxPixel.x, minPixel.y), sliceNear);
    corners[2] = ScreenToView(vec2(minPixel.x, maxPixel.y), sliceNear);
    corners[3] = ScreenToView(maxPixel, sliceNear);
    corners[4] = ScreenToView(minPixel, sliceFar);
    corners[5] = ScreenToView(vec2(maxPixel.x, minPixel.y), sliceFar);
    corners[6] = ScreenToView(vec2(minPixel.x, maxPixel.y), sliceFar);
    corners[7] = ScreenToView(maxPixel, sliceFar);

    vec3 boundsMin = corners[0];
    vec3 boundsMax = corners[0];
    for (int i = 1; i < 8; ++i) {
        boundsMin = min(boundsMin, corners[i]);
        boundsMax = max(boundsMax, corners[i]);
    }

    clusters[clusterIndex].minPoint = vec4(boundsMin, 0.0);
    clusters[clusterIndex].maxPoint = vec4(boundsMax, 0.0);
}
)GLSL";

        const char* kCullShader = R"GLSL(
layout(local_size_x = 64) in;

float DistanceSquaredToAABB(vec3 point, vec3 boundsMin, vec3 boundsMax) {
    vec3 closest = clamp(point, boundsMin, boundsMax);
    vec3 delta = point - closest;
    return dot(delta, delta);
}

void main() {
    uint clusterIndex = gl_GlobalInvocationID.x;
    if (clusterIndex >= cp.counts.y) {
        return;
    }

    ClusterBounds bounds = clusters[clusterIndex];
    uint visible[256];
    uint visibleCount = 0u;

    for (uint i = 0u; i < cp.counts.x && visibleCount < 256u; ++i) {
        PunctualLight light = lights[i];
        float radius = light.positionRadius.w;
        if (radius <= 0.0) {
            continue;
        }

        // The froxel bounds are in view space, so the light has to be too.
        vec3 viewPosition = (cp.view * vec4(light.positionRadius.xyz, 1.0)).xyz;
        if (DistanceSquaredToAABB(viewPosition, bounds.minPoint.xyz, bounds.maxPoint.xyz) >
            radius * radius) {
            continue;
        }

        visible[visibleCount] = i;
        ++visibleCount;
    }

    // One atomic per froxel rather than per light: the list is written as a
    // contiguous run, so a froxel's lights stay together in memory.
    uint offset = 0u;
    if (visibleCount > 0u) {
        offset = atomicAdd(counters[0], visibleCount);
        uint capacity = cp.counts.y * 256u;
        if (offset + visibleCount > capacity) {
            // Out of index list. Drop this froxel's lights rather than writing
            // past the end; the counter records that it happened.
            atomicAdd(counters[2], 1u);
            lightGrid[clusterIndex] = uvec2(0u, 0u);
            return;
        }
        for (uint i = 0u; i < visibleCount; ++i) {
            lightIndices[offset + i] = visible[i];
        }
        atomicMax(counters[1], visibleCount);
    }

    lightGrid[clusterIndex] = uvec2(offset, visibleCount);
}
)GLSL";

    } // namespace

    ClusteredLightCuller::~ClusteredLightCuller() {
        Shutdown();
    }

    bool ClusteredLightCuller::Initialize(RHI::VulkanContext* context) {
        if (!context || context->GetDevice() == VK_NULL_HANDLE) {
            return false;
        }
        Shutdown();
        m_Context = context;

        if (!CreatePipelines()) {
            Shutdown();
            return false;
        }
        ENGINE_CORE_INFO("Clustered light culler ready ({}x{} tiles, {} z-slices, up to {} lights)",
                         kLightTileSize, kLightTileSize, kLightGridSlices, kMaxClusteredLights);
        return true;
    }

    bool ClusteredLightCuller::CreatePipelines() {
        VkDevice device = m_Context->GetDevice();

        m_SetLayout = RHI::CreateComputeSetLayout(device, {
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,   // cluster bounds
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,   // lights
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,   // light grid
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,   // light index list
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,   // counters
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,   // params
        });
        if (m_SetLayout == VK_NULL_HANDLE) {
            return false;
        }

        const std::string buildSource = std::string(kClusterCommon) + kBuildShader;
        const std::string cullSource = std::string(kClusterCommon) + kCullShader;
        VkPipelineCache cache = m_Context->GetPipelineCache();
        m_BuildPipeline = RHI::CreateComputePipeline(device, cache, buildSource,
                                                     "cluster_bounds_build", {m_SetLayout}, 0);
        m_CullPipeline = RHI::CreateComputePipeline(device, cache, cullSource,
                                                    "cluster_light_cull", {m_SetLayout}, 0);
        if (!m_BuildPipeline.IsValid() || !m_CullPipeline.IsValid()) {
            return false;
        }

        const VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
            return false;
        }

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_DescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_SetLayout;
        return vkAllocateDescriptorSets(device, &allocInfo, &m_Set) == VK_SUCCESS;
    }

    bool ClusteredLightCuller::Resize(uint32_t renderWidth, uint32_t renderHeight) {
        if (!m_Context || renderWidth == 0 || renderHeight == 0) {
            return false;
        }
        const uint32_t gridX = (renderWidth + kLightTileSize - 1) / kLightTileSize;
        const uint32_t gridY = (renderHeight + kLightTileSize - 1) / kLightTileSize;
        if (gridX == m_GridX && gridY == m_GridY && m_ClusterBounds.IsValid()) {
            m_RenderWidth = renderWidth;
            m_RenderHeight = renderHeight;
            return true;
        }

        vkDeviceWaitIdle(m_Context->GetDevice());
        DestroyBuffers();

        m_GridX = gridX;
        m_GridY = gridY;
        m_ClusterCount = gridX * gridY * kLightGridSlices;
        m_RenderWidth = renderWidth;
        m_RenderHeight = renderHeight;
        // A resolution change reshapes every froxel, so last frame's bounds are
        // meaningless.
        m_BoundsValid = false;

        m_Stats.GridX = gridX;
        m_Stats.GridY = gridY;
        m_Stats.GridZ = kLightGridSlices;
        m_Stats.ClusterCount = m_ClusterCount;
        return CreateBuffers();
    }

    bool ClusteredLightCuller::CreateBuffers() {
        VmaAllocator allocator = m_Context->GetAllocator();

        const VkBufferUsageFlags storageUsage =
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        const bool ok =
            RHI::CreateGpuBuffer(allocator, static_cast<VkDeviceSize>(m_ClusterCount) * 32,
                                 storageUsage, false, m_ClusterBounds) &&
            RHI::CreateGpuBuffer(allocator,
                                 static_cast<VkDeviceSize>(kMaxClusteredLights) * sizeof(GpuPunctualLight),
                                 storageUsage, true, m_LightBuffer) &&
            RHI::CreateGpuBuffer(allocator, static_cast<VkDeviceSize>(m_ClusterCount) * 8,
                                 storageUsage, false, m_LightGrid) &&
            // Worst case is every froxel full. That is 256 uints per froxel and
            // never actually reached, but sizing for it means the shader's
            // overflow path is a real guard rather than the common case.
            RHI::CreateGpuBuffer(allocator,
                                 static_cast<VkDeviceSize>(m_ClusterCount) * kMaxLightsPerCluster * 4,
                                 storageUsage, false, m_LightIndices) &&
            RHI::CreateGpuBuffer(allocator, kCounterSlots * sizeof(uint32_t), storageUsage, true, m_Counters) &&
            RHI::CreateGpuBuffer(allocator, sizeof(ClusterUniforms),
                                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, m_Uniforms);
        if (!ok) {
            ENGINE_CORE_ERROR("Clustered light culler: buffer allocation failed for {} clusters",
                              m_ClusterCount);
            return false;
        }

        ENGINE_CORE_INFO("Light grid {}x{}x{} = {} froxels ({} MB index list)",
                         m_GridX, m_GridY, kLightGridSlices, m_ClusterCount,
                         (static_cast<uint64_t>(m_ClusterCount) * kMaxLightsPerCluster * 4) / (1024 * 1024));
        return true;
    }

    void ClusteredLightCuller::DestroyBuffers() {
        if (!m_Context) {
            return;
        }
        VmaAllocator allocator = m_Context->GetAllocator();
        RHI::DestroyGpuBuffer(allocator, m_ClusterBounds);
        RHI::DestroyGpuBuffer(allocator, m_LightBuffer);
        RHI::DestroyGpuBuffer(allocator, m_LightGrid);
        RHI::DestroyGpuBuffer(allocator, m_LightIndices);
        RHI::DestroyGpuBuffer(allocator, m_Counters);
        RHI::DestroyGpuBuffer(allocator, m_Uniforms);
    }

    void ClusteredLightCuller::SetLights(const std::vector<GpuPunctualLight>& lights) {
        m_LightCount = std::min<uint32_t>(kMaxClusteredLights, static_cast<uint32_t>(lights.size()));
        m_Stats.LightCount = m_LightCount;
        if (lights.size() > kMaxClusteredLights) {
            ENGINE_CORE_WARN("Punctual light count {} exceeds the {} the grid can hold; "
                             "the surplus is dropped this frame",
                             lights.size(), kMaxClusteredLights);
        }
        if (m_LightBuffer.Mapped && m_LightCount > 0) {
            std::memcpy(m_LightBuffer.Mapped, lights.data(),
                        static_cast<std::size_t>(m_LightCount) * sizeof(GpuPunctualLight));
        }
    }

    Math::Vec4 ClusteredLightCuller::GetGridParams() const {
        return Math::Vec4(static_cast<float>(m_GridX), static_cast<float>(m_GridY),
                          static_cast<float>(kLightGridSlices), static_cast<float>(kLightTileSize));
    }

    void ClusteredLightCuller::Render(VkCommandBuffer cmd, const FrameRenderData& frame) {
        m_Stats.Active = false;
        if (!IsInitialized() || m_ClusterCount == 0 || !m_ClusterBounds.IsValid()) {
            return;
        }

        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        if (!Math::ExtractNearFar(frame.Projection, nearPlane, farPlane)) {
            nearPlane = 0.1f;
            farPlane = 1000.0f;
        }

        // Slice mapping: slice = log(viewZ) * scale + bias, so the shader can
        // recover a froxel from a depth with two multiply-adds.
        const float logRatio = std::log(farPlane / nearPlane);
        const float scale = logRatio > 1e-6f ? static_cast<float>(kLightGridSlices) / logRatio : 1.0f;
        const float bias = -(static_cast<float>(kLightGridSlices) * std::log(nearPlane)) /
                           std::max(logRatio, 1e-6f);
        m_DepthParams = Math::Vec4(scale, bias, nearPlane, farPlane);

        ClusterUniforms uniforms{};
        uniforms.InverseProjection = glm::inverse(frame.Projection);
        uniforms.View = frame.View;
        uniforms.GridParams = GetGridParams();
        uniforms.DepthParams = m_DepthParams;
        uniforms.ScreenParams = Math::Vec4(static_cast<float>(m_RenderWidth),
                                           static_cast<float>(m_RenderHeight),
                                           1.0f / static_cast<float>(std::max(m_RenderWidth, 1u)),
                                           1.0f / static_cast<float>(std::max(m_RenderHeight, 1u)));
        uniforms.Counts = Math::UVec4(m_LightCount, m_ClusterCount, 0, 0);
        if (m_Uniforms.Mapped) {
            std::memcpy(m_Uniforms.Mapped, &uniforms, sizeof(uniforms));
        }

        VkDescriptorBufferInfo bufferInfos[5]{};
        bufferInfos[0] = {m_ClusterBounds.Buffer, 0, VK_WHOLE_SIZE};
        bufferInfos[1] = {m_LightBuffer.Buffer, 0, VK_WHOLE_SIZE};
        bufferInfos[2] = {m_LightGrid.Buffer, 0, VK_WHOLE_SIZE};
        bufferInfos[3] = {m_LightIndices.Buffer, 0, VK_WHOLE_SIZE};
        bufferInfos[4] = {m_Counters.Buffer, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo uniformInfo{m_Uniforms.Buffer, 0, sizeof(ClusterUniforms)};

        VkWriteDescriptorSet writes[6]{};
        for (uint32_t i = 0; i < 5; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = m_Set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &bufferInfos[i];
        }
        writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet = m_Set;
        writes[5].dstBinding = 5;
        writes[5].descriptorCount = 1;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[5].pBufferInfo = &uniformInfo;
        vkUpdateDescriptorSets(m_Context->GetDevice(), 6, writes, 0, nullptr);

        // Froxel bounds depend only on the projection, so they survive camera
        // movement and are rebuilt only when the projection itself changes.
        // The jitter in [2][0]/[2][1] moves every frame and must not count.
        Math::Mat4 comparableProjection = frame.Projection;
        comparableProjection[2][0] = 0.0f;
        comparableProjection[2][1] = 0.0f;
        if (!m_BoundsValid || comparableProjection != m_BoundsProjection) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_BuildPipeline.Pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_BuildPipeline.Layout,
                                    0, 1, &m_Set, 0, nullptr);
            vkCmdDispatch(cmd,
                          (m_GridX + kBuildGroupSize - 1) / kBuildGroupSize,
                          (m_GridY + kBuildGroupSize - 1) / kBuildGroupSize,
                          (kLightGridSlices + kBuildGroupSize - 1) / kBuildGroupSize);

            RHI::BufferBarrier(cmd, m_ClusterBounds.Buffer, VK_ACCESS_SHADER_WRITE_BIT,
                               VK_ACCESS_SHADER_READ_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

            m_BoundsProjection = comparableProjection;
            m_BoundsValid = true;
        }

        vkCmdFillBuffer(cmd, m_Counters.Buffer, 0, kCounterSlots * sizeof(uint32_t), 0);
        RHI::BufferBarrier(cmd, m_Counters.Buffer, VK_ACCESS_TRANSFER_WRITE_BIT,
                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_CullPipeline.Pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_CullPipeline.Layout,
                                0, 1, &m_Set, 0, nullptr);
        vkCmdDispatch(cmd, (m_ClusterCount + kCullGroupSize - 1) / kCullGroupSize, 1, 1);

        // The lit pass reads the grid and index list in the fragment stage.
        RHI::BufferBarrier(cmd, m_LightGrid.Buffer, VK_ACCESS_SHADER_WRITE_BIT,
                           VK_ACCESS_SHADER_READ_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        RHI::BufferBarrier(cmd, m_LightIndices.Buffer, VK_ACCESS_SHADER_WRITE_BIT,
                           VK_ACCESS_SHADER_READ_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        m_Stats.Active = m_LightCount > 0;
    }

    void ClusteredLightCuller::RefreshStats() {
        if (!m_Counters.Mapped) {
            return;
        }
        // Read after the in-flight fence, so these are the previous frame's
        // completed totals rather than a torn read.
        const auto* counters = static_cast<const uint32_t*>(m_Counters.Mapped);
        m_Stats.VisibleAssignments = counters[0];
        m_Stats.MaxLightsInCluster = counters[1];
        m_Stats.OverflowedClusters = counters[2];
    }

    void ClusteredLightCuller::Shutdown() {
        if (!m_Context) {
            return;
        }
        VkDevice device = m_Context->GetDevice();
        vkDeviceWaitIdle(device);

        DestroyBuffers();
        RHI::DestroyComputePipeline(device, m_BuildPipeline);
        RHI::DestroyComputePipeline(device, m_CullPipeline);

        if (m_DescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
            m_DescriptorPool = VK_NULL_HANDLE;
            m_Set = VK_NULL_HANDLE;
        }
        if (m_SetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, m_SetLayout, nullptr);
            m_SetLayout = VK_NULL_HANDLE;
        }
        m_GridX = 0;
        m_GridY = 0;
        m_ClusterCount = 0;
        m_BoundsValid = false;
        m_Context = nullptr;
    }

} // namespace Renderer
} // namespace Core
