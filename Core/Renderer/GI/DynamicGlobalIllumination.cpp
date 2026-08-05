#include "DynamicGlobalIllumination.h"

#include "Core/Log.h"
#include "Core/RHI/Vulkan/VulkanContext.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Core {
namespace Renderer {

    namespace {

        constexpr uint32_t kGroupSize = 8;
        constexpr uint32_t kProbeGroupSize = 64;

        // Shared prologue: both dispatches read the same G-buffer and cache.
        const char* kSharedPrologue = R"GLSL(
#version 450

layout(binding = 0) uniform sampler2D uDepth;
layout(binding = 1) uniform sampler2D uNormal;
layout(binding = 2) uniform sampler2D uColor;
layout(binding = 3) uniform sampler2D uHistory;
layout(binding = 4, rgba16f) uniform image2D uRadiance;

struct Probe {
    vec4 shR;
    vec4 shG;
    vec4 shB;
};
layout(std430, binding = 5) buffer Probes { Probe probes[]; };

layout(binding = 6) uniform GIParams {
    mat4 invViewProjection;
    mat4 viewProjection;
    mat4 previousViewProjection;
    vec4 cameraPosition;
    vec4 resolution;    // xy size, zw 1/size
    vec4 probeOrigin;   // xyz origin, w spacing
    uvec4 probeGrid;    // xyz counts, w frame index
    vec4 traceParams;   // x rays, y steps, z maxDistance, w temporalAlpha
    vec4 skyColor;      // rgb, w intensity
    vec4 flags;         // x intensity, y probe cache, z history valid
} gi;

vec3 WorldFromDepth(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = gi.invViewProjection * clip;
    return world.xyz / max(world.w, 1e-6);
}

uint ProbeIndex(ivec3 coord) {
    return uint(coord.z) * gi.probeGrid.x * gi.probeGrid.y +
           uint(coord.y) * gi.probeGrid.x + uint(coord.x);
}

ivec3 ProbeCoord(vec3 worldPos) {
    return ivec3(floor((worldPos - gi.probeOrigin.xyz) / gi.probeOrigin.w));
}

bool ProbeCoordValid(ivec3 coord) {
    return all(greaterThanEqual(coord, ivec3(0))) &&
           all(lessThan(coord, ivec3(gi.probeGrid.xyz)));
}

vec3 EvaluateProbe(uint index, vec3 direction) {
    // L1 spherical harmonics: enough for a soft bounce, and it fits in 48 bytes
    // per probe so the whole cache stays under a megabyte.
    const float c0 = 0.282095;
    const float c1 = 0.488603;
    vec4 basis = vec4(c0, c1 * direction.y, c1 * direction.z, c1 * direction.x);
    Probe probe = probes[index];
    return max(vec3(dot(probe.shR, basis), dot(probe.shG, basis), dot(probe.shB, basis)), vec3(0.0));
}

vec3 SampleRadianceCache(vec3 worldPos, vec3 direction) {
    vec3 sky = gi.skyColor.rgb * gi.skyColor.w;
    if (gi.flags.y < 0.5) {
        return sky;
    }
    ivec3 coord = ProbeCoord(worldPos);
    if (!ProbeCoordValid(coord)) {
        return sky;
    }
    vec3 cached = EvaluateProbe(ProbeIndex(coord), direction);
    // Probes that have never been written stay black; fall back to sky rather
    // than darkening the scene while the cache fills.
    float weight = clamp(dot(cached, vec3(1.0)) * 8.0, 0.0, 1.0);
    return mix(sky, cached, weight);
}

// Hash-based low-discrepancy-ish jitter; the temporal filter cleans up the rest.
float Hash(vec3 seed) {
    return fract(sin(dot(seed, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
}

vec3 CosineHemisphere(vec3 normal, vec2 rand) {
    float phi = 6.2831853 * rand.x;
    float cosTheta = sqrt(1.0 - rand.y);
    float sinTheta = sqrt(rand.y);
    vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);
    return normalize(tangent * (cos(phi) * sinTheta) + bitangent * (sin(phi) * sinTheta) + normal * cosTheta);
}
)GLSL";

        const char* kProbeShaderBody = R"GLSL(
layout(local_size_x = 64) in;

// Screen-space radiance injection. Each probe projects to the screen; when the
// pixel it lands on is at roughly the probe's own depth, that pixel's shaded
// colour is this probe's incoming radiance for this frame. Probes with no
// screen coverage decay instead of holding stale light forever.
void main() {
    uint index = gl_GlobalInvocationID.x;
    uint total = gi.probeGrid.x * gi.probeGrid.y * gi.probeGrid.z;
    if (index >= total) {
        return;
    }

    uint x = index % gi.probeGrid.x;
    uint y = (index / gi.probeGrid.x) % gi.probeGrid.y;
    uint z = index / (gi.probeGrid.x * gi.probeGrid.y);
    vec3 probePos = gi.probeOrigin.xyz + (vec3(x, y, z) + 0.5) * gi.probeOrigin.w;

    Probe probe = probes[index];
    const float decay = 0.98;

    vec4 clip = gi.viewProjection * vec4(probePos, 1.0);
    if (clip.w <= 1e-5) {
        probes[index].shR = probe.shR * decay;
        probes[index].shG = probe.shG * decay;
        probes[index].shB = probe.shB * decay;
        return;
    }

    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
        probes[index].shR = probe.shR * decay;
        probes[index].shG = probe.shG * decay;
        probes[index].shB = probe.shB * decay;
        return;
    }

    float sceneDepth = texture(uDepth, uv).r;
    if (sceneDepth >= 1.0) {
        probes[index].shR = probe.shR * decay;
        probes[index].shG = probe.shG * decay;
        probes[index].shB = probe.shB * decay;
        return;
    }

    vec3 surfacePos = WorldFromDepth(uv, sceneDepth);
    // Only accept the sample when the probe sits within one grid cell of the
    // surface it projected onto; otherwise it is reading light off a wall it is
    // nowhere near.
    if (distance(surfacePos, probePos) > gi.probeOrigin.w * 1.5) {
        probes[index].shR = probe.shR * decay;
        probes[index].shG = probe.shG * decay;
        probes[index].shB = probe.shB * decay;
        return;
    }

    vec3 normal = normalize(texture(uNormal, uv).xyz * 2.0 - 1.0);
    vec3 radiance = texture(uColor, uv).rgb;

    const float c0 = 0.282095;
    const float c1 = 0.488603;
    vec4 basis = vec4(c0, c1 * normal.y, c1 * normal.z, c1 * normal.x);

    float alpha = clamp(gi.traceParams.w * 2.0, 0.02, 1.0);
    probes[index].shR = mix(probe.shR, radiance.r * basis * 4.0, alpha);
    probes[index].shG = mix(probe.shG, radiance.g * basis * 4.0, alpha);
    probes[index].shB = mix(probe.shB, radiance.b * basis * 4.0, alpha);
}
)GLSL";

        const char* kTraceShaderBody = R"GLSL(
layout(local_size_x = 8, local_size_y = 8) in;

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (pixel.x >= int(gi.resolution.x) || pixel.y >= int(gi.resolution.y)) {
        return;
    }

    vec2 uv = (vec2(pixel) + 0.5) * gi.resolution.zw;
    float depth = texture(uDepth, uv).r;
    if (depth >= 1.0) {
        imageStore(uRadiance, pixel, vec4(0.0));
        return;
    }

    vec3 worldPos = WorldFromDepth(uv, depth);
    vec3 normal = normalize(texture(uNormal, uv).xyz * 2.0 - 1.0);

    int rayCount = int(gi.traceParams.x);
    int stepCount = int(gi.traceParams.y);
    float maxDistance = gi.traceParams.z;
    float stepLength = maxDistance / float(max(stepCount, 1));

    vec3 accumulated = vec3(0.0);
    float frameSeed = float(gi.probeGrid.w & 63u);

    for (int ray = 0; ray < rayCount; ++ray) {
        vec2 rand = vec2(Hash(vec3(pixel, frameSeed + float(ray) * 7.0)),
                         Hash(vec3(pixel.yx, frameSeed * 3.0 + float(ray) * 13.0)));
        vec3 direction = CosineHemisphere(normal, rand);

        vec3 hitRadiance = vec3(0.0);
        bool hit = false;
        // March in world space and re-project each step: no view-space
        // reconstruction, and it degrades gracefully when the ray leaves the
        // screen instead of smearing edge pixels inward.
        vec3 samplePos = worldPos + normal * 0.02;
        for (int step = 1; step <= stepCount; ++step) {
            samplePos += direction * stepLength;
            vec4 clip = gi.viewProjection * vec4(samplePos, 1.0);
            if (clip.w <= 1e-5) {
                break;
            }
            vec3 ndc = clip.xyz / clip.w;
            vec2 sampleUV = ndc.xy * 0.5 + 0.5;
            if (any(lessThan(sampleUV, vec2(0.0))) || any(greaterThan(sampleUV, vec2(1.0)))) {
                break;
            }
            float sceneDepth = texture(uDepth, sampleUV).r;
            if (sceneDepth >= 1.0) {
                continue;
            }
            if (ndc.z > sceneDepth + 1e-5) {
                // Behind the depth buffer: reject a hit that is far behind the
                // surface, which is geometry the ray passed by rather than hit.
                vec3 hitPos = WorldFromDepth(sampleUV, sceneDepth);
                if (distance(hitPos, samplePos) < stepLength * 2.0) {
                    hitRadiance = texture(uColor, sampleUV).rgb;
                    hit = true;
                }
                break;
            }
        }

        accumulated += hit ? hitRadiance : SampleRadianceCache(worldPos, direction);
    }

    vec3 radiance = accumulated / float(max(rayCount, 1)) * gi.flags.x;

    // Temporal reprojection through the previous frame's view projection. World
    // position is exact here, so this needs no motion vector buffer.
    if (gi.flags.z > 0.5) {
        vec4 previousClip = gi.previousViewProjection * vec4(worldPos, 1.0);
        if (previousClip.w > 1e-5) {
            vec2 previousUV = (previousClip.xy / previousClip.w) * 0.5 + 0.5;
            if (all(greaterThanEqual(previousUV, vec2(0.0))) && all(lessThanEqual(previousUV, vec2(1.0)))) {
                vec3 history = texture(uHistory, previousUV).rgb;
                radiance = mix(history, radiance, clamp(gi.traceParams.w, 0.01, 1.0));
            }
        }
    }

    imageStore(uRadiance, pixel, vec4(radiance, 1.0));
}
)GLSL";

    } // namespace

    DynamicGlobalIllumination::~DynamicGlobalIllumination() {
        Shutdown();
    }

    bool DynamicGlobalIllumination::Initialize(RHI::VulkanContext* context) {
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
        const uint32_t probeCount = kProbeGridX * kProbeGridY * kProbeGridZ;
        const bool buffersOk =
            m_Sampler != VK_NULL_HANDLE &&
            RHI::CreateGpuBuffer(context->GetAllocator(),
                                 static_cast<VkDeviceSize>(probeCount) * 48,
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                 true, m_Probes) &&
            RHI::CreateGpuBuffer(context->GetAllocator(), sizeof(GIUniforms),
                                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, m_Uniforms);
        if (!buffersOk) {
            Shutdown();
            return false;
        }

        if (m_Probes.Mapped) {
            std::memset(m_Probes.Mapped, 0, static_cast<std::size_t>(m_Probes.Size));
        }
        m_Stats.ProbeCount = probeCount;

        ENGINE_CORE_INFO("Dynamic GI ready: {} probes, screen traces + world radiance cache", probeCount);
        return true;
    }

    bool DynamicGlobalIllumination::CreatePipelines() {
        VkDevice device = m_Context->GetDevice();

        m_SetLayout = RHI::CreateComputeSetLayout(device, {
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  // depth
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  // normal
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  // color
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  // history
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,           // radiance out
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          // probes
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,          // params
        });
        if (m_SetLayout == VK_NULL_HANDLE) {
            return false;
        }

        const std::string probeSource = std::string(kSharedPrologue) + kProbeShaderBody;
        const std::string traceSource = std::string(kSharedPrologue) + kTraceShaderBody;
        VkPipelineCache cache = m_Context->GetPipelineCache();
        m_ProbePipeline = RHI::CreateComputePipeline(device, cache, probeSource, "gi_probe_inject",
                                                     {m_SetLayout}, 0);
        m_TracePipeline = RHI::CreateComputePipeline(device, cache, traceSource, "gi_screen_trace",
                                                     {m_SetLayout}, 0);
        if (!m_ProbePipeline.IsValid() || !m_TracePipeline.IsValid()) {
            return false;
        }

        const VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 4;
        poolInfo.pPoolSizes = poolSizes;
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("Dynamic GI: descriptor pool creation failed");
            return false;
        }

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_DescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_SetLayout;
        if (vkAllocateDescriptorSets(device, &allocInfo, &m_Set) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("Dynamic GI: descriptor set allocation failed");
            return false;
        }
        return true;
    }

    bool DynamicGlobalIllumination::Resize(uint32_t renderWidth, uint32_t renderHeight) {
        if (!m_Context) {
            return false;
        }
        const uint32_t width = std::max(1u, renderWidth / 2);
        const uint32_t height = std::max(1u, renderHeight / 2);
        if (width == m_Width && height == m_Height && m_Radiance.IsValid()) {
            return true;
        }

        vkDeviceWaitIdle(m_Context->GetDevice());
        DestroyTargets();
        return CreateTargets(width, height);
    }

    bool DynamicGlobalIllumination::CreateTargets(uint32_t width, uint32_t height) {
        RHI::GpuImageDesc desc{};
        desc.Width = width;
        desc.Height = height;
        desc.Format = VK_FORMAT_R16G16B16A16_SFLOAT;
        desc.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        desc.DebugName = "GIRadiance";

        VkDevice device = m_Context->GetDevice();
        VmaAllocator allocator = m_Context->GetAllocator();
        if (!RHI::CreateGpuImage(device, allocator, desc, m_Radiance)) {
            return false;
        }
        desc.DebugName = "GIHistory";
        if (!RHI::CreateGpuImage(device, allocator, desc, m_History)) {
            return false;
        }

        m_Width = width;
        m_Height = height;
        m_HistoryValid = false;
        m_FramesAccumulated = 0;
        m_Stats.Width = width;
        m_Stats.Height = height;
        return true;
    }

    void DynamicGlobalIllumination::DestroyTargets() {
        if (!m_Context) {
            return;
        }
        RHI::DestroyGpuImage(m_Context->GetDevice(), m_Context->GetAllocator(), m_Radiance);
        RHI::DestroyGpuImage(m_Context->GetDevice(), m_Context->GetAllocator(), m_History);
        m_Width = 0;
        m_Height = 0;
        m_HistoryValid = false;
    }

    void DynamicGlobalIllumination::Shutdown() {
        if (!m_Context) {
            return;
        }
        VkDevice device = m_Context->GetDevice();
        VmaAllocator allocator = m_Context->GetAllocator();

        DestroyTargets();
        RHI::DestroyGpuBuffer(allocator, m_Probes);
        RHI::DestroyGpuBuffer(allocator, m_Uniforms);
        RHI::DestroyComputePipeline(device, m_ProbePipeline);
        RHI::DestroyComputePipeline(device, m_TracePipeline);

        if (m_Sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, m_Sampler, nullptr);
            m_Sampler = VK_NULL_HANDLE;
        }
        if (m_DescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
            m_DescriptorPool = VK_NULL_HANDLE;
            m_Set = VK_NULL_HANDLE;
        }
        if (m_SetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, m_SetLayout, nullptr);
            m_SetLayout = VK_NULL_HANDLE;
        }
        m_Context = nullptr;
    }

    void DynamicGlobalIllumination::WriteDescriptors(const GIFrameInputs& inputs) {
        VkSampler sampler = inputs.LinearSampler != VK_NULL_HANDLE ? inputs.LinearSampler : m_Sampler;

        VkDescriptorImageInfo imageInfos[5]{};
        imageInfos[0] = {sampler, inputs.DepthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imageInfos[1] = {sampler, inputs.NormalView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imageInfos[2] = {sampler, inputs.ColorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imageInfos[3] = {sampler, m_History.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imageInfos[4] = {VK_NULL_HANDLE, m_Radiance.View, VK_IMAGE_LAYOUT_GENERAL};

        VkDescriptorBufferInfo probeInfo{m_Probes.Buffer, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo uniformInfo{m_Uniforms.Buffer, 0, sizeof(GIUniforms)};

        VkWriteDescriptorSet writes[7]{};
        for (uint32_t i = 0; i < 4; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = m_Set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &imageInfos[i];
        }
        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = m_Set;
        writes[4].dstBinding = 4;
        writes[4].descriptorCount = 1;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[4].pImageInfo = &imageInfos[4];
        writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet = m_Set;
        writes[5].dstBinding = 5;
        writes[5].descriptorCount = 1;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[5].pBufferInfo = &probeInfo;
        writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet = m_Set;
        writes[6].dstBinding = 6;
        writes[6].descriptorCount = 1;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[6].pBufferInfo = &uniformInfo;

        vkUpdateDescriptorSets(m_Context->GetDevice(), 7, writes, 0, nullptr);
    }

    void DynamicGlobalIllumination::Render(VkCommandBuffer cmd, const GIFrameInputs& inputs) {
        if (!IsInitialized() || !m_Radiance.IsValid() || !m_Settings.Enabled ||
            inputs.DepthView == VK_NULL_HANDLE || inputs.NormalView == VK_NULL_HANDLE ||
            inputs.ColorView == VK_NULL_HANDLE) {
            m_Stats.Ready = false;
            return;
        }

        // The probe grid is snapped to a multiple of the spacing and centred on
        // the camera, so walking forward shifts the cache by whole cells instead
        // of resampling every probe every frame.
        const float spacing = std::max(0.25f, m_Settings.ProbeSpacing);
        const Math::Vec3 halfExtent(kProbeGridX * spacing * 0.5f,
                                    kProbeGridY * spacing * 0.5f,
                                    kProbeGridZ * spacing * 0.5f);
        Math::Vec3 origin = inputs.CameraPosition - halfExtent;
        origin = Math::Vec3(std::floor(origin.x / spacing) * spacing,
                            std::floor(origin.y / spacing) * spacing,
                            std::floor(origin.z / spacing) * spacing);

        GIUniforms uniforms{};
        uniforms.InverseViewProjection = glm::inverse(inputs.ViewProjection);
        uniforms.ViewProjection = inputs.ViewProjection;
        uniforms.PreviousViewProjection = inputs.PreviousViewProjection;
        uniforms.CameraPosition = Math::Vec4(inputs.CameraPosition, 1.0f);
        uniforms.Resolution = Math::Vec4(static_cast<float>(m_Width), static_cast<float>(m_Height),
                                         1.0f / static_cast<float>(m_Width),
                                         1.0f / static_cast<float>(m_Height));
        uniforms.ProbeOrigin = Math::Vec4(origin, spacing);
        uniforms.ProbeGrid = Math::UVec4(kProbeGridX, kProbeGridY, kProbeGridZ,
                                         static_cast<uint32_t>(inputs.FrameIndex));
        uniforms.TraceParams = Math::Vec4(static_cast<float>(std::max(1u, m_Settings.RaysPerPixel)),
                                          static_cast<float>(std::max(1u, m_Settings.StepsPerRay)),
                                          m_Settings.MaxTraceDistance,
                                          m_Settings.TemporalAlpha);
        uniforms.SkyColor = Math::Vec4(m_Settings.SkyColor, m_Settings.SkyIntensity);
        uniforms.Flags = Math::Vec4(m_Settings.Intensity,
                                    m_Settings.ProbeCacheEnabled ? 1.0f : 0.0f,
                                    m_HistoryValid ? 1.0f : 0.0f, 0.0f);
        if (m_Uniforms.Mapped) {
            std::memcpy(m_Uniforms.Mapped, &uniforms, sizeof(uniforms));
        }

        // History must be readable before the trace samples it, and the radiance
        // target writable before the trace stores into it.
        RHI::TransitionImage(cmd, m_History, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        RHI::TransitionImage(cmd, m_Radiance, VK_IMAGE_LAYOUT_GENERAL);
        WriteDescriptors(inputs);

        if (m_Settings.ProbeCacheEnabled) {
            const uint32_t probeCount = kProbeGridX * kProbeGridY * kProbeGridZ;
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ProbePipeline.Pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ProbePipeline.Layout,
                                    0, 1, &m_Set, 0, nullptr);
            vkCmdDispatch(cmd, (probeCount + kProbeGroupSize - 1) / kProbeGroupSize, 1, 1);

            RHI::BufferBarrier(cmd, m_Probes.Buffer, VK_ACCESS_SHADER_WRITE_BIT,
                               VK_ACCESS_SHADER_READ_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_TracePipeline.Pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_TracePipeline.Layout,
                                0, 1, &m_Set, 0, nullptr);
        vkCmdDispatch(cmd,
                      (m_Width + kGroupSize - 1) / kGroupSize,
                      (m_Height + kGroupSize - 1) / kGroupSize, 1);

        // ponytail: a copy into history instead of ping-ponging two descriptor
        // sets. One half-res RGBA16F blit per frame; switch to ping-pong if it
        // ever shows up in a capture.
        RHI::TransitionImage(cmd, m_Radiance, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        RHI::TransitionImage(cmd, m_History, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkImageCopy region{};
        region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.srcSubresource.layerCount = 1;
        region.dstSubresource = region.srcSubresource;
        region.extent = {m_Width, m_Height, 1};
        vkCmdCopyImage(cmd, m_Radiance.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       m_History.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        RHI::TransitionImage(cmd, m_Radiance, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        m_HistoryValid = true;
        ++m_FramesAccumulated;
        m_Stats.FramesAccumulated = m_FramesAccumulated;
        m_Stats.Ready = true;
    }

} // namespace Renderer
} // namespace Core
