#include "GPUSkinningPass.h"

#include "Core/Log.h"
#include "Core/RHI/Vulkan/VulkanContext.h"
#include "Core/Renderer/GPUDriven/GPUScene.h"

#include <algorithm>
#include <cstring>

namespace Core {
namespace Renderer {

    namespace {

        constexpr uint32_t kGroupSize = 64;
        // Enough for a crowd; past this the bone buffer is larger than the
        // geometry it poses.
        constexpr uint32_t kMaxSkinnedInstances = 256;

        // Writes posed vertices into the merged arena. The output layout is
        // exactly Renderer::Vertex, because everything downstream - clusters,
        // culling, the depth passes - reads the arena as static geometry and
        // must not need to know this ran.
        const char* kSkinningShader = R"GLSL(
#version 450

layout(local_size_x = 64) in;

// Scalar members, not vec3: in std430 a vec3 aligns to 16 bytes, which would
// silently insert padding this buffer does not have. Renderer::SkinnedVertex is
// tightly packed at 80 bytes and Renderer::Vertex at 48, and scalars are the
// only way to say that in GLSL.
struct SkinnedVertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    float tx, ty, tz, tw;
    uint b0, b1, b2, b3;
    float w0, w1, w2, w3;
};

struct OutputVertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    float tx, ty, tz, tw;
};

layout(std430, binding = 0) readonly buffer Source { SkinnedVertex sourceVertices[]; };
layout(std430, binding = 1) buffer Target { OutputVertex targetVertices[]; };
layout(std430, binding = 2) readonly buffer Bones { mat4 boneMatrices[]; };
layout(binding = 3) uniform Params {
    uvec4 params;   // x source offset, y target offset, z vertex count, w bone offset
} sk;

void main() {
    uint index = gl_GlobalInvocationID.x;
    if (index >= sk.params.z) {
        return;
    }

    SkinnedVertex source = sourceVertices[sk.params.x + index];

    // Linear blend skinning. Weights are normalised at build time, but a mesh
    // imported from elsewhere may not be, so they are renormalised here rather
    // than trusting the asset.
    vec4 rawWeights = vec4(source.w0, source.w1, source.w2, source.w3);
    uvec4 boneIndices = uvec4(source.b0, source.b1, source.b2, source.b3);
    float weightSum = rawWeights.x + rawWeights.y + rawWeights.z + rawWeights.w;
    vec4 weights = weightSum > 1e-5 ? rawWeights / weightSum : vec4(1.0, 0.0, 0.0, 0.0);

    mat4 skin = mat4(0.0);
    for (int i = 0; i < 4; ++i) {
        if (weights[i] <= 0.0) {
            continue;
        }
        skin += boneMatrices[sk.params.w + boneIndices[i]] * weights[i];
    }

    vec3 position = (skin * vec4(source.px, source.py, source.pz, 1.0)).xyz;
    // Normals and tangents are directions: the translation column must not
    // apply, or every normal drifts with the bone's position.
    mat3 skinRotation = mat3(skin);
    vec3 normal = normalize(skinRotation * vec3(source.nx, source.ny, source.nz));
    vec3 tangent = normalize(skinRotation * vec3(source.tx, source.ty, source.tz));

    OutputVertex result;
    result.px = position.x; result.py = position.y; result.pz = position.z;
    result.nx = normal.x;   result.ny = normal.y;   result.nz = normal.z;
    result.u = source.u;    result.v = source.v;
    result.tx = tangent.x;  result.ty = tangent.y;  result.tz = tangent.z;
    result.tw = source.tw;
    targetVertices[sk.params.y + index] = result;
}
)GLSL";

    } // namespace

    GPUSkinningPass::~GPUSkinningPass() {
        Shutdown();
    }

    bool GPUSkinningPass::Initialize(RHI::VulkanContext* context, uint32_t maxBones) {
        if (!context || context->GetDevice() == VK_NULL_HANDLE) {
            return false;
        }
        Shutdown();
        m_Context = context;
        m_MaxBones = std::max(maxBones, 64u);

        if (!CreatePipeline()) {
            Shutdown();
            return false;
        }
        ENGINE_CORE_INFO("GPU skinning ready ({} bone matrices, {} instances per frame)",
                         m_MaxBones, kMaxSkinnedInstances);
        return true;
    }

    bool GPUSkinningPass::CreatePipeline() {
        VkDevice device = m_Context->GetDevice();

        m_SetLayout = RHI::CreateComputeSetLayout(device, {
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,   // skinned source
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,   // merged arena
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,   // bone matrices
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        });
        if (m_SetLayout == VK_NULL_HANDLE) {
            return false;
        }

        m_Pipeline = RHI::CreateComputePipeline(device, m_Context->GetPipelineCache(),
                                                kSkinningShader, "gpu_skinning",
                                                {m_SetLayout}, 0);
        if (!m_Pipeline.IsValid()) {
            return false;
        }

        if (!RHI::CreateGpuBuffer(m_Context->GetAllocator(),
                                  static_cast<VkDeviceSize>(m_MaxBones) * sizeof(Math::Mat4),
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, m_BoneBuffer)) {
            return false;
        }

        const VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kMaxSkinnedInstances * 3},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxSkinnedInstances},
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = kMaxSkinnedInstances;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
            return false;
        }

        std::vector<VkDescriptorSetLayout> layouts(kMaxSkinnedInstances, m_SetLayout);
        m_Sets.assign(kMaxSkinnedInstances, VK_NULL_HANDLE);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_DescriptorPool;
        allocInfo.descriptorSetCount = kMaxSkinnedInstances;
        allocInfo.pSetLayouts = layouts.data();
        if (vkAllocateDescriptorSets(device, &allocInfo, m_Sets.data()) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("GPUSkinningPass: descriptor set allocation failed");
            return false;
        }

        m_Uniforms.resize(kMaxSkinnedInstances);
        for (uint32_t i = 0; i < kMaxSkinnedInstances; ++i) {
            if (!RHI::CreateGpuBuffer(m_Context->GetAllocator(), sizeof(SkinningUniforms),
                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, m_Uniforms[i])) {
                return false;
            }
        }
        return true;
    }

    void GPUSkinningPass::BeginFrame() {
        m_FrameBones.clear();
        m_Dispatches.clear();
        m_Stats = SkinningStats{};
    }

    uint32_t GPUSkinningPass::PushBones(const Math::Mat4* matrices, uint32_t count) {
        if (!matrices || count == 0) {
            return UINT32_MAX;
        }
        if (m_FrameBones.size() + count > m_MaxBones) {
            ++m_Stats.DroppedInstances;
            return UINT32_MAX;
        }
        const uint32_t offset = static_cast<uint32_t>(m_FrameBones.size());
        m_FrameBones.insert(m_FrameBones.end(), matrices, matrices + count);
        return offset;
    }

    void GPUSkinningPass::PushDispatch(const SkinningDispatch& dispatch) {
        if (m_Dispatches.size() >= kMaxSkinnedInstances) {
            ++m_Stats.DroppedInstances;
            return;
        }
        m_Dispatches.push_back(dispatch);
    }

    void GPUSkinningPass::Render(VkCommandBuffer cmd, GPUScene& scene) {
        m_Stats.Active = false;
        m_Stats.Instances = static_cast<uint32_t>(m_Dispatches.size());
        m_Stats.Bones = static_cast<uint32_t>(m_FrameBones.size());
        if (!IsInitialized() || m_Dispatches.empty() ||
            scene.GetSkinnedSourceBuffer() == VK_NULL_HANDLE) {
            return;
        }

        if (m_BoneBuffer.Mapped && !m_FrameBones.empty()) {
            std::memcpy(m_BoneBuffer.Mapped, m_FrameBones.data(),
                        m_FrameBones.size() * sizeof(Math::Mat4));
        }

        VkDevice device = m_Context->GetDevice();
        uint32_t totalVertices = 0;

        for (std::size_t i = 0; i < m_Dispatches.size(); ++i) {
            const SkinningDispatch& dispatch = m_Dispatches[i];
            if (dispatch.VertexCount == 0) {
                continue;
            }

            SkinningUniforms uniforms{};
            uniforms.Params = Math::UVec4(dispatch.SourceVertexOffset, dispatch.TargetVertexOffset,
                                          dispatch.VertexCount, dispatch.BoneOffset);
            if (m_Uniforms[i].Mapped) {
                std::memcpy(m_Uniforms[i].Mapped, &uniforms, sizeof(uniforms));
            }

            VkDescriptorBufferInfo bufferInfos[3]{};
            bufferInfos[0] = {scene.GetSkinnedSourceBuffer(), 0, VK_WHOLE_SIZE};
            bufferInfos[1] = {scene.GetVertexBuffer(), 0, VK_WHOLE_SIZE};
            bufferInfos[2] = {m_BoneBuffer.Buffer, 0, VK_WHOLE_SIZE};
            VkDescriptorBufferInfo uniformInfo{m_Uniforms[i].Buffer, 0, sizeof(SkinningUniforms)};

            VkWriteDescriptorSet writes[4]{};
            for (uint32_t binding = 0; binding < 3; ++binding) {
                writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[binding].dstSet = m_Sets[i];
                writes[binding].dstBinding = binding;
                writes[binding].descriptorCount = 1;
                writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[binding].pBufferInfo = &bufferInfos[binding];
            }
            writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[3].dstSet = m_Sets[i];
            writes[3].dstBinding = 3;
            writes[3].descriptorCount = 1;
            writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[3].pBufferInfo = &uniformInfo;
            vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_Pipeline.Pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_Pipeline.Layout,
                                    0, 1, &m_Sets[i], 0, nullptr);
            vkCmdDispatch(cmd, (dispatch.VertexCount + kGroupSize - 1) / kGroupSize, 1, 1);
            totalVertices += dispatch.VertexCount;
        }

        // The arena is read as a vertex buffer by every geometry and shadow pass
        // this frame, so the writes have to be visible to vertex input before
        // any of them run.
        RHI::BufferBarrier(cmd, scene.GetVertexBuffer(),
                           VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_VERTEX_INPUT_BIT);

        m_Stats.Vertices = totalVertices;
        m_Stats.Active = totalVertices > 0;
    }

    void GPUSkinningPass::Shutdown() {
        if (!m_Context) {
            return;
        }
        VkDevice device = m_Context->GetDevice();
        VmaAllocator allocator = m_Context->GetAllocator();
        vkDeviceWaitIdle(device);

        for (auto& buffer : m_Uniforms) {
            RHI::DestroyGpuBuffer(allocator, buffer);
        }
        m_Uniforms.clear();
        RHI::DestroyGpuBuffer(allocator, m_BoneBuffer);
        RHI::DestroyComputePipeline(device, m_Pipeline);

        if (m_DescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
            m_DescriptorPool = VK_NULL_HANDLE;
        }
        m_Sets.clear();
        if (m_SetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, m_SetLayout, nullptr);
            m_SetLayout = VK_NULL_HANDLE;
        }
        m_FrameBones.clear();
        m_Dispatches.clear();
        m_Context = nullptr;
    }

} // namespace Renderer
} // namespace Core
