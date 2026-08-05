#include "GPUDrivenCuller.h"

#include "Core/Log.h"
#include "Core/RHI/Vulkan/VulkanContext.h"
#include "Core/Renderer/GPUDriven/ClusterCullShader.h"
#include "Core/Renderer/GPUDriven/GPUScene.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Core {
namespace Renderer {

    namespace {

        constexpr uint32_t kCullGroupSize = 64;
        constexpr uint32_t kHZBGroupSize = 8;
        constexpr uint32_t kCounterSlots = 8;

        // Gribb-Hartmann plane extraction, normalised so the sphere test is a
        // plain signed distance.
        void ExtractFrustumPlanes(const Math::Mat4& viewProjection, Math::Vec4 outPlanes[6]) {
            const Math::Mat4& m = viewProjection;
            // glm is column-major: m[column][row].
            for (int i = 0; i < 3; ++i) {
                outPlanes[i * 2 + 0] = Math::Vec4(m[0][3] + m[0][i], m[1][3] + m[1][i],
                                                  m[2][3] + m[2][i], m[3][3] + m[3][i]);
                outPlanes[i * 2 + 1] = Math::Vec4(m[0][3] - m[0][i], m[1][3] - m[1][i],
                                                  m[2][3] - m[2][i], m[3][3] - m[3][i]);
            }
            for (int i = 0; i < 6; ++i) {
                const float length = glm::length(Math::Vec3(outPlanes[i]));
                if (length > 1e-6f) {
                    outPlanes[i] /= length;
                }
            }
        }

        // Shared with the shadow views; see ClusterCullShader.h.
        const char* kCullShader = kClusterCullShaderSource;

        const char* kHZBCopyShader = R"GLSL(
#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D uDepth;
layout(binding = 1, r32f) uniform writeonly image2D uHZBMip0;

layout(push_constant) uniform Push {
    ivec4 sizes;   // xy = hzb mip0 size, zw = depth size
} pc;

void main() {
    ivec2 target = ivec2(gl_GlobalInvocationID.xy);
    if (target.x >= pc.sizes.x || target.y >= pc.sizes.y) {
        return;
    }

    // Half resolution: reduce the 2x2 depth footprint with max, so the HZB is a
    // conservative "farthest depth in this region" pyramid.
    ivec2 source = target * 2;
    ivec2 limit = pc.sizes.zw - ivec2(1);
    float d0 = texelFetch(uDepth, min(source + ivec2(0, 0), limit), 0).r;
    float d1 = texelFetch(uDepth, min(source + ivec2(1, 0), limit), 0).r;
    float d2 = texelFetch(uDepth, min(source + ivec2(0, 1), limit), 0).r;
    float d3 = texelFetch(uDepth, min(source + ivec2(1, 1), limit), 0).r;
    imageStore(uHZBMip0, target, vec4(max(max(d0, d1), max(d2, d3))));
}
)GLSL";

        const char* kHZBReduceShader = R"GLSL(
#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0, r32f) uniform readonly image2D uSrc;
layout(binding = 1, r32f) uniform writeonly image2D uDst;

layout(push_constant) uniform Push {
    ivec4 sizes;   // xy = dst size, zw = src size
} pc;

void main() {
    ivec2 target = ivec2(gl_GlobalInvocationID.xy);
    if (target.x >= pc.sizes.x || target.y >= pc.sizes.y) {
        return;
    }

    ivec2 source = target * 2;
    ivec2 limit = pc.sizes.zw - ivec2(1);
    float d0 = imageLoad(uSrc, min(source + ivec2(0, 0), limit)).r;
    float d1 = imageLoad(uSrc, min(source + ivec2(1, 0), limit)).r;
    float d2 = imageLoad(uSrc, min(source + ivec2(0, 1), limit)).r;
    float d3 = imageLoad(uSrc, min(source + ivec2(1, 1), limit)).r;
    imageStore(uDst, target, vec4(max(max(d0, d1), max(d2, d3))));
}
)GLSL";

    } // namespace

    GPUDrivenCuller::~GPUDrivenCuller() {
        Shutdown();
    }

    bool GPUDrivenCuller::Initialize(RHI::VulkanContext* context, uint32_t maxClusterSlots) {
        if (!context || context->GetDevice() == VK_NULL_HANDLE) {
            return false;
        }
        Shutdown();
        m_Context = context;
        m_MaxClusterSlots = maxClusterSlots;

        if (!CreatePipelines() || !CreateBuffers(maxClusterSlots)) {
            Shutdown();
            return false;
        }

        m_HZBSampler = RHI::CreateClampedSampler(context->GetDevice(), VK_FILTER_NEAREST);
        if (m_HZBSampler == VK_NULL_HANDLE) {
            Shutdown();
            return false;
        }

        ENGINE_CORE_INFO("GPU-driven culler ready ({} cluster slots, two-phase HZB occlusion)",
                         maxClusterSlots);
        return true;
    }

    bool GPUDrivenCuller::CreatePipelines() {
        VkDevice device = m_Context->GetDevice();

        m_CullSetLayout = RHI::CreateComputeSetLayout(device, {
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          // instances
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          // clusters
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          // instance offsets
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          // draw commands
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          // retest flags
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          // counters
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  // HZB
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,          // cull params
        });
        m_HZBCopySetLayout = RHI::CreateComputeSetLayout(device, {
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        });
        m_HZBReduceSetLayout = RHI::CreateComputeSetLayout(device, {
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        });
        if (m_CullSetLayout == VK_NULL_HANDLE || m_HZBCopySetLayout == VK_NULL_HANDLE ||
            m_HZBReduceSetLayout == VK_NULL_HANDLE) {
            return false;
        }

        VkPipelineCache cache = m_Context->GetPipelineCache();
        m_CullPipeline = RHI::CreateComputePipeline(device, cache, kCullShader, "cluster_cull",
                                                    {m_CullSetLayout}, 0);
        m_HZBCopyPipeline = RHI::CreateComputePipeline(device, cache, kHZBCopyShader, "hzb_copy",
                                                       {m_HZBCopySetLayout}, sizeof(int32_t) * 4);
        m_HZBReducePipeline = RHI::CreateComputePipeline(device, cache, kHZBReduceShader, "hzb_reduce",
                                                         {m_HZBReduceSetLayout}, sizeof(int32_t) * 4);
        return m_CullPipeline.IsValid() && m_HZBCopyPipeline.IsValid() && m_HZBReducePipeline.IsValid();
    }

    bool GPUDrivenCuller::CreateBuffers(uint32_t maxClusterSlots) {
        VmaAllocator allocator = m_Context->GetAllocator();
        VkDevice device = m_Context->GetDevice();

        const VkDeviceSize drawBytes = static_cast<VkDeviceSize>(maxClusterSlots) * kDrawCommandStride;
        const VkBufferUsageFlags drawUsage =
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        const bool buffersOk =
            RHI::CreateGpuBuffer(allocator, drawBytes, drawUsage, false, m_EarlyDraws) &&
            RHI::CreateGpuBuffer(allocator, drawBytes, drawUsage, false, m_LateDraws) &&
            RHI::CreateGpuBuffer(allocator, static_cast<VkDeviceSize>(maxClusterSlots) * sizeof(uint32_t),
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                 false, m_RetestFlags) &&
            RHI::CreateGpuBuffer(allocator, kCounterSlots * sizeof(uint32_t),
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                 true, m_Counters) &&
            RHI::CreateGpuBuffer(allocator, sizeof(CullUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                 true, m_EarlyUniforms) &&
            RHI::CreateGpuBuffer(allocator, sizeof(CullUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                 true, m_LateUniforms);
        if (!buffersOk) {
            return false;
        }

        // Two cull sets, one HZB copy set, and one reduce set per mip. 16 mips
        // covers any resolution a swapchain can present.
        const VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 40},
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        // HZB reduce sets are freed and reallocated whenever the render resolution changes.
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 24;
        poolInfo.poolSizeCount = 4;
        poolInfo.pPoolSizes = poolSizes;
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("GPUDrivenCuller: descriptor pool creation failed");
            return false;
        }

        const VkDescriptorSetLayout cullLayouts[] = {m_CullSetLayout, m_CullSetLayout};
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_DescriptorPool;
        allocInfo.descriptorSetCount = 2;
        allocInfo.pSetLayouts = cullLayouts;
        VkDescriptorSet cullSets[2]{};
        if (vkAllocateDescriptorSets(device, &allocInfo, cullSets) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("GPUDrivenCuller: cull descriptor set allocation failed");
            return false;
        }
        m_EarlySet = cullSets[0];
        m_LateSet = cullSets[1];

        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_HZBCopySetLayout;
        if (vkAllocateDescriptorSets(device, &allocInfo, &m_HZBCopySet) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("GPUDrivenCuller: HZB descriptor set allocation failed");
            return false;
        }
        return true;
    }

    bool GPUDrivenCuller::Resize(uint32_t renderWidth, uint32_t renderHeight) {
        if (!m_Context || renderWidth == 0 || renderHeight == 0) {
            return false;
        }
        if (renderWidth == m_RenderWidth && renderHeight == m_RenderHeight && m_HZB.IsValid()) {
            return true;
        }

        vkDeviceWaitIdle(m_Context->GetDevice());
        DestroyHZB();
        m_RenderWidth = renderWidth;
        m_RenderHeight = renderHeight;
        return CreateHZB(std::max(1u, renderWidth / 2), std::max(1u, renderHeight / 2));
    }

    bool GPUDrivenCuller::CreateHZB(uint32_t width, uint32_t height) {
        VkDevice device = m_Context->GetDevice();

        RHI::GpuImageDesc desc{};
        desc.Width = width;
        desc.Height = height;
        desc.Format = VK_FORMAT_R32_SFLOAT;
        desc.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        desc.MipLevels = 1 + static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(std::max(width, height)))));
        desc.CreateMipViews = true;
        desc.DebugName = "HZB";

        if (!RHI::CreateGpuImage(device, m_Context->GetAllocator(), desc, m_HZB)) {
            return false;
        }
        m_HZBValid = false;
        m_Stats.HZBMipCount = m_HZB.MipLevels;

        // One reduce set per mip transition (mip N-1 -> mip N).
        const uint32_t reduceSetCount = m_HZB.MipLevels > 1 ? m_HZB.MipLevels - 1 : 0;
        m_HZBReduceSets.assign(reduceSetCount, VK_NULL_HANDLE);
        if (reduceSetCount > 0) {
            std::vector<VkDescriptorSetLayout> layouts(reduceSetCount, m_HZBReduceSetLayout);
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = m_DescriptorPool;
            allocInfo.descriptorSetCount = reduceSetCount;
            allocInfo.pSetLayouts = layouts.data();
            if (vkAllocateDescriptorSets(device, &allocInfo, m_HZBReduceSets.data()) != VK_SUCCESS) {
                ENGINE_CORE_ERROR("GPUDrivenCuller: HZB reduce descriptor allocation failed ({} mips)",
                                  m_HZB.MipLevels);
                m_HZBReduceSets.clear();
                return false;
            }

            for (uint32_t mip = 1; mip < m_HZB.MipLevels; ++mip) {
                VkDescriptorImageInfo srcInfo{};
                srcInfo.imageView = m_HZB.MipViews[mip - 1];
                srcInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                VkDescriptorImageInfo dstInfo{};
                dstInfo.imageView = m_HZB.MipViews[mip];
                dstInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

                VkWriteDescriptorSet writes[2]{};
                writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[0].dstSet = m_HZBReduceSets[mip - 1];
                writes[0].dstBinding = 0;
                writes[0].descriptorCount = 1;
                writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                writes[0].pImageInfo = &srcInfo;
                writes[1] = writes[0];
                writes[1].dstBinding = 1;
                writes[1].pImageInfo = &dstInfo;
                vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
            }
        }
        return true;
    }

    void GPUDrivenCuller::DestroyHZB() {
        if (!m_Context || !m_HZB.IsValid()) {
            m_HZBReduceSets.clear();
            return;
        }
        if (!m_HZBReduceSets.empty()) {
            vkFreeDescriptorSets(m_Context->GetDevice(), m_DescriptorPool,
                                 static_cast<uint32_t>(m_HZBReduceSets.size()), m_HZBReduceSets.data());
            m_HZBReduceSets.clear();
        }
        RHI::DestroyGpuImage(m_Context->GetDevice(), m_Context->GetAllocator(), m_HZB);
        m_HZBValid = false;
    }

    void GPUDrivenCuller::Shutdown() {
        if (!m_Context) {
            return;
        }
        VkDevice device = m_Context->GetDevice();
        VmaAllocator allocator = m_Context->GetAllocator();

        DestroyHZB();
        if (m_HZBSampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, m_HZBSampler, nullptr);
            m_HZBSampler = VK_NULL_HANDLE;
        }
        RHI::DestroyComputePipeline(device, m_CullPipeline);
        RHI::DestroyComputePipeline(device, m_HZBCopyPipeline);
        RHI::DestroyComputePipeline(device, m_HZBReducePipeline);

        for (VkDescriptorSetLayout* layout : {&m_CullSetLayout, &m_HZBCopySetLayout, &m_HZBReduceSetLayout}) {
            if (*layout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device, *layout, nullptr);
                *layout = VK_NULL_HANDLE;
            }
        }
        if (m_DescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
            m_DescriptorPool = VK_NULL_HANDLE;
            m_EarlySet = VK_NULL_HANDLE;
            m_LateSet = VK_NULL_HANDLE;
            m_HZBCopySet = VK_NULL_HANDLE;
        }

        RHI::DestroyGpuBuffer(allocator, m_EarlyDraws);
        RHI::DestroyGpuBuffer(allocator, m_LateDraws);
        RHI::DestroyGpuBuffer(allocator, m_RetestFlags);
        RHI::DestroyGpuBuffer(allocator, m_Counters);
        RHI::DestroyGpuBuffer(allocator, m_EarlyUniforms);
        RHI::DestroyGpuBuffer(allocator, m_LateUniforms);

        m_RenderWidth = 0;
        m_RenderHeight = 0;
        m_Context = nullptr;
    }

    void GPUDrivenCuller::WriteCullDescriptors(VkDescriptorSet set, VkBuffer drawBuffer, VkBuffer uniformBuffer) {
        // The scene buffers are recreated only on GPUScene teardown, but writing
        // them per frame keeps this correct if they ever are, and a descriptor
        // write is far cheaper than the dispatch it configures.
        VkDescriptorBufferInfo bufferInfos[6]{};
        bufferInfos[0] = {m_SceneInstances, 0, VK_WHOLE_SIZE};
        bufferInfos[1] = {m_SceneClusters, 0, VK_WHOLE_SIZE};
        bufferInfos[2] = {m_SceneOffsets, 0, VK_WHOLE_SIZE};
        bufferInfos[3] = {drawBuffer, 0, VK_WHOLE_SIZE};
        bufferInfos[4] = {m_RetestFlags.Buffer, 0, VK_WHOLE_SIZE};
        bufferInfos[5] = {m_Counters.Buffer, 0, VK_WHOLE_SIZE};

        VkDescriptorImageInfo hzbInfo{};
        hzbInfo.sampler = m_HZBSampler;
        hzbInfo.imageView = m_HZB.View;
        hzbInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorBufferInfo uniformInfo{uniformBuffer, 0, sizeof(CullUniforms)};

        VkWriteDescriptorSet writes[8]{};
        for (uint32_t i = 0; i < 6; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &bufferInfos[i];
        }
        writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet = set;
        writes[6].dstBinding = 6;
        writes[6].descriptorCount = 1;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[6].pImageInfo = &hzbInfo;
        writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[7].dstSet = set;
        writes[7].dstBinding = 7;
        writes[7].descriptorCount = 1;
        writes[7].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[7].pBufferInfo = &uniformInfo;

        vkUpdateDescriptorSets(m_Context->GetDevice(), 8, writes, 0, nullptr);
    }

    void GPUDrivenCuller::BeginFrame(VkCommandBuffer cmd, GPUScene& scene, const GpuCullView& view) {
        if (!IsInitialized() || !m_HZB.IsValid()) {
            m_ClusterSlotsThisFrame = 0;
            return;
        }

        m_SceneInstances = scene.GetInstanceBuffer();
        m_SceneClusters = scene.GetClusterBuffer();
        m_SceneOffsets = scene.GetInstanceOffsetBuffer();
        m_ClusterSlotsThisFrame = scene.GetFrameClusterSlotCount();
        m_Stats.ClusterSlots = m_ClusterSlotsThisFrame;

        if (m_ClusterSlotsThisFrame == 0 || m_SceneInstances == VK_NULL_HANDLE) {
            return;
        }

        vkCmdFillBuffer(cmd, m_Counters.Buffer, 0, kCounterSlots * sizeof(uint32_t), 0);
        RHI::BufferBarrier(cmd, m_Counters.Buffer, VK_ACCESS_TRANSFER_WRITE_BIT,
                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        CullUniforms uniforms{};
        uniforms.View = view.View;
        uniforms.Projection = view.Projection;
        uniforms.ViewProjection = view.ViewProjection;
        ExtractFrustumPlanes(view.ViewProjection, uniforms.FrustumPlanes);
        uniforms.CameraPosition = Math::Vec4(view.CameraPosition, 1.0f);
        uniforms.HZBParams = Math::Vec4(static_cast<float>(m_HZB.Extent.width),
                                        static_cast<float>(m_HZB.Extent.height),
                                        static_cast<float>(m_HZB.MipLevels), 0.0f);
        // The HZB holds nothing usable until the first BuildHZB, so the first
        // frame runs frustum-only rather than occluding against undefined memory.
        uniforms.Flags = Math::Vec4((m_OcclusionEnabled && m_HZBValid) ? 1.0f : 0.0f,
                                    m_ConeCullingEnabled ? 1.0f : 0.0f, 0.0f, 0.0f);
        uniforms.Counts = Math::UVec4(m_ClusterSlotsThisFrame, scene.GetFrameInstanceCount(), 0, 0);

        if (m_EarlyUniforms.Mapped) {
            std::memcpy(m_EarlyUniforms.Mapped, &uniforms, sizeof(uniforms));
        }
        uniforms.HZBParams.w = 1.0f; // late phase
        if (m_LateUniforms.Mapped) {
            std::memcpy(m_LateUniforms.Mapped, &uniforms, sizeof(uniforms));
        }

        WriteCullDescriptors(m_EarlySet, m_EarlyDraws.Buffer, m_EarlyUniforms.Buffer);
        WriteCullDescriptors(m_LateSet, m_LateDraws.Buffer, m_LateUniforms.Buffer);
    }

    void GPUDrivenCuller::Dispatch(VkCommandBuffer cmd, VkDescriptorSet set, VkBuffer drawBuffer) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_CullPipeline.Pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_CullPipeline.Layout,
                                0, 1, &set, 0, nullptr);
        vkCmdDispatch(cmd, (m_ClusterSlotsThisFrame + kCullGroupSize - 1) / kCullGroupSize, 1, 1);

        RHI::BufferBarrier(cmd, drawBuffer, VK_ACCESS_SHADER_WRITE_BIT,
                           VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);
        RHI::BufferBarrier(cmd, m_RetestFlags.Buffer, VK_ACCESS_SHADER_WRITE_BIT,
                           VK_ACCESS_SHADER_READ_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

    void GPUDrivenCuller::CullEarly(VkCommandBuffer cmd) {
        if (!IsInitialized() || m_ClusterSlotsThisFrame == 0) {
            return;
        }
        Dispatch(cmd, m_EarlySet, m_EarlyDraws.Buffer);
    }

    void GPUDrivenCuller::CullLate(VkCommandBuffer cmd) {
        if (!IsInitialized() || m_ClusterSlotsThisFrame == 0 || !m_TwoPhaseEnabled || !m_HZBValid) {
            return;
        }
        Dispatch(cmd, m_LateSet, m_LateDraws.Buffer);
    }

    void GPUDrivenCuller::BuildHZB(VkCommandBuffer cmd, VkImageView depthView, VkSampler depthSampler) {
        if (!IsInitialized() || !m_HZB.IsValid() || depthView == VK_NULL_HANDLE) {
            return;
        }

        RHI::TransitionImage(cmd, m_HZB, VK_IMAGE_LAYOUT_GENERAL);

        VkDescriptorImageInfo depthInfo{};
        depthInfo.sampler = depthSampler;
        depthInfo.imageView = depthView;
        depthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkDescriptorImageInfo mip0Info{};
        mip0Info.imageView = m_HZB.MipViews.empty() ? m_HZB.View : m_HZB.MipViews[0];
        mip0Info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet writes[2]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m_HZBCopySet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &depthInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = m_HZBCopySet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].pImageInfo = &mip0Info;
        vkUpdateDescriptorSets(m_Context->GetDevice(), 2, writes, 0, nullptr);

        int32_t push[4] = {static_cast<int32_t>(m_HZB.Extent.width),
                           static_cast<int32_t>(m_HZB.Extent.height),
                           static_cast<int32_t>(m_RenderWidth),
                           static_cast<int32_t>(m_RenderHeight)};

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_HZBCopyPipeline.Pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_HZBCopyPipeline.Layout,
                                0, 1, &m_HZBCopySet, 0, nullptr);
        vkCmdPushConstants(cmd, m_HZBCopyPipeline.Layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(push), push);
        vkCmdDispatch(cmd,
                      (m_HZB.Extent.width + kHZBGroupSize - 1) / kHZBGroupSize,
                      (m_HZB.Extent.height + kHZBGroupSize - 1) / kHZBGroupSize, 1);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_HZBReducePipeline.Pipeline);
        uint32_t srcWidth = m_HZB.Extent.width;
        uint32_t srcHeight = m_HZB.Extent.height;
        for (uint32_t mip = 1; mip < m_HZB.MipLevels; ++mip) {
            // Each mip reads the one above it, so they cannot overlap.
            VkMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

            const uint32_t dstWidth = std::max(1u, srcWidth / 2);
            const uint32_t dstHeight = std::max(1u, srcHeight / 2);
            int32_t mipPush[4] = {static_cast<int32_t>(dstWidth), static_cast<int32_t>(dstHeight),
                                  static_cast<int32_t>(srcWidth), static_cast<int32_t>(srcHeight)};

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_HZBReducePipeline.Layout,
                                    0, 1, &m_HZBReduceSets[mip - 1], 0, nullptr);
            vkCmdPushConstants(cmd, m_HZBReducePipeline.Layout, VK_SHADER_STAGE_COMPUTE_BIT,
                               0, sizeof(mipPush), mipPush);
            vkCmdDispatch(cmd,
                          (dstWidth + kHZBGroupSize - 1) / kHZBGroupSize,
                          (dstHeight + kHZBGroupSize - 1) / kHZBGroupSize, 1);

            srcWidth = dstWidth;
            srcHeight = dstHeight;
        }

        RHI::TransitionImage(cmd, m_HZB, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_HZBValid = true;
    }

    void GPUDrivenCuller::RefreshStats() {
        m_Stats.OcclusionEnabled = m_OcclusionEnabled;
        m_Stats.ConeCullingEnabled = m_ConeCullingEnabled;
        m_Stats.TwoPhaseEnabled = m_TwoPhaseEnabled;
        if (!m_Counters.Mapped) {
            return;
        }
        // Read after the in-flight fence has been waited on, so this is the
        // previous frame's completed totals rather than a torn read.
        const auto* counters = static_cast<const uint32_t*>(m_Counters.Mapped);
        m_Stats.VisibleEarly = counters[0];
        m_Stats.VisibleLate = counters[1];
        m_Stats.FrustumCulled = counters[2];
        m_Stats.ConeCulled = counters[3];
        m_Stats.OcclusionCulled = counters[4];
    }

} // namespace Renderer
} // namespace Core
