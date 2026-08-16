#include "ComputeSSR.h"

#include "Core/Log.h"
#include "Core/RHI/Vulkan/VulkanContext.h"
#include "Core/Renderer/EnvironmentBRDF.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace Core {
namespace Renderer {

    namespace {

        constexpr uint32_t kGroupSize = 8;

        const char* kSSRShader = R"GLSL(
#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D sceneColor;
layout(binding = 1) uniform sampler2D sceneDepth;
layout(binding = 2) uniform sampler2D sceneNormal;   // xyz world normal, w metallic
layout(binding = 3) uniform sampler2D sceneAlbedo;   // rgb base colour, a roughness
layout(binding = 4, rgba16f) uniform writeonly image2D outColor;
layout(binding = 5) uniform Params {
    mat4 view;
    mat4 projection;
    mat4 invViewProjection;
    vec4 resolution;       // xy size, zw 1/size
    vec4 cameraPosition;
    vec4 params;           // x maxDistance, y stepCount, z thickness, w intensity
    vec4 params2;          // x refineSteps, y roughnessCutoff, z frameIndex
    vec4 skyColor;
    vec4 groundColor;
} ssr;

%ENVIRONMENT_BRDF%

vec3 WorldFromDepth(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = ssr.invViewProjection * clip;
    return world.xyz / max(world.w, 1e-6);
}

// View-space point to UV plus its own depth, so the march can compare against
// what the depth buffer holds at the same pixel.
bool ProjectView(vec3 viewPos, out vec2 uv, out float clipDepth) {
    vec4 clip = ssr.projection * vec4(viewPos, 1.0);
    if (clip.w <= 1e-6) {
        return false;
    }
    vec3 ndc = clip.xyz / clip.w;
    uv = ndc.xy * 0.5 + 0.5;
    clipDepth = ndc.z;
    return all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0)));
}

float ViewDepthAt(vec2 uv) {
    float depth = texture(sceneDepth, uv).r;
    vec3 world = WorldFromDepth(uv, depth);
    return -(ssr.view * vec4(world, 1.0)).z;
}

void main() {
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = ivec2(ssr.resolution.xy);
    if (coord.x >= size.x || coord.y >= size.y) {
        return;
    }

    vec2 uv = (vec2(coord) + 0.5) * ssr.resolution.zw;
    vec3 base = texelFetch(sceneColor, coord, 0).rgb;
    float depth = texelFetch(sceneDepth, coord, 0).r;

    vec4 normalSample = texelFetch(sceneNormal, coord, 0);
    vec4 albedoSample = texelFetch(sceneAlbedo, coord, 0);
    float roughness = albedoSample.a;
    float metallic = normalSample.w;

    // Sky, and anything too rough to reflect coherently, keep the lit colour
    // untouched. Marching them would only add noise.
    float cutoff = max(ssr.params2.y, 1e-3);
    if (depth >= 1.0 || roughness > cutoff) {
        imageStore(outColor, coord, vec4(base, 1.0));
        return;
    }

    vec3 worldPos = WorldFromDepth(uv, depth);
    vec3 worldNormal = normalize(normalSample.xyz * 2.0 - 1.0);
    vec3 viewPos = (ssr.view * vec4(worldPos, 1.0)).xyz;
    vec3 viewNormal = normalize(mat3(ssr.view) * worldNormal);
    vec3 viewDir = normalize(viewPos);
    vec3 reflectDir = reflect(viewDir, viewNormal);

    // A ray heading back at the camera can only leave the screen, so there is
    // nothing on screen for it to find.
    if (reflectDir.z > 0.0) {
        imageStore(outColor, coord, vec4(base, 1.0));
        return;
    }

    int steps = int(max(ssr.params.y, 1.0));
    float maxDistance = ssr.params.x;
    float stepSize = maxDistance / float(steps);
    float thickness = ssr.params.z;

    // Jitter the first step by a per-pixel offset. Marching every pixel from the
    // same place makes the miss pattern line up into visible bands.
    float jitter = fract(sin(dot(vec2(coord) + ssr.params2.z, vec2(12.9898, 78.233))) * 43758.5453);

    vec3 hitColor = vec3(0.0);
    float hitMask = 0.0;
    vec2 hitUV = vec2(0.0);

    float travelled = stepSize * (0.5 + jitter * 0.5);
    for (int i = 0; i < steps; ++i) {
        vec3 samplePos = viewPos + reflectDir * travelled;
        vec2 sampleUV;
        float sampleClipDepth;
        if (!ProjectView(samplePos, sampleUV, sampleClipDepth)) {
            break;
        }

        float sceneViewDepth = ViewDepthAt(sampleUV);
        float rayViewDepth = -samplePos.z;
        float difference = rayViewDepth - sceneViewDepth;

        if (difference > 0.0 && difference < thickness + stepSize) {
            // Hit. Halve back and forth to find where the ray actually crossed
            // the surface, otherwise the reflection quantises to the step size.
            float low = travelled - stepSize;
            float high = travelled;
            for (int r = 0; r < int(ssr.params2.x); ++r) {
                float mid = (low + high) * 0.5;
                vec3 midPos = viewPos + reflectDir * mid;
                vec2 midUV;
                float midClip;
                if (!ProjectView(midPos, midUV, midClip)) {
                    break;
                }
                if (-midPos.z - ViewDepthAt(midUV) > 0.0) {
                    high = mid;
                } else {
                    low = mid;
                }
            }
            vec3 finalPos = viewPos + reflectDir * high;
            vec2 finalUV;
            float finalClip;
            if (ProjectView(finalPos, finalUV, finalClip)) {
                hitUV = finalUV;
                hitColor = texture(sceneColor, finalUV).rgb;
                hitMask = 1.0;
            }
            break;
        }
        travelled += stepSize;
    }

    if (hitMask > 0.0) {
        // Fade at the screen edges, because a reflection that ends at the border
        // of the frame is the most obvious way screen-space anything gives
        // itself away.
        vec2 edge = smoothstep(vec2(0.0), vec2(0.12), hitUV) *
                    smoothstep(vec2(0.0), vec2(0.12), vec2(1.0) - hitUV);
        hitMask *= edge.x * edge.y;
        // And fade with distance, so a ray that only just reached its limit does
        // not arrive at full strength.
        hitMask *= clamp(1.0 - travelled / max(maxDistance, 1e-3), 0.0, 1.0);
        // And with roughness, up to the cutoff where it stops entirely.
        hitMask *= clamp(1.0 - roughness / cutoff, 0.0, 1.0);
    }

    // Same f0 convention the geometry pass uses: dielectrics reflect 4%, metals
    // reflect their own colour.
    vec3 f0 = mix(vec3(0.04), albedoSample.rgb, metallic);
    float ndotv = clamp(dot(-viewDir, viewNormal), 0.0, 1.0);
    vec3 envWeight = EnvBRDFApprox(f0, roughness, ndotv);

    // The lit image already carries ambient specular from the environment. A
    // traced hit is a better answer for the same lobe, so it replaces that term
    // rather than stacking on top of it - adding both would make a mirror twice
    // as bright as the thing it reflects.
    vec3 environment = EnvironmentSpecular(worldNormal, normalize(ssr.cameraPosition.xyz - worldPos),
                                           f0, roughness,
                                           ssr.skyColor.rgb, ssr.groundColor.rgb);
    vec3 traced = hitColor * envWeight * ssr.params.w;
    vec3 result = base + (traced - environment) * hitMask;
    imageStore(outColor, coord, vec4(max(result, vec3(0.0)), 1.0));
}
)GLSL";

    } // namespace

    ComputeSSR::~ComputeSSR() {
        Shutdown();
    }

    bool ComputeSSR::Initialize(RHI::VulkanContext* context) {
        if (!context || context->GetDevice() == VK_NULL_HANDLE) {
            return false;
        }
        Shutdown();
        m_Context = context;
        if (!CreatePipeline()) {
            Shutdown();
            return false;
        }
        ENGINE_CORE_INFO("Screen-space reflections ready ({} steps, {:.0f}m reach)",
                         m_Settings.StepCount, m_Settings.MaxDistance);
        return true;
    }

    bool ComputeSSR::CreatePipeline() {
        VkDevice device = m_Context->GetDevice();

        m_SetLayout = RHI::CreateComputeSetLayout(device, {
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,   // lit colour
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,   // depth
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,   // normal + metallic
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,   // albedo + roughness
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        });
        if (m_SetLayout == VK_NULL_HANDLE) {
            return false;
        }

        std::string source = kSSRShader;
        const std::string token = "%ENVIRONMENT_BRDF%";
        const std::size_t slot = source.find(token);
        if (slot == std::string::npos) {
            return false;
        }
        source.replace(slot, token.size(), kEnvironmentSpecularGLSL);

        m_Pipeline = RHI::CreateComputePipeline(device, m_Context->GetPipelineCache(),
                                                source.c_str(), "ssr_trace", {m_SetLayout}, 0);
        if (!m_Pipeline.IsValid()) {
            return false;
        }

        const VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 3;
        poolInfo.pPoolSizes = poolSizes;
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
            return false;
        }

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_DescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_SetLayout;
        if (vkAllocateDescriptorSets(device, &allocInfo, &m_Set) != VK_SUCCESS) {
            return false;
        }

        if (!RHI::CreateGpuBuffer(m_Context->GetAllocator(), sizeof(SSRUniforms),
                                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, m_Uniforms)) {
            return false;
        }

        m_Sampler = RHI::CreateClampedSampler(device, VK_FILTER_LINEAR);
        return m_Sampler != VK_NULL_HANDLE;
    }

    bool ComputeSSR::CreateTargets(uint32_t width, uint32_t height) {
        RHI::GpuImageDesc desc{};
        desc.Width = width;
        desc.Height = height;
        desc.Format = VK_FORMAT_R16G16B16A16_SFLOAT;
        // TRANSFER_SRC because a frame can end on this image when nothing after
        // it runs, and a capture blits from whatever the frame ended on.
        desc.Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        desc.DebugName = "SSROutput";
        if (!RHI::CreateGpuImage(m_Context->GetDevice(), m_Context->GetAllocator(), desc, m_Output)) {
            return false;
        }
        m_Width = width;
        m_Height = height;
        return true;
    }

    void ComputeSSR::DestroyTargets() {
        if (!m_Context) {
            return;
        }
        RHI::DestroyGpuImage(m_Context->GetDevice(), m_Context->GetAllocator(), m_Output);
        m_Width = 0;
        m_Height = 0;
    }

    bool ComputeSSR::Resize(uint32_t width, uint32_t height) {
        if (!IsInitialized() || width == 0 || height == 0) {
            return false;
        }
        if (width == m_Width && height == m_Height) {
            return true;
        }
        vkDeviceWaitIdle(m_Context->GetDevice());
        DestroyTargets();
        return CreateTargets(width, height);
    }

    void ComputeSSR::Render(VkCommandBuffer cmd, RHI::GpuImage& source, const SSRInputs& inputs) {
        m_Stats.Enabled = m_Settings.Enabled;
        m_Stats.Active = false;
        m_Stats.Width = m_Width;
        m_Stats.Height = m_Height;
        m_Stats.StepCount = m_Settings.StepCount;
        m_Stats.MaxDistance = m_Settings.MaxDistance;
        if (!IsInitialized() || !m_Settings.Enabled || m_Width == 0 || !source.IsValid() ||
            inputs.DepthView == VK_NULL_HANDLE || inputs.NormalView == VK_NULL_HANDLE ||
            inputs.AlbedoView == VK_NULL_HANDLE) {
            return;
        }

        SSRUniforms uniforms{};
        uniforms.View = inputs.View;
        uniforms.Projection = inputs.Projection;
        uniforms.InverseViewProjection = inputs.InverseViewProjection;
        uniforms.Resolution = Math::Vec4(static_cast<float>(m_Width), static_cast<float>(m_Height),
                                         1.0f / static_cast<float>(m_Width),
                                         1.0f / static_cast<float>(m_Height));
        uniforms.CameraPosition = Math::Vec4(inputs.CameraPosition, 1.0f);
        uniforms.Params = Math::Vec4(m_Settings.MaxDistance,
                                     static_cast<float>(m_Settings.StepCount),
                                     m_Settings.Thickness, m_Settings.Intensity);
        uniforms.Params2 = Math::Vec4(static_cast<float>(m_Settings.RefineSteps),
                                      m_Settings.RoughnessCutoff,
                                      static_cast<float>(inputs.FrameIndex & 0xFFFFu), 0.0f);
        uniforms.SkyColor = Math::Vec4(inputs.SkyColor, 1.0f);
        uniforms.GroundColor = Math::Vec4(inputs.GroundColor, 1.0f);
        if (m_Uniforms.Mapped) {
            std::memcpy(m_Uniforms.Mapped, &uniforms, sizeof(uniforms));
        }

        RHI::TransitionImage(cmd, m_Output, VK_IMAGE_LAYOUT_GENERAL);

        VkSampler sampler = inputs.Sampler != VK_NULL_HANDLE ? inputs.Sampler : m_Sampler;
        VkDescriptorImageInfo imageInfos[5]{};
        imageInfos[0] = {sampler, source.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imageInfos[1] = {m_Sampler, inputs.DepthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imageInfos[2] = {m_Sampler, inputs.NormalView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imageInfos[3] = {m_Sampler, inputs.AlbedoView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imageInfos[4] = {VK_NULL_HANDLE, m_Output.View, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorBufferInfo bufferInfo{m_Uniforms.Buffer, 0, sizeof(SSRUniforms)};

        VkWriteDescriptorSet writes[6]{};
        for (uint32_t i = 0; i < 5; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = m_Set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = i == 4 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                              : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &imageInfos[i];
        }
        writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet = m_Set;
        writes[5].dstBinding = 5;
        writes[5].descriptorCount = 1;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[5].pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(m_Context->GetDevice(), 6, writes, 0, nullptr);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_Pipeline.Pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_Pipeline.Layout,
                                0, 1, &m_Set, 0, nullptr);
        vkCmdDispatch(cmd, (m_Width + kGroupSize - 1) / kGroupSize,
                      (m_Height + kGroupSize - 1) / kGroupSize, 1);

        RHI::TransitionImage(cmd, m_Output, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_Stats.Active = true;
    }

    void ComputeSSR::Shutdown() {
        if (!m_Context) {
            return;
        }
        VkDevice device = m_Context->GetDevice();
        vkDeviceWaitIdle(device);

        DestroyTargets();
        RHI::DestroyGpuBuffer(m_Context->GetAllocator(), m_Uniforms);
        RHI::DestroyComputePipeline(device, m_Pipeline);
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
        m_Set = VK_NULL_HANDLE;
        m_Context = nullptr;
    }

} // namespace Renderer
} // namespace Core
