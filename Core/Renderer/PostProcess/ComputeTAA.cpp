#include "ComputeTAA.h"

#include "Core/Log.h"
#include "Core/RHI/Vulkan/VulkanContext.h"

#include <algorithm>
#include <cstring>

namespace Core {
namespace Renderer {

    namespace {

        constexpr uint32_t kGroupSize = 8;

        const char* kTAAShader = R"GLSL(
#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D currentColor;
layout(binding = 1) uniform sampler2D sceneDepth;
layout(binding = 2) uniform sampler2D historyColor;
layout(binding = 3) uniform sampler2D sceneVelocity;
layout(binding = 4, rgba16f) uniform writeonly image2D outColor;
layout(binding = 5) uniform Params {
    mat4 invViewProjection;
    mat4 prevViewProjection;
    vec4 resolution;   // xy size, zw 1/size
    vec4 params;       // x feedback, y history valid, z frame index
} taa;

// Clamping in YCoCg rather than RGB: luminance and chroma get their own bounds,
// so a history sample that is the right brightness but the wrong hue is caught.
// Doing it in RGB lets chroma drift inside the box.
vec3 RGBToYCoCg(vec3 c) {
    return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
                0.5 * c.r - 0.5 * c.b,
                -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

vec3 YCoCgToRGB(vec3 c) {
    return vec3(c.x + c.y - c.z,
                c.x + c.z,
                c.x - c.y - c.z);
}

void main() {
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = ivec2(taa.resolution.xy);
    if (coord.x >= size.x || coord.y >= size.y) {
        return;
    }

    vec3 current = texelFetch(currentColor, coord, 0).rgb;

    // Reprojection. Depth is reconstructed to world space with this frame's
    // inverse view-projection and pushed back through the previous one, which
    // is exact for a static world and any camera motion.
    vec2 uv = (vec2(coord) + 0.5) * taa.resolution.zw;

    // Prefer the velocity the geometry pass wrote: it describes the surface own
    // motion as well as the camera. Where it wrote nothing - the sky, or a pixel
    // the pass never touched - fall back to reprojecting depth, which is exact
    // for a static world.
    vec2 velocity = texelFetch(sceneVelocity, coord, 0).rg;
    vec2 prevUV;
    float prevW = 1.0;
    if (dot(velocity, velocity) > 1e-12) {
        prevUV = uv - velocity;
    } else {
        float depth = texelFetch(sceneDepth, coord, 0).r;
        vec4 world = taa.invViewProjection * vec4(uv * 2.0 - 1.0, depth, 1.0);
        world /= max(world.w, 1e-6);
        vec4 prevClip = taa.prevViewProjection * vec4(world.xyz, 1.0);
        prevW = prevClip.w;
        prevUV = (prevClip.xy / max(abs(prevClip.w), 1e-6) * sign(prevClip.w)) * 0.5 + 0.5;
    }

    bool onScreen = all(greaterThanEqual(prevUV, vec2(0.0))) &&
                    all(lessThanEqual(prevUV, vec2(1.0))) && prevW > 0.0;
    float feedback = taa.params.x * taa.params.y * (onScreen ? 1.0 : 0.0);
    if (feedback <= 0.0) {
        imageStore(outColor, coord, vec4(current, 1.0));
        return;
    }

    // Neighbourhood statistics. The variance box is tighter than a hard min/max
    // over the 3x3, which on a noisy input would open wide enough to let almost
    // any history through.
    vec3 m1 = vec3(0.0);
    vec3 m2 = vec3(0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 tap = clamp(coord + ivec2(x, y), ivec2(0), size - 1);
            vec3 c = RGBToYCoCg(texelFetch(currentColor, tap, 0).rgb);
            m1 += c;
            m2 += c * c;
        }
    }
    vec3 mean = m1 / 9.0;
    vec3 sigma = sqrt(max(m2 / 9.0 - mean * mean, vec3(0.0)));
    vec3 minColor = mean - sigma;
    vec3 maxColor = mean + sigma;

    vec3 history = RGBToYCoCg(texture(historyColor, prevUV).rgb);
    history = clamp(history, minColor, maxColor);

    // Motion widens the door for the current frame: a fast pan has little
    // trustworthy history behind it, and holding on to it is what reads as
    // smearing.
    float motion = length((prevUV - uv) * taa.resolution.xy);
    feedback *= clamp(1.0 - motion * 0.02, 0.6, 1.0);

    vec3 result = YCoCgToRGB(mix(RGBToYCoCg(current), history, feedback));
    imageStore(outColor, coord, vec4(max(result, vec3(0.0)), 1.0));
}
)GLSL";

    } // namespace

    ComputeTAA::~ComputeTAA() {
        Shutdown();
    }

    bool ComputeTAA::Initialize(RHI::VulkanContext* context) {
        if (!context || context->GetDevice() == VK_NULL_HANDLE) {
            return false;
        }
        Shutdown();
        m_Context = context;

        if (!CreatePipeline()) {
            Shutdown();
            return false;
        }
        ENGINE_CORE_INFO("Temporal antialiasing ready (feedback {:.2f})", m_Feedback);
        return true;
    }

    bool ComputeTAA::CreatePipeline() {
        VkDevice device = m_Context->GetDevice();

        m_SetLayout = RHI::CreateComputeSetLayout(device, {
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,   // current colour
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,   // depth
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,   // history
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,   // velocity
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,            // output
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        });
        if (m_SetLayout == VK_NULL_HANDLE) {
            return false;
        }

        m_Pipeline = RHI::CreateComputePipeline(device, m_Context->GetPipelineCache(),
                                                kTAAShader, "taa_resolve", {m_SetLayout}, 0);
        if (!m_Pipeline.IsValid()) {
            return false;
        }

        const VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 2;
        poolInfo.poolSizeCount = 3;
        poolInfo.pPoolSizes = poolSizes;
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
            return false;
        }

        VkDescriptorSetLayout layouts[2] = {m_SetLayout, m_SetLayout};
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_DescriptorPool;
        allocInfo.descriptorSetCount = 2;
        allocInfo.pSetLayouts = layouts;
        if (vkAllocateDescriptorSets(device, &allocInfo, m_Sets) != VK_SUCCESS) {
            return false;
        }

        for (uint32_t i = 0; i < 2; ++i) {
            if (!RHI::CreateGpuBuffer(m_Context->GetAllocator(), sizeof(TAAUniforms),
                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, m_Uniforms[i])) {
                return false;
            }
        }

        // Clamped and linear: history is sampled at an arbitrary reprojected
        // position, and wrapping there would drag the far edge of the screen
        // into the near one.
        m_Sampler = RHI::CreateClampedSampler(device, VK_FILTER_LINEAR);
        return m_Sampler != VK_NULL_HANDLE;
    }

    bool ComputeTAA::CreateTargets(uint32_t width, uint32_t height) {
        VkDevice device = m_Context->GetDevice();
        VmaAllocator allocator = m_Context->GetAllocator();

        RHI::GpuImageDesc desc{};
        desc.Width = width;
        desc.Height = height;
        desc.Format = VK_FORMAT_R16G16B16A16_SFLOAT;
        desc.Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        for (uint32_t i = 0; i < 2; ++i) {
            desc.DebugName = i == 0 ? "TAAHistory0" : "TAAHistory1";
            if (!RHI::CreateGpuImage(device, allocator, desc, m_History[i])) {
                return false;
            }
        }
        m_Width = width;
        m_Height = height;
        m_HistoryValid = false;
        return true;
    }

    void ComputeTAA::DestroyTargets() {
        if (!m_Context) {
            return;
        }
        VkDevice device = m_Context->GetDevice();
        VmaAllocator allocator = m_Context->GetAllocator();
        for (auto& image : m_History) {
            RHI::DestroyGpuImage(device, allocator, image);
        }
        m_Width = 0;
        m_Height = 0;
        m_HistoryValid = false;
    }

    bool ComputeTAA::Resize(uint32_t width, uint32_t height) {
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

    void ComputeTAA::SetEnabled(bool enabled) {
        if (m_Stats.Enabled == enabled) {
            return;
        }
        m_Stats.Enabled = enabled;
        // Whatever is in the history was accumulated under different rules.
        m_HistoryValid = false;
    }

    void ComputeTAA::SetFeedback(float feedback) {
        m_Feedback = std::clamp(feedback, 0.0f, 0.98f);
    }

    void ComputeTAA::Render(VkCommandBuffer cmd, RHI::GpuImage& source, const TAAInputs& inputs) {
        m_Stats.Active = false;
        m_Stats.Width = m_Width;
        m_Stats.Height = m_Height;
        m_Stats.Feedback = m_Feedback;
        m_Stats.HistoryValid = m_HistoryValid;
        if (!IsInitialized() || !m_Stats.Enabled || m_Width == 0 || m_Height == 0 ||
            !source.IsValid() || inputs.DepthView == VK_NULL_HANDLE) {
            return;
        }
        if (inputs.ResetHistory) {
            m_HistoryValid = false;
        }

        const uint32_t readIndex = m_WriteIndex;
        const uint32_t writeIndex = 1 - m_WriteIndex;

        TAAUniforms uniforms{};
        uniforms.InverseViewProjection = inputs.InverseViewProjection;
        uniforms.PreviousViewProjection = inputs.PreviousViewProjection;
        uniforms.Resolution = Math::Vec4(static_cast<float>(m_Width), static_cast<float>(m_Height),
                                         1.0f / static_cast<float>(m_Width),
                                         1.0f / static_cast<float>(m_Height));
        uniforms.Params = Math::Vec4(m_Feedback, m_HistoryValid ? 1.0f : 0.0f,
                                     static_cast<float>(inputs.FrameIndex & 0xFFFFu), 0.0f);
        if (m_Uniforms[writeIndex].Mapped) {
            std::memcpy(m_Uniforms[writeIndex].Mapped, &uniforms, sizeof(uniforms));
        }

        RHI::TransitionImage(cmd, m_History[readIndex], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        RHI::TransitionImage(cmd, m_History[writeIndex], VK_IMAGE_LAYOUT_GENERAL);

        VkSampler sampler = inputs.Sampler != VK_NULL_HANDLE ? inputs.Sampler : m_Sampler;
        VkDescriptorImageInfo imageInfos[5]{};
        imageInfos[0] = {sampler, source.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imageInfos[1] = {sampler, inputs.DepthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imageInfos[2] = {m_Sampler, m_History[readIndex].View,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        // Depth stands in when there is no velocity target, so the sampler is
        // always live even though the branch then never reads it.
        imageInfos[3] = {m_Sampler,
                         inputs.VelocityView != VK_NULL_HANDLE ? inputs.VelocityView
                                                               : inputs.DepthView,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imageInfos[4] = {VK_NULL_HANDLE, m_History[writeIndex].View, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorBufferInfo bufferInfo{m_Uniforms[writeIndex].Buffer, 0, sizeof(TAAUniforms)};

        VkWriteDescriptorSet writes[6]{};
        for (uint32_t i = 0; i < 5; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = m_Sets[writeIndex];
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = i == 4 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                              : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &imageInfos[i];
        }
        writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet = m_Sets[writeIndex];
        writes[5].dstBinding = 5;
        writes[5].descriptorCount = 1;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[5].pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(m_Context->GetDevice(), 6, writes, 0, nullptr);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_Pipeline.Pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_Pipeline.Layout,
                                0, 1, &m_Sets[writeIndex], 0, nullptr);
        vkCmdDispatch(cmd, (m_Width + kGroupSize - 1) / kGroupSize,
                      (m_Height + kGroupSize - 1) / kGroupSize, 1);

        RHI::TransitionImage(cmd, m_History[writeIndex], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        m_WriteIndex = writeIndex;
        m_HistoryValid = true;
        m_Stats.Active = true;
        m_Stats.HistoryValid = true;
    }

    void ComputeTAA::Shutdown() {
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
        m_Sets[0] = m_Sets[1] = VK_NULL_HANDLE;
        m_WriteIndex = 0;
        m_Context = nullptr;
    }

} // namespace Renderer
} // namespace Core
