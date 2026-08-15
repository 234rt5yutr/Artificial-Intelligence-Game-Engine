#include "ComputeBloom.h"

#include "Core/ECS/Components/PostProcessComponent.h"
#include "Core/Log.h"
#include "Core/RHI/Vulkan/VulkanContext.h"

#include <algorithm>
#include <cstring>

namespace Core {
namespace Renderer {

    namespace {

        constexpr uint32_t kGroupSize = 8;
        // Five halvings from the render resolution is enough for the widest
        // useful falloff; past that a mip is a handful of texels and adds
        // nothing but a dispatch.
        constexpr uint32_t kMaxBloomMips = 5;

        const char* kCommonPrologue = R"GLSL(
#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D uSource;
layout(binding = 1, rgba16f) uniform writeonly image2D uTarget;
layout(binding = 2) uniform Constants {
    vec4 sourceSize;   // xy size, zw 1/size
    vec4 targetSize;
    vec4 params;       // x threshold, y soft knee, z intensity, w scatter
} bloom;

bool OutOfBounds(ivec2 pixel) {
    return pixel.x >= int(bloom.targetSize.x) || pixel.y >= int(bloom.targetSize.y);
}

vec2 TargetUV(ivec2 pixel) {
    return (vec2(pixel) + 0.5) * bloom.targetSize.zw;
}
)GLSL";

        // Soft-knee threshold: a hard cutoff makes bloom pop on and off as a
        // highlight crosses it, which reads as flicker in motion.
        const char* kThresholdShader = R"GLSL(
void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (OutOfBounds(pixel)) {
        return;
    }

    vec3 color = texture(uSource, TargetUV(pixel)).rgb;
    float brightness = max(color.r, max(color.g, color.b));

    float threshold = bloom.params.x;
    float knee = max(threshold * bloom.params.y, 1e-4);
    float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
    soft = (soft * soft) / (4.0 * knee);
    float contribution = max(soft, brightness - threshold) / max(brightness, 1e-4);

    // NaNs and infinities in the HDR buffer would spread across the whole mip
    // chain from here, so they are clamped out at the one place they enter.
    vec3 result = color * contribution;
    result = clamp(result, vec3(0.0), vec3(64.0));
    if (any(isnan(result)) || any(isinf(result))) {
        result = vec3(0.0);
    }
    imageStore(uTarget, pixel, vec4(result, 1.0));
}
)GLSL";

        // 13-tap downsample: a plain box filter aliases badly on small bright
        // features, which is exactly what bloom is made of.
        const char* kDownsampleShader = R"GLSL(
void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (OutOfBounds(pixel)) {
        return;
    }

    vec2 uv = TargetUV(pixel);
    vec2 texel = bloom.sourceSize.zw;

    vec3 a = texture(uSource, uv + texel * vec2(-2.0,  2.0)).rgb;
    vec3 b = texture(uSource, uv + texel * vec2( 0.0,  2.0)).rgb;
    vec3 c = texture(uSource, uv + texel * vec2( 2.0,  2.0)).rgb;
    vec3 d = texture(uSource, uv + texel * vec2(-2.0,  0.0)).rgb;
    vec3 e = texture(uSource, uv).rgb;
    vec3 f = texture(uSource, uv + texel * vec2( 2.0,  0.0)).rgb;
    vec3 g = texture(uSource, uv + texel * vec2(-2.0, -2.0)).rgb;
    vec3 h = texture(uSource, uv + texel * vec2( 0.0, -2.0)).rgb;
    vec3 i = texture(uSource, uv + texel * vec2( 2.0, -2.0)).rgb;
    vec3 j = texture(uSource, uv + texel * vec2(-1.0,  1.0)).rgb;
    vec3 k = texture(uSource, uv + texel * vec2( 1.0,  1.0)).rgb;
    vec3 l = texture(uSource, uv + texel * vec2(-1.0, -1.0)).rgb;
    vec3 m = texture(uSource, uv + texel * vec2( 1.0, -1.0)).rgb;

    vec3 result = e * 0.125;
    result += (a + c + g + i) * 0.03125;
    result += (b + d + f + h) * 0.0625;
    result += (j + k + l + m) * 0.125;

    imageStore(uTarget, pixel, vec4(result, 1.0));
}
)GLSL";

        // Tent-filter upsample, accumulating into the level below. `scatter`
        // controls how far the glow spreads.
        const char* kUpsampleShader = R"GLSL(
void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (OutOfBounds(pixel)) {
        return;
    }

    vec2 uv = TargetUV(pixel);
    vec2 texel = bloom.sourceSize.zw * max(bloom.params.w, 0.1);

    vec3 result = texture(uSource, uv + texel * vec2(-1.0,  1.0)).rgb * 1.0;
    result += texture(uSource, uv + texel * vec2( 0.0,  1.0)).rgb * 2.0;
    result += texture(uSource, uv + texel * vec2( 1.0,  1.0)).rgb * 1.0;
    result += texture(uSource, uv + texel * vec2(-1.0,  0.0)).rgb * 2.0;
    result += texture(uSource, uv).rgb * 4.0;
    result += texture(uSource, uv + texel * vec2( 1.0,  0.0)).rgb * 2.0;
    result += texture(uSource, uv + texel * vec2(-1.0, -1.0)).rgb * 1.0;
    result += texture(uSource, uv + texel * vec2( 0.0, -1.0)).rgb * 2.0;
    result += texture(uSource, uv + texel * vec2( 1.0, -1.0)).rgb * 1.0;
    result *= (1.0 / 16.0);

    imageStore(uTarget, pixel, vec4(result, 1.0));
}
)GLSL";

    } // namespace

    // Composite is separate: it reads two images, so it needs its own binding
    // layout rather than the shared source/target pair.
    static const char* kCompositeShader = R"GLSL(
#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D uScene;
layout(binding = 1, rgba16f) uniform writeonly image2D uTarget;
layout(binding = 2) uniform Constants {
    vec4 sourceSize;
    vec4 targetSize;
    vec4 params;
} bloom;
layout(binding = 3) uniform sampler2D uBloom;

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (pixel.x >= int(bloom.targetSize.x) || pixel.y >= int(bloom.targetSize.y)) {
        return;
    }
    vec2 uv = (vec2(pixel) + 0.5) * bloom.targetSize.zw;

    vec3 scene = texture(uScene, uv).rgb;
    vec3 glow = texture(uBloom, uv).rgb;
    imageStore(uTarget, pixel, vec4(scene + glow * bloom.params.z, 1.0));
}
)GLSL";

    ComputeBloom::~ComputeBloom() {
        Shutdown();
    }

    bool ComputeBloom::Initialize(RHI::VulkanContext* context) {
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

        ENGINE_CORE_INFO("Compute bloom ready (threshold + {} mip progressive blur)", kMaxBloomMips);
        return true;
    }

    bool ComputeBloom::CreatePipelines() {
        VkDevice device = m_Context->GetDevice();

        // One layout for every stage. The composite's second texture sits at
        // binding 3 and is simply unused by the others, which is cheaper than
        // maintaining two layouts and two pools.
        m_SetLayout = RHI::CreateComputeSetLayout(device, {
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        });
        if (m_SetLayout == VK_NULL_HANDLE) {
            return false;
        }

        VkPipelineCache cache = m_Context->GetPipelineCache();
        const std::string prologue = kCommonPrologue;
        m_ThresholdPipeline = RHI::CreateComputePipeline(device, cache, prologue + kThresholdShader,
                                                         "bloom_threshold", {m_SetLayout}, 0);
        m_DownsamplePipeline = RHI::CreateComputePipeline(device, cache, prologue + kDownsampleShader,
                                                          "bloom_downsample", {m_SetLayout}, 0);
        m_UpsamplePipeline = RHI::CreateComputePipeline(device, cache, prologue + kUpsampleShader,
                                                        "bloom_upsample", {m_SetLayout}, 0);
        m_CompositePipeline = RHI::CreateComputePipeline(device, cache, kCompositeShader,
                                                         "bloom_composite", {m_SetLayout}, 0);
        if (!m_ThresholdPipeline.IsValid() || !m_DownsamplePipeline.IsValid() ||
            !m_UpsamplePipeline.IsValid() || !m_CompositePipeline.IsValid()) {
            return false;
        }

        // Threshold + downsamples + upsamples + composite, with room to spare.
        const uint32_t setCount = kMaxBloomMips * 2 + 4;
        const VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, setCount * 2},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, setCount},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, setCount},
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = setCount;
        poolInfo.poolSizeCount = 3;
        poolInfo.pPoolSizes = poolSizes;
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("ComputeBloom: descriptor pool creation failed");
            return false;
        }

        std::vector<VkDescriptorSetLayout> layouts(setCount, m_SetLayout);
        m_Sets.assign(setCount, VK_NULL_HANDLE);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_DescriptorPool;
        allocInfo.descriptorSetCount = setCount;
        allocInfo.pSetLayouts = layouts.data();
        if (vkAllocateDescriptorSets(device, &allocInfo, m_Sets.data()) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("ComputeBloom: descriptor set allocation failed");
            return false;
        }

        // One constant buffer per set: a single shared buffer would be rewritten
        // before the earlier dispatches had read it.
        m_Constants.resize(setCount);
        for (uint32_t i = 0; i < setCount; ++i) {
            if (!RHI::CreateGpuBuffer(m_Context->GetAllocator(), sizeof(BloomConstants),
                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, m_Constants[i])) {
                return false;
            }
        }
        return true;
    }

    bool ComputeBloom::Resize(uint32_t renderWidth, uint32_t renderHeight) {
        if (!m_Context || renderWidth == 0 || renderHeight == 0) {
            return false;
        }
        if (renderWidth == m_Width && renderHeight == m_Height && m_Output.IsValid()) {
            return true;
        }
        vkDeviceWaitIdle(m_Context->GetDevice());
        DestroyTargets();
        return CreateTargets(renderWidth, renderHeight);
    }

    bool ComputeBloom::CreateTargets(uint32_t width, uint32_t height) {
        VkDevice device = m_Context->GetDevice();
        VmaAllocator allocator = m_Context->GetAllocator();

        RHI::GpuImageDesc desc{};
        desc.Format = VK_FORMAT_R16G16B16A16_SFLOAT;
        // TRANSFER_SRC because a frame can end on this image, and a capture
        // blits from whichever image the post chain produced last.
        desc.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        desc.Width = width;
        desc.Height = height;
        desc.DebugName = "BloomOutput";
        if (!RHI::CreateGpuImage(device, allocator, desc, m_Output)) {
            return false;
        }

        uint32_t mipWidth = std::max(width / 2, 1u);
        uint32_t mipHeight = std::max(height / 2, 1u);
        m_Chain.clear();
        for (uint32_t mip = 0; mip < kMaxBloomMips; ++mip) {
            RHI::GpuImage image{};
            desc.Width = mipWidth;
            desc.Height = mipHeight;
            desc.DebugName = "BloomMip";
            if (!RHI::CreateGpuImage(device, allocator, desc, image)) {
                return false;
            }
            m_Chain.push_back(image);

            if (mipWidth <= 2 || mipHeight <= 2) {
                break;   // below this a mip is a few texels and adds nothing
            }
            mipWidth = std::max(mipWidth / 2, 1u);
            mipHeight = std::max(mipHeight / 2, 1u);
        }

        m_Width = width;
        m_Height = height;
        m_Stats.Width = width;
        m_Stats.Height = height;
        m_Stats.MipCount = static_cast<uint32_t>(m_Chain.size());
        return true;
    }

    void ComputeBloom::DestroyTargets() {
        if (!m_Context) {
            return;
        }
        VkDevice device = m_Context->GetDevice();
        VmaAllocator allocator = m_Context->GetAllocator();
        for (auto& image : m_Chain) {
            RHI::DestroyGpuImage(device, allocator, image);
        }
        m_Chain.clear();
        RHI::DestroyGpuImage(device, allocator, m_Output);
        m_Width = 0;
        m_Height = 0;
    }

    void ComputeBloom::Dispatch(VkCommandBuffer cmd, const RHI::ComputePipeline& pipeline,
                                uint32_t setIndex, VkImageView source, RHI::GpuImage& target,
                                const BloomConstants& constants) {
        if (setIndex >= m_Sets.size()) {
            return;
        }
        if (m_Constants[setIndex].Mapped) {
            std::memcpy(m_Constants[setIndex].Mapped, &constants, sizeof(constants));
        }

        RHI::TransitionImage(cmd, target, VK_IMAGE_LAYOUT_GENERAL);

        VkDescriptorImageInfo sourceInfo{m_Sampler, source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo targetInfo{VK_NULL_HANDLE, target.View, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorBufferInfo constantInfo{m_Constants[setIndex].Buffer, 0, sizeof(BloomConstants)};

        VkWriteDescriptorSet writes[4]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m_Sets[setIndex];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &sourceInfo;
        writes[1] = writes[0];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].pImageInfo = &targetInfo;
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = m_Sets[setIndex];
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[2].pBufferInfo = &constantInfo;
        // Binding 3 is only read by the composite, but every set writes it: an
        // unwritten descriptor in a bound set is undefined behaviour even when
        // the shader never touches it.
        writes[3] = writes[0];
        writes[3].dstBinding = 3;
        vkUpdateDescriptorSets(m_Context->GetDevice(), 4, writes, 0, nullptr);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Layout,
                                0, 1, &m_Sets[setIndex], 0, nullptr);
        vkCmdDispatch(cmd,
                      (target.Extent.width + kGroupSize - 1) / kGroupSize,
                      (target.Extent.height + kGroupSize - 1) / kGroupSize, 1);

        // Every stage feeds the next one, so each write has to be visible before
        // the following dispatch samples it.
        RHI::TransitionImage(cmd, target, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void ComputeBloom::Render(VkCommandBuffer cmd, RHI::GpuImage& sceneColor,
                              const ECS::PostProcessSettings& settings) {
        m_Stats.Active = false;
        if (!IsInitialized() || m_Chain.empty() || !m_Output.IsValid() ||
            sceneColor.View == VK_NULL_HANDLE) {
            return;
        }

        m_SetCursor = 0;
        auto sizeOf = [](const RHI::GpuImage& image) {
            return Math::Vec4(static_cast<float>(image.Extent.width),
                              static_cast<float>(image.Extent.height),
                              1.0f / static_cast<float>(std::max(image.Extent.width, 1u)),
                              1.0f / static_cast<float>(std::max(image.Extent.height, 1u)));
        };
        const Math::Vec4 sceneSize(static_cast<float>(m_Width), static_cast<float>(m_Height),
                                   1.0f / static_cast<float>(std::max(m_Width, 1u)),
                                   1.0f / static_cast<float>(std::max(m_Height, 1u)));
        const Math::Vec4 params(settings.bloomThreshold, settings.bloomSoftKnee,
                                settings.bloomIntensity, settings.bloomScatter);

        // Threshold straight into the first (half-resolution) mip.
        BloomConstants constants{};
        constants.SourceSize = sceneSize;
        constants.TargetSize = sizeOf(m_Chain[0]);
        constants.Params = params;
        Dispatch(cmd, m_ThresholdPipeline, m_SetCursor++, sceneColor.View, m_Chain[0], constants);

        // Down the chain.
        for (std::size_t mip = 1; mip < m_Chain.size(); ++mip) {
            constants.SourceSize = sizeOf(m_Chain[mip - 1]);
            constants.TargetSize = sizeOf(m_Chain[mip]);
            Dispatch(cmd, m_DownsamplePipeline, m_SetCursor++, m_Chain[mip - 1].View,
                     m_Chain[mip], constants);
        }

        // Back up it, each level blurring the one below into itself. Reading mip
        // N and writing mip N-1 means no dispatch ever reads what it writes.
        for (std::size_t mip = m_Chain.size(); mip-- > 1;) {
            constants.SourceSize = sizeOf(m_Chain[mip]);
            constants.TargetSize = sizeOf(m_Chain[mip - 1]);
            Dispatch(cmd, m_UpsamplePipeline, m_SetCursor++, m_Chain[mip].View,
                     m_Chain[mip - 1], constants);
        }

        // Composite the glow back over the scene into a separate output, so the
        // scene image is never both read and written.
        constants.SourceSize = sceneSize;
        constants.TargetSize = sceneSize;

        if (m_SetCursor < m_Sets.size()) {
            const uint32_t setIndex = m_SetCursor++;
            if (m_Constants[setIndex].Mapped) {
                std::memcpy(m_Constants[setIndex].Mapped, &constants, sizeof(constants));
            }
            RHI::TransitionImage(cmd, m_Output, VK_IMAGE_LAYOUT_GENERAL);

            VkDescriptorImageInfo sceneInfo{m_Sampler, sceneColor.View,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkDescriptorImageInfo targetInfo{VK_NULL_HANDLE, m_Output.View, VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo glowInfo{m_Sampler, m_Chain[0].View,
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkDescriptorBufferInfo constantInfo{m_Constants[setIndex].Buffer, 0, sizeof(BloomConstants)};

            VkWriteDescriptorSet writes[4]{};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = m_Sets[setIndex];
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].pImageInfo = &sceneInfo;
            writes[1] = writes[0];
            writes[1].dstBinding = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[1].pImageInfo = &targetInfo;
            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = m_Sets[setIndex];
            writes[2].dstBinding = 2;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[2].pBufferInfo = &constantInfo;
            writes[3] = writes[0];
            writes[3].dstBinding = 3;
            writes[3].pImageInfo = &glowInfo;
            vkUpdateDescriptorSets(m_Context->GetDevice(), 4, writes, 0, nullptr);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_CompositePipeline.Pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_CompositePipeline.Layout,
                                    0, 1, &m_Sets[setIndex], 0, nullptr);
            vkCmdDispatch(cmd,
                          (m_Width + kGroupSize - 1) / kGroupSize,
                          (m_Height + kGroupSize - 1) / kGroupSize, 1);
            RHI::TransitionImage(cmd, m_Output, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            m_Stats.Active = true;
        }
    }

    void ComputeBloom::Shutdown() {
        if (!m_Context) {
            return;
        }
        VkDevice device = m_Context->GetDevice();
        VmaAllocator allocator = m_Context->GetAllocator();
        vkDeviceWaitIdle(device);

        DestroyTargets();
        for (auto& buffer : m_Constants) {
            RHI::DestroyGpuBuffer(allocator, buffer);
        }
        m_Constants.clear();

        RHI::DestroyComputePipeline(device, m_ThresholdPipeline);
        RHI::DestroyComputePipeline(device, m_DownsamplePipeline);
        RHI::DestroyComputePipeline(device, m_UpsamplePipeline);
        RHI::DestroyComputePipeline(device, m_CompositePipeline);

        if (m_Sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, m_Sampler, nullptr);
            m_Sampler = VK_NULL_HANDLE;
        }
        if (m_DescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
            m_DescriptorPool = VK_NULL_HANDLE;
        }
        m_Sets.clear();
        if (m_SetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, m_SetLayout, nullptr);
            m_SetLayout = VK_NULL_HANDLE;
        }
        m_Context = nullptr;
    }

} // namespace Renderer
} // namespace Core
