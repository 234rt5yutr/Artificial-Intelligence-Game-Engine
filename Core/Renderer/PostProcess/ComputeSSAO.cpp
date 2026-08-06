#include "ComputeSSAO.h"

#include "Core/ECS/Components/PostProcessComponent.h"
#include "Core/Log.h"
#include "Core/RHI/Vulkan/VulkanContext.h"

#include <algorithm>
#include <cstring>

namespace Core {
namespace Renderer {

    namespace {

        constexpr uint32_t kGroupSize = 8;

        const char* kSSAOCommon = R"GLSL(
#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D uDepth;
layout(binding = 1) uniform sampler2D uNormal;
layout(binding = 2) uniform sampler2D uSource;
layout(binding = 3, rgba8) uniform writeonly image2D uTarget;
layout(binding = 4) uniform SSAOParams {
    mat4 view;
    mat4 projection;
    mat4 inverseViewProjection;
    vec4 resolution;   // xy size, zw 1/size
    vec4 params;       // x radius, y bias, z intensity, w kernel size
    vec4 blurParams;   // x horizontal, y depth sigma, z frame index
} ao;

vec3 WorldFromDepth(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = ao.inverseViewProjection * clip;
    return world.xyz / max(world.w, 1e-6);
}

float LinearViewDepth(vec2 uv) {
    float depth = texture(uDepth, uv).r;
    if (depth >= 1.0) {
        return -1.0;
    }
    vec3 world = WorldFromDepth(uv, depth);
    return -(ao.view * vec4(world, 1.0)).z;
}
)GLSL";

        const char* kOcclusionShader = R"GLSL(
float Hash(vec3 seed) {
    return fract(sin(dot(seed, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
}

// Cosine-weighted hemisphere around the normal, with samples pulled toward the
// centre so the kernel is denser close to the shaded point - that is where
// contact occlusion actually lives.
vec3 KernelSample(vec3 normal, int index, int count, float jitter) {
    float u = (float(index) + jitter) / float(max(count, 1));
    float phi = 6.2831853 * fract(u * 7.0 + jitter);
    float cosTheta = sqrt(1.0 - u);
    float sinTheta = sqrt(u);

    vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);
    vec3 direction = tangent * (cos(phi) * sinTheta) + bitangent * (sin(phi) * sinTheta) +
                     normal * cosTheta;

    float scale = mix(0.15, 1.0, u * u);
    return direction * scale;
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (pixel.x >= int(ao.resolution.x) || pixel.y >= int(ao.resolution.y)) {
        return;
    }

    vec2 uv = (vec2(pixel) + 0.5) * ao.resolution.zw;
    float depth = texture(uDepth, uv).r;
    if (depth >= 1.0) {
        // Sky: unoccluded, or the horizon picks up a dark rim.
        imageStore(uTarget, pixel, vec4(1.0));
        return;
    }

    vec3 worldPos = WorldFromDepth(uv, depth);
    vec3 normal = normalize(texture(uNormal, uv).xyz * 2.0 - 1.0);
    float centerDepth = -(ao.view * vec4(worldPos, 1.0)).z;

    float radius = ao.params.x;
    float bias = ao.params.y;
    int kernelSize = int(ao.params.w);
    float jitter = Hash(vec3(pixel, ao.blurParams.z));

    float occlusion = 0.0;
    int counted = 0;

    for (int i = 0; i < kernelSize; ++i) {
        vec3 offset = KernelSample(normal, i, kernelSize, jitter) * radius;
        vec3 samplePos = worldPos + offset;

        vec4 clip = ao.projection * ao.view * vec4(samplePos, 1.0);
        if (clip.w <= 1e-5) {
            continue;
        }
        vec2 sampleUV = (clip.xy / clip.w) * 0.5 + 0.5;
        if (any(lessThan(sampleUV, vec2(0.0))) || any(greaterThan(sampleUV, vec2(1.0)))) {
            continue;
        }

        float sampleDepth = -(ao.view * vec4(samplePos, 1.0)).z;
        float sceneDepth = LinearViewDepth(sampleUV);
        if (sceneDepth < 0.0) {
            continue;
        }
        ++counted;

        // Range check: a distant occluder in front of the sample is a different
        // surface, not something touching this one. Without it every silhouette
        // grows a dark halo.
        float rangeCheck = smoothstep(0.0, 1.0, radius / max(abs(centerDepth - sceneDepth), 1e-4));
        if (sceneDepth < sampleDepth - bias) {
            occlusion += rangeCheck;
        }
    }

    float result = counted > 0 ? 1.0 - (occlusion / float(counted)) * ao.params.z : 1.0;
    imageStore(uTarget, pixel, vec4(clamp(result, 0.0, 1.0)));
}
)GLSL";

        const char* kBlurShader = R"GLSL(
// Depth-aware separable blur. A plain blur bleeds occlusion across silhouettes,
// which is exactly where AO is most visible and most wrong.
void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (pixel.x >= int(ao.resolution.x) || pixel.y >= int(ao.resolution.y)) {
        return;
    }

    vec2 uv = (vec2(pixel) + 0.5) * ao.resolution.zw;
    // Not named `step`: that shadows the GLSL builtin. `sample` is reserved
    // outright and will not compile at all.
    vec2 stepUV = ao.blurParams.x > 0.5 ? vec2(ao.resolution.z, 0.0) : vec2(0.0, ao.resolution.w);

    float centerDepth = LinearViewDepth(uv);
    float sum = 0.0;
    float weightSum = 0.0;

    for (int i = -3; i <= 3; ++i) {
        vec2 sampleUV = uv + stepUV * float(i);
        float occlusionSample = texture(uSource, sampleUV).r;

        float weight = exp(-float(i * i) / 8.0);
        if (centerDepth > 0.0) {
            float sampleDepth = LinearViewDepth(sampleUV);
            if (sampleDepth > 0.0) {
                float depthDelta = abs(centerDepth - sampleDepth);
                weight *= exp(-depthDelta * depthDelta / max(ao.blurParams.y, 1e-4));
            }
        }

        sum += occlusionSample * weight;
        weightSum += weight;
    }

    imageStore(uTarget, pixel, vec4(weightSum > 1e-5 ? sum / weightSum : 1.0));
}
)GLSL";

    } // namespace

    ComputeSSAO::~ComputeSSAO() {
        Shutdown();
    }

    bool ComputeSSAO::Initialize(RHI::VulkanContext* context) {
        if (!context || context->GetDevice() == VK_NULL_HANDLE) {
            return false;
        }
        Shutdown();
        m_Context = context;

        if (!CreatePipelines()) {
            Shutdown();
            return false;
        }
        m_Sampler = RHI::CreateClampedSampler(context->GetDevice(), VK_FILTER_LINEAR);
        if (m_Sampler == VK_NULL_HANDLE) {
            Shutdown();
            return false;
        }
        ENGINE_CORE_INFO("Compute SSAO ready (hemisphere kernel + depth-aware blur)");
        return true;
    }

    bool ComputeSSAO::CreatePipelines() {
        VkDevice device = m_Context->GetDevice();

        m_SetLayout = RHI::CreateComputeSetLayout(device, {
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,   // depth
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,   // normal
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,   // source (blur input)
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,            // target
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        });
        if (m_SetLayout == VK_NULL_HANDLE) {
            return false;
        }

        VkPipelineCache cache = m_Context->GetPipelineCache();
        const std::string common = kSSAOCommon;
        m_OcclusionPipeline = RHI::CreateComputePipeline(device, cache, common + kOcclusionShader,
                                                         "ssao_occlusion", {m_SetLayout}, 0);
        m_BlurPipeline = RHI::CreateComputePipeline(device, cache, common + kBlurShader,
                                                    "ssao_blur", {m_SetLayout}, 0);
        if (!m_OcclusionPipeline.IsValid() || !m_BlurPipeline.IsValid()) {
            return false;
        }

        const VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 9},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 3;
        poolInfo.poolSizeCount = 3;
        poolInfo.pPoolSizes = poolSizes;
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
            return false;
        }

        VkDescriptorSetLayout layouts[3] = {m_SetLayout, m_SetLayout, m_SetLayout};
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_DescriptorPool;
        allocInfo.descriptorSetCount = 3;
        allocInfo.pSetLayouts = layouts;
        if (vkAllocateDescriptorSets(device, &allocInfo, m_Sets) != VK_SUCCESS) {
            return false;
        }

        for (uint32_t i = 0; i < 3; ++i) {
            if (!RHI::CreateGpuBuffer(m_Context->GetAllocator(), sizeof(SSAOUniforms),
                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, m_Uniforms[i])) {
                return false;
            }
        }
        return true;
    }

    bool ComputeSSAO::Resize(uint32_t renderWidth, uint32_t renderHeight) {
        if (!m_Context) {
            return false;
        }
        const uint32_t width = std::max(1u, renderWidth / 2);
        const uint32_t height = std::max(1u, renderHeight / 2);
        if (width == m_Width && height == m_Height && m_Occlusion.IsValid()) {
            return true;
        }
        vkDeviceWaitIdle(m_Context->GetDevice());
        DestroyTargets();
        return CreateTargets(width, height);
    }

    bool ComputeSSAO::CreateTargets(uint32_t width, uint32_t height) {
        RHI::GpuImageDesc desc{};
        desc.Width = width;
        desc.Height = height;
        // Occlusion is one number, but R8_UNORM is not a guaranteed storage
        // image format - RGBA8_UNORM is one of the few that are. The extra
        // channels are wasted; a device-feature query for the narrow format
        // would be worse than the bandwidth.
        desc.Format = VK_FORMAT_R8G8B8A8_UNORM;
        desc.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        desc.DebugName = "SSAOOcclusion";

        VkDevice device = m_Context->GetDevice();
        VmaAllocator allocator = m_Context->GetAllocator();
        if (!RHI::CreateGpuImage(device, allocator, desc, m_Occlusion)) {
            return false;
        }
        desc.DebugName = "SSAOScratch";
        if (!RHI::CreateGpuImage(device, allocator, desc, m_Scratch)) {
            return false;
        }

        m_Width = width;
        m_Height = height;
        m_Stats.Width = width;
        m_Stats.Height = height;
        return true;
    }

    void ComputeSSAO::DestroyTargets() {
        if (!m_Context) {
            return;
        }
        RHI::DestroyGpuImage(m_Context->GetDevice(), m_Context->GetAllocator(), m_Occlusion);
        RHI::DestroyGpuImage(m_Context->GetDevice(), m_Context->GetAllocator(), m_Scratch);
        m_Width = 0;
        m_Height = 0;
    }

    void ComputeSSAO::Render(VkCommandBuffer cmd, const SSAOInputs& inputs,
                             const ECS::PostProcessSettings& settings) {
        m_Stats.Active = false;
        if (!IsInitialized() || !m_Occlusion.IsValid() || inputs.DepthView == VK_NULL_HANDLE ||
            inputs.NormalView == VK_NULL_HANDLE) {
            return;
        }

        VkSampler sampler = inputs.Sampler != VK_NULL_HANDLE ? inputs.Sampler : m_Sampler;
        const uint32_t kernelSize = std::clamp(static_cast<uint32_t>(settings.ssaoKernelSize), 4u, 64u);
        const uint32_t blurPasses = std::clamp(static_cast<uint32_t>(settings.ssaoBlurPasses), 0u, 4u);
        m_Stats.KernelSize = kernelSize;
        m_Stats.BlurPasses = blurPasses;

        SSAOUniforms uniforms{};
        uniforms.View = inputs.View;
        uniforms.Projection = inputs.Projection;
        uniforms.InverseViewProjection = inputs.InverseViewProjection;
        uniforms.Resolution = Math::Vec4(static_cast<float>(m_Width), static_cast<float>(m_Height),
                                         1.0f / static_cast<float>(m_Width),
                                         1.0f / static_cast<float>(m_Height));
        uniforms.Params = Math::Vec4(settings.ssaoRadius, settings.ssaoBias,
                                     settings.ssaoIntensity, static_cast<float>(kernelSize));
        uniforms.BlurParams = Math::Vec4(0.0f, 0.05f,
                                         static_cast<float>(inputs.FrameIndex & 63u), 0.0f);

        auto dispatch = [&](uint32_t setIndex, const RHI::ComputePipeline& pipeline,
                            VkImageView source, RHI::GpuImage& target, const SSAOUniforms& constants) {
            if (m_Uniforms[setIndex].Mapped) {
                std::memcpy(m_Uniforms[setIndex].Mapped, &constants, sizeof(constants));
            }
            RHI::TransitionImage(cmd, target, VK_IMAGE_LAYOUT_GENERAL);

            VkDescriptorImageInfo imageInfos[4]{};
            imageInfos[0] = {sampler, inputs.DepthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            imageInfos[1] = {sampler, inputs.NormalView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            imageInfos[2] = {sampler, source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            imageInfos[3] = {VK_NULL_HANDLE, target.View, VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorBufferInfo bufferInfo{m_Uniforms[setIndex].Buffer, 0, sizeof(SSAOUniforms)};

            VkWriteDescriptorSet writes[5]{};
            for (uint32_t i = 0; i < 3; ++i) {
                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = m_Sets[setIndex];
                writes[i].dstBinding = i;
                writes[i].descriptorCount = 1;
                writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[i].pImageInfo = &imageInfos[i];
            }
            writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[3].dstSet = m_Sets[setIndex];
            writes[3].dstBinding = 3;
            writes[3].descriptorCount = 1;
            writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[3].pImageInfo = &imageInfos[3];
            writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[4].dstSet = m_Sets[setIndex];
            writes[4].dstBinding = 4;
            writes[4].descriptorCount = 1;
            writes[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[4].pBufferInfo = &bufferInfo;
            vkUpdateDescriptorSets(m_Context->GetDevice(), 5, writes, 0, nullptr);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Layout,
                                    0, 1, &m_Sets[setIndex], 0, nullptr);
            vkCmdDispatch(cmd,
                          (m_Width + kGroupSize - 1) / kGroupSize,
                          (m_Height + kGroupSize - 1) / kGroupSize, 1);
            RHI::TransitionImage(cmd, target, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        };

        // The occlusion pass does not read `uSource`, but the binding is in the
        // layout, so it gets a live view anyway - an unwritten descriptor in a
        // bound set is undefined behaviour whether the shader samples it or not.
        dispatch(0, m_OcclusionPipeline, m_Occlusion.View, m_Occlusion, uniforms);

        // Separable blur, ping-ponging so no dispatch reads what it writes. Each
        // full pass is one horizontal and one vertical.
        for (uint32_t pass = 0; pass < blurPasses; ++pass) {
            SSAOUniforms horizontal = uniforms;
            horizontal.BlurParams.x = 1.0f;
            dispatch(1, m_BlurPipeline, m_Occlusion.View, m_Scratch, horizontal);

            SSAOUniforms vertical = uniforms;
            vertical.BlurParams.x = 0.0f;
            dispatch(2, m_BlurPipeline, m_Scratch.View, m_Occlusion, vertical);
        }

        m_Stats.Active = true;
    }

    void ComputeSSAO::Shutdown() {
        if (!m_Context) {
            return;
        }
        VkDevice device = m_Context->GetDevice();
        VmaAllocator allocator = m_Context->GetAllocator();
        vkDeviceWaitIdle(device);

        DestroyTargets();
        for (auto& buffer : m_Uniforms) {
            RHI::DestroyGpuBuffer(allocator, buffer);
        }
        RHI::DestroyComputePipeline(device, m_OcclusionPipeline);
        RHI::DestroyComputePipeline(device, m_BlurPipeline);

        if (m_Sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, m_Sampler, nullptr);
            m_Sampler = VK_NULL_HANDLE;
        }
        if (m_DescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
            m_DescriptorPool = VK_NULL_HANDLE;
        }
        if (m_SetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, m_SetLayout, nullptr);
            m_SetLayout = VK_NULL_HANDLE;
        }
        m_Context = nullptr;
    }

} // namespace Renderer
} // namespace Core
