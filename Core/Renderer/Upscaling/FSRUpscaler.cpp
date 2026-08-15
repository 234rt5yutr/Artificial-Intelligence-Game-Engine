#include "FSRUpscaler.h"

#include "Core/Log.h"
#include "Core/RHI/Vulkan/VulkanContext.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Core {
namespace Renderer {

    namespace {

        constexpr uint32_t kGroupSize = 8;

        // Radical inverse in an arbitrary base; Halton(2,3) is the sequence FSR
        // and every temporal upscaler uses for sub-pixel jitter.
        float RadicalInverse(uint32_t index, uint32_t base) {
            float result = 0.0f;
            float fraction = 1.0f / static_cast<float>(base);
            while (index > 0) {
                result += static_cast<float>(index % base) * fraction;
                index /= base;
                fraction /= static_cast<float>(base);
            }
            return result;
        }

        const char* kEASUShader = R"GLSL(
#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D uSource;
layout(binding = 1, rgba16f) uniform writeonly image2D uTarget;
layout(binding = 2) uniform Constants {
    vec4 sourceSize;   // xy size, zw 1/size
    vec4 targetSize;   // xy size, zw 1/size
    vec4 params;       // x scale ratio, y sharpness
} fsr;

float Luma(vec3 c) {
    return c.g * 0.5 + (c.r + c.b) * 0.25;
}

// EASU analyses the 12 taps around the resample position, estimates the local
// edge direction and how strongly the neighbourhood is an edge, then applies an
// anisotropic windowed-sinc kernel aligned to that direction. The result keeps
// edges thin where a bilinear or Lanczos filter would blur or ring them.
void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (pixel.x >= int(fsr.targetSize.x) || pixel.y >= int(fsr.targetSize.y)) {
        return;
    }

    vec2 outputUV = (vec2(pixel) + 0.5) * fsr.targetSize.zw;
    vec2 sourcePixel = outputUV * fsr.sourceSize.xy - 0.5;
    vec2 base = floor(sourcePixel);
    vec2 frac = sourcePixel - base;

    // 12-tap layout (corners of the 4x4 block are dropped):
    //      b c
    //    e f g h
    //    i j k l
    //      n o
    vec2 texel = fsr.sourceSize.zw;
    vec2 origin = (base + 0.5) * texel;

    vec3 b = texture(uSource, origin + texel * vec2( 0.0, -1.0)).rgb;
    vec3 c = texture(uSource, origin + texel * vec2( 1.0, -1.0)).rgb;
    vec3 e = texture(uSource, origin + texel * vec2(-1.0,  0.0)).rgb;
    vec3 f = texture(uSource, origin + texel * vec2( 0.0,  0.0)).rgb;
    vec3 g = texture(uSource, origin + texel * vec2( 1.0,  0.0)).rgb;
    vec3 h = texture(uSource, origin + texel * vec2( 2.0,  0.0)).rgb;
    vec3 i = texture(uSource, origin + texel * vec2(-1.0,  1.0)).rgb;
    vec3 j = texture(uSource, origin + texel * vec2( 0.0,  1.0)).rgb;
    vec3 k = texture(uSource, origin + texel * vec2( 1.0,  1.0)).rgb;
    vec3 l = texture(uSource, origin + texel * vec2( 2.0,  1.0)).rgb;
    vec3 n = texture(uSource, origin + texel * vec2( 0.0,  2.0)).rgb;
    vec3 o = texture(uSource, origin + texel * vec2( 1.0,  2.0)).rgb;

    float bL = Luma(b), cL = Luma(c);
    float eL = Luma(e), fL = Luma(f), gL = Luma(g), hL = Luma(h);
    float iL = Luma(i), jL = Luma(j), kL = Luma(k), lL = Luma(l);
    float nL = Luma(n), oL = Luma(o);

    // Per-quadrant direction and edge length, accumulated with bilinear weights
    // so the kernel varies smoothly across the output pixel.
    vec2 direction = vec2(0.0);
    float length2 = 0.0;

    // Quadrant f (top-left of the 2x2 centre).
    {
        float dc = jL - fL;
        float cb = bL - fL;
        float lenX = max(abs(fL - gL), abs(fL - eL));
        float dirX = eL - gL;
        float lenY = max(abs(dc), abs(cb));
        float dirY = bL - jL;
        float len = clamp(abs(lenX) + abs(lenY), 0.0, 1.0);
        float weight = (1.0 - frac.x) * (1.0 - frac.y);
        direction += vec2(dirX, dirY) * weight;
        length2 += len * weight;
    }
    // Quadrant g (top-right).
    {
        float lenX = max(abs(gL - hL), abs(gL - fL));
        float dirX = fL - hL;
        float lenY = max(abs(gL - kL), abs(gL - cL));
        float dirY = cL - kL;
        float len = clamp(abs(lenX) + abs(lenY), 0.0, 1.0);
        float weight = frac.x * (1.0 - frac.y);
        direction += vec2(dirX, dirY) * weight;
        length2 += len * weight;
    }
    // Quadrant j (bottom-left).
    {
        float lenX = max(abs(jL - kL), abs(jL - iL));
        float dirX = iL - kL;
        float lenY = max(abs(jL - nL), abs(jL - fL));
        float dirY = fL - nL;
        float len = clamp(abs(lenX) + abs(lenY), 0.0, 1.0);
        float weight = (1.0 - frac.x) * frac.y;
        direction += vec2(dirX, dirY) * weight;
        length2 += len * weight;
    }
    // Quadrant k (bottom-right).
    {
        float lenX = max(abs(kL - lL), abs(kL - jL));
        float dirX = jL - lL;
        float lenY = max(abs(kL - oL), abs(kL - gL));
        float dirY = gL - oL;
        float len = clamp(abs(lenX) + abs(lenY), 0.0, 1.0);
        float weight = frac.x * frac.y;
        direction += vec2(dirX, dirY) * weight;
        length2 += len * weight;
    }

    float dirLength = length(direction);
    vec2 dir = dirLength > 1e-5 ? direction / dirLength : vec2(1.0, 0.0);

    // Anisotropy: a strong edge stretches the kernel along it and squeezes it
    // across, which is what keeps a diagonal from stair-stepping.
    float len2 = length2 * length2;
    float stretch = clamp(dirLength * 2.0, 0.0, 1.0);
    float scaleX = mix(1.0, 2.0, len2);
    float scaleY = mix(1.0, 0.5, len2 * stretch);

    vec3 accumulated = vec3(0.0);
    float weightSum = 0.0;
    vec3 minColor = vec3(1e9);
    vec3 maxColor = vec3(-1e9);

    // Windowed-sinc approximation: (25/16 * (x*x*w - 1)^2 - (25/16 - 1)) shaped
    // by a (1 - x*x/4)^2 window, exactly the base/window pair FSR uses.
    #define EASU_TAP(color, offset)                                            \
    {                                                                          \
        vec2 delta = (offset) - frac;                                          \
        vec2 rotated = vec2(dot(delta, dir), dot(delta, vec2(-dir.y, dir.x))); \
        rotated *= vec2(scaleX, scaleY);                                       \
        float distance2 = clamp(dot(rotated, rotated), 0.0, 4.0);              \
        float window = 1.0 - distance2 * 0.25;                                 \
        float base = (25.0 / 16.0) * (distance2 * 0.4 - 1.0) * (distance2 * 0.4 - 1.0) - (25.0 / 16.0 - 1.0); \
        float weight = base * window * window;                                 \
        accumulated += (color) * weight;                                       \
        weightSum += weight;                                                   \
        minColor = min(minColor, (color));                                     \
        maxColor = max(maxColor, (color));                                     \
    }

    EASU_TAP(b, vec2( 0.0, -1.0))
    EASU_TAP(c, vec2( 1.0, -1.0))
    EASU_TAP(e, vec2(-1.0,  0.0))
    EASU_TAP(f, vec2( 0.0,  0.0))
    EASU_TAP(g, vec2( 1.0,  0.0))
    EASU_TAP(h, vec2( 2.0,  0.0))
    EASU_TAP(i, vec2(-1.0,  1.0))
    EASU_TAP(j, vec2( 0.0,  1.0))
    EASU_TAP(k, vec2( 1.0,  1.0))
    EASU_TAP(l, vec2( 2.0,  1.0))
    EASU_TAP(n, vec2( 0.0,  2.0))
    EASU_TAP(o, vec2( 1.0,  2.0))
    #undef EASU_TAP

    vec3 result = weightSum > 1e-5 ? accumulated / weightSum : f;
    // Deringing: the anisotropic kernel has negative lobes, so clamp back into
    // the neighbourhood the taps actually contained.
    result = clamp(result, minColor, maxColor);

    imageStore(uTarget, pixel, vec4(max(result, vec3(0.0)), 1.0));
}
)GLSL";

        const char* kRCASShader = R"GLSL(
#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D uSource;
layout(binding = 1, rgba16f) uniform writeonly image2D uTarget;
layout(binding = 2) uniform Constants {
    vec4 sourceSize;
    vec4 targetSize;
    vec4 params;       // y = sharpness
} fsr;

float Luma(vec3 c) {
    return c.g * 0.5 + (c.r + c.b) * 0.25;
}

// Robust Contrast-Adaptive Sharpening: a single negative-lobe kernel whose
// strength is limited per pixel so it can never clip a neighbourhood extreme,
// which is what lets it run after an upscale without ringing.
void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (pixel.x >= int(fsr.targetSize.x) || pixel.y >= int(fsr.targetSize.y)) {
        return;
    }

    vec2 texel = fsr.sourceSize.zw;
    vec2 uv = (vec2(pixel) + 0.5) * fsr.targetSize.zw;

    vec3 e = texture(uSource, uv).rgb;
    vec3 b = texture(uSource, uv + vec2(0.0, -texel.y)).rgb;
    vec3 d = texture(uSource, uv + vec2(-texel.x, 0.0)).rgb;
    vec3 f = texture(uSource, uv + vec2( texel.x, 0.0)).rgb;
    vec3 h = texture(uSource, uv + vec2(0.0,  texel.y)).rgb;

    float bL = Luma(b), dL = Luma(d), eL = Luma(e), fL = Luma(f), hL = Luma(h);
    float minL = min(min(bL, dL), min(fL, hL));
    float maxL = max(max(bL, dL), max(fL, hL));
    minL = min(minL, eL);
    maxL = max(maxL, eL);

    float hitMin = minL / (4.0 * maxL + 1e-5);
    float hitMax = (1.0 - maxL) / (4.0 * minL - 4.0 - 1e-5);
    float lobe = max(-hitMin, hitMax);
    lobe = clamp(lobe, -0.1875, 0.0) * clamp(fsr.params.y, 0.0, 1.0);

    vec3 sharpened = (lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0);
    imageStore(uTarget, pixel, vec4(max(sharpened, vec3(0.0)), 1.0));
}
)GLSL";

    } // namespace

    float FSRRenderScaleFor(FSRQualityMode mode) {
        switch (mode) {
            case FSRQualityMode::Off:              return 1.0f;
            case FSRQualityMode::UltraQuality:     return 1.0f / 1.3f;
            case FSRQualityMode::Quality:          return 1.0f / 1.5f;
            case FSRQualityMode::Balanced:         return 1.0f / 1.7f;
            case FSRQualityMode::Performance:      return 1.0f / 2.0f;
            case FSRQualityMode::UltraPerformance: return 1.0f / 3.0f;
        }
        return 1.0f;
    }

    const char* FSRQualityModeName(FSRQualityMode mode) {
        switch (mode) {
            case FSRQualityMode::Off:              return "off";
            case FSRQualityMode::UltraQuality:     return "ultraQuality";
            case FSRQualityMode::Quality:          return "quality";
            case FSRQualityMode::Balanced:         return "balanced";
            case FSRQualityMode::Performance:      return "performance";
            case FSRQualityMode::UltraPerformance: return "ultraPerformance";
        }
        return "off";
    }

    bool FSRQualityModeFromString(const std::string& text, FSRQualityMode& out) {
        if (text == "off")              { out = FSRQualityMode::Off; return true; }
        if (text == "ultraQuality")     { out = FSRQualityMode::UltraQuality; return true; }
        if (text == "quality")          { out = FSRQualityMode::Quality; return true; }
        if (text == "balanced")         { out = FSRQualityMode::Balanced; return true; }
        if (text == "performance")      { out = FSRQualityMode::Performance; return true; }
        if (text == "ultraPerformance") { out = FSRQualityMode::UltraPerformance; return true; }
        return false;
    }

    FSRUpscaler::~FSRUpscaler() {
        Shutdown();
    }

    bool FSRUpscaler::Initialize(RHI::VulkanContext* context) {
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
        ENGINE_CORE_INFO("FSR upscaler ready (EASU + RCAS compute)");
        return true;
    }

    bool FSRUpscaler::CreatePipelines() {
        VkDevice device = m_Context->GetDevice();

        m_SetLayout = RHI::CreateComputeSetLayout(device, {
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        });
        if (m_SetLayout == VK_NULL_HANDLE) {
            return false;
        }

        VkPipelineCache cache = m_Context->GetPipelineCache();
        m_EASUPipeline = RHI::CreateComputePipeline(device, cache, kEASUShader, "fsr_easu",
                                                    {m_SetLayout}, 0);
        m_RCASPipeline = RHI::CreateComputePipeline(device, cache, kRCASShader, "fsr_rcas",
                                                    {m_SetLayout}, 0);
        if (!m_EASUPipeline.IsValid() || !m_RCASPipeline.IsValid()) {
            return false;
        }

        const VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 2;
        poolInfo.poolSizeCount = 3;
        poolInfo.pPoolSizes = poolSizes;
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("FSR: descriptor pool creation failed");
            return false;
        }

        const VkDescriptorSetLayout layouts[] = {m_SetLayout, m_SetLayout};
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_DescriptorPool;
        allocInfo.descriptorSetCount = 2;
        allocInfo.pSetLayouts = layouts;
        VkDescriptorSet sets[2]{};
        if (vkAllocateDescriptorSets(device, &allocInfo, sets) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("FSR: descriptor set allocation failed");
            return false;
        }
        m_EASUSet = sets[0];
        m_RCASSet = sets[1];
        return true;
    }

    bool FSRUpscaler::Resize(uint32_t renderWidth, uint32_t renderHeight,
                             uint32_t displayWidth, uint32_t displayHeight) {
        if (!m_Context || displayWidth == 0 || displayHeight == 0) {
            return false;
        }
        m_RenderWidth = renderWidth;
        m_RenderHeight = renderHeight;

        if (displayWidth == m_DisplayWidth && displayHeight == m_DisplayHeight && m_Output.IsValid()) {
            m_Stats.RenderWidth = renderWidth;
            m_Stats.RenderHeight = renderHeight;
            return true;
        }

        vkDeviceWaitIdle(m_Context->GetDevice());
        DestroyTargets();
        return CreateTargets(displayWidth, displayHeight);
    }

    bool FSRUpscaler::CreateTargets(uint32_t width, uint32_t height) {
        RHI::GpuImageDesc desc{};
        desc.Width = width;
        desc.Height = height;
        desc.Format = VK_FORMAT_R16G16B16A16_SFLOAT;
        desc.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        desc.DebugName = "FSRIntermediate";

        VkDevice device = m_Context->GetDevice();
        VmaAllocator allocator = m_Context->GetAllocator();
        if (!RHI::CreateGpuImage(device, allocator, desc, m_Intermediate)) {
            return false;
        }
        desc.DebugName = "FSROutput";
        if (!RHI::CreateGpuImage(device, allocator, desc, m_Output)) {
            return false;
        }

        // Constants are tiny and rewritten per frame, so they live in one mapped
        // buffer per pass rather than a push-constant block the two shaders would
        // have to agree on.
        const bool buffersOk =
            (m_EASUConstants.IsValid() ||
             RHI::CreateGpuBuffer(allocator, sizeof(FSRConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                  true, m_EASUConstants)) &&
            (m_RCASConstants.IsValid() ||
             RHI::CreateGpuBuffer(allocator, sizeof(FSRConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                  true, m_RCASConstants));
        if (!buffersOk) {
            return false;
        }

        m_DisplayWidth = width;
        m_DisplayHeight = height;
        m_Stats.DisplayWidth = width;
        m_Stats.DisplayHeight = height;
        return true;
    }

    void FSRUpscaler::DestroyTargets() {
        if (!m_Context) {
            return;
        }
        RHI::DestroyGpuImage(m_Context->GetDevice(), m_Context->GetAllocator(), m_Intermediate);
        RHI::DestroyGpuImage(m_Context->GetDevice(), m_Context->GetAllocator(), m_Output);
        m_DisplayWidth = 0;
        m_DisplayHeight = 0;
    }

    void FSRUpscaler::Shutdown() {
        if (!m_Context) {
            return;
        }
        VkDevice device = m_Context->GetDevice();
        VmaAllocator allocator = m_Context->GetAllocator();

        DestroyTargets();
        RHI::DestroyGpuBuffer(allocator, m_EASUConstants);
        RHI::DestroyGpuBuffer(allocator, m_RCASConstants);
        RHI::DestroyComputePipeline(device, m_EASUPipeline);
        RHI::DestroyComputePipeline(device, m_RCASPipeline);

        if (m_Sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, m_Sampler, nullptr);
            m_Sampler = VK_NULL_HANDLE;
        }
        if (m_DescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
            m_DescriptorPool = VK_NULL_HANDLE;
            m_EASUSet = VK_NULL_HANDLE;
            m_RCASSet = VK_NULL_HANDLE;
        }
        if (m_SetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, m_SetLayout, nullptr);
            m_SetLayout = VK_NULL_HANDLE;
        }
        m_Context = nullptr;
    }

    Math::Vec2 FSRUpscaler::GetJitter(uint64_t frameIndex) const {
        if (!m_Settings.JitterEnabled || !m_Settings.Enabled ||
            m_Settings.Quality == FSRQualityMode::Off) {
            return Math::Vec2(0.0f);
        }
        // Phase count scales with the upscale ratio, as FSR recommends: a bigger
        // jump needs more distinct sample positions to fill in.
        const float ratio = 1.0f / std::max(0.05f, FSRRenderScaleFor(m_Settings.Quality));
        const uint32_t phaseCount = std::max(8u, static_cast<uint32_t>(8.0f * ratio * ratio));
        const uint32_t index = static_cast<uint32_t>(frameIndex % phaseCount) + 1;
        return Math::Vec2(RadicalInverse(index, 2) - 0.5f, RadicalInverse(index, 3) - 0.5f);
    }

    void FSRUpscaler::Render(VkCommandBuffer cmd, VkImageView sourceView, VkSampler sampler) {
        m_Stats.Active = false;
        if (!IsInitialized() || !m_Output.IsValid() || sourceView == VK_NULL_HANDLE ||
            m_RenderWidth == 0 || m_RenderHeight == 0) {
            return;
        }

        const bool sharpen = m_Settings.Sharpness > 0.001f;
        VkSampler activeSampler = sampler != VK_NULL_HANDLE ? sampler : m_Sampler;
        RHI::GpuImage& easuTarget = sharpen ? m_Intermediate : m_Output;

        FSRConstants constants{};
        constants.SourceSize = Math::Vec4(static_cast<float>(m_RenderWidth),
                                          static_cast<float>(m_RenderHeight),
                                          1.0f / static_cast<float>(m_RenderWidth),
                                          1.0f / static_cast<float>(m_RenderHeight));
        constants.TargetSize = Math::Vec4(static_cast<float>(m_DisplayWidth),
                                          static_cast<float>(m_DisplayHeight),
                                          1.0f / static_cast<float>(m_DisplayWidth),
                                          1.0f / static_cast<float>(m_DisplayHeight));
        constants.Params = Math::Vec4(static_cast<float>(m_DisplayWidth) / static_cast<float>(m_RenderWidth),
                                      m_Settings.Sharpness, 0.0f, 0.0f);
        if (m_EASUConstants.Mapped) {
            std::memcpy(m_EASUConstants.Mapped, &constants, sizeof(constants));
        }

        auto writeSet = [&](VkDescriptorSet set, VkImageView source, VkImageView target, VkBuffer constantBuffer) {
            VkDescriptorImageInfo sourceInfo{activeSampler, source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkDescriptorImageInfo targetInfo{VK_NULL_HANDLE, target, VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorBufferInfo constantInfo{constantBuffer, 0, sizeof(FSRConstants)};

            VkWriteDescriptorSet writes[3]{};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = set;
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].pImageInfo = &sourceInfo;
            writes[1] = writes[0];
            writes[1].dstBinding = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[1].pImageInfo = &targetInfo;
            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = set;
            writes[2].dstBinding = 2;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[2].pBufferInfo = &constantInfo;
            vkUpdateDescriptorSets(m_Context->GetDevice(), 3, writes, 0, nullptr);
        };

        const uint32_t groupsX = (m_DisplayWidth + kGroupSize - 1) / kGroupSize;
        const uint32_t groupsY = (m_DisplayHeight + kGroupSize - 1) / kGroupSize;

        RHI::TransitionImage(cmd, easuTarget, VK_IMAGE_LAYOUT_GENERAL);
        writeSet(m_EASUSet, sourceView, easuTarget.View, m_EASUConstants.Buffer);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_EASUPipeline.Pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_EASUPipeline.Layout,
                                0, 1, &m_EASUSet, 0, nullptr);
        vkCmdDispatch(cmd, groupsX, groupsY, 1);

        if (sharpen) {
            // RCAS runs at display resolution, so its "source size" is the
            // upscaled image rather than the render target.
            FSRConstants rcasConstants = constants;
            rcasConstants.SourceSize = constants.TargetSize;
            if (m_RCASConstants.Mapped) {
                std::memcpy(m_RCASConstants.Mapped, &rcasConstants, sizeof(rcasConstants));
            }

            RHI::TransitionImage(cmd, m_Intermediate, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            RHI::TransitionImage(cmd, m_Output, VK_IMAGE_LAYOUT_GENERAL);
            writeSet(m_RCASSet, m_Intermediate.View, m_Output.View, m_RCASConstants.Buffer);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_RCASPipeline.Pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_RCASPipeline.Layout,
                                    0, 1, &m_RCASSet, 0, nullptr);
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        }

        RHI::TransitionImage(cmd, m_Output, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        m_Stats.RenderWidth = m_RenderWidth;
        m_Stats.RenderHeight = m_RenderHeight;
        m_Stats.RenderScale = static_cast<float>(m_RenderWidth) / static_cast<float>(m_DisplayWidth);
        m_Stats.SharpeningActive = sharpen;
        m_Stats.Active = true;
    }

} // namespace Renderer
} // namespace Core
