#include "ComputeMotionBlur.h"

#include "Core/ECS/Components/PostProcessComponent.h"
#include "Core/Log.h"
#include "Core/RHI/Vulkan/VulkanContext.h"

#include <algorithm>
#include <cstring>

namespace Core {
namespace Renderer {

    namespace {

        constexpr uint32_t kGroupSize = 8;

        const char* kMotionBlurShader = R"GLSL(
#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D sceneColor;
layout(binding = 1) uniform sampler2D sceneVelocity;
layout(binding = 2, rgba16f) uniform writeonly image2D outColor;
layout(binding = 3) uniform Params {
    vec4 resolution;   // xy size, zw 1/size
    vec4 params;       // x strength, y sample count, z frame scale
} mb;

void main() {
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = ivec2(mb.resolution.xy);
    if (coord.x >= size.x || coord.y >= size.y) {
        return;
    }

    vec3 centre = texelFetch(sceneColor, coord, 0).rgb;
    vec2 velocity = texelFetch(sceneVelocity, coord, 0).rg * mb.params.x * mb.params.z;

    // Below a pixel of travel the gather would only resample the same texel, and
    // clamping the far end stops a teleport from smearing across the frame.
    float travel = length(velocity * mb.resolution.xy);
    if (travel < 1.0) {
        imageStore(outColor, coord, vec4(centre, 1.0));
        return;
    }
    if (travel > 64.0) {
        velocity *= 64.0 / travel;
    }

    int samples = int(clamp(mb.params.y, 2.0, 32.0));
    vec3 sum = centre;
    float weight = 1.0;
    for (int i = 1; i < samples; ++i) {
        // Centred on the pixel and spread both ways along the motion, so a
        // moving edge blurs symmetrically instead of trailing to one side.
        float t = (float(i) / float(samples - 1)) - 0.5;
        vec2 tapUV = (vec2(coord) + 0.5) * mb.resolution.zw + velocity * t;
        if (any(lessThan(tapUV, vec2(0.0))) || any(greaterThan(tapUV, vec2(1.0)))) {
            continue;
        }
        sum += texture(sceneColor, tapUV).rgb;
        weight += 1.0;
    }

    imageStore(outColor, coord, vec4(sum / max(weight, 1.0), 1.0));
}
)GLSL";

    } // namespace

    ComputeMotionBlur::~ComputeMotionBlur() {
        Shutdown();
    }

    bool ComputeMotionBlur::Initialize(RHI::VulkanContext* context) {
        if (!context || context->GetDevice() == VK_NULL_HANDLE) {
            return false;
        }
        Shutdown();
        m_Context = context;
        if (!CreatePipeline()) {
            Shutdown();
            return false;
        }
        ENGINE_CORE_INFO("Motion blur ready");
        return true;
    }

    bool ComputeMotionBlur::CreatePipeline() {
        VkDevice device = m_Context->GetDevice();

        m_SetLayout = RHI::CreateComputeSetLayout(device, {
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,   // colour
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,   // velocity
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        });
        if (m_SetLayout == VK_NULL_HANDLE) {
            return false;
        }

        m_Pipeline = RHI::CreateComputePipeline(device, m_Context->GetPipelineCache(),
                                                kMotionBlurShader, "motion_blur", {m_SetLayout}, 0);
        if (!m_Pipeline.IsValid()) {
            return false;
        }

        const VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2},
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

        if (!RHI::CreateGpuBuffer(m_Context->GetAllocator(), sizeof(MotionBlurUniforms),
                                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, m_Uniforms)) {
            return false;
        }

        m_Sampler = RHI::CreateClampedSampler(device, VK_FILTER_LINEAR);
        return m_Sampler != VK_NULL_HANDLE;
    }

    bool ComputeMotionBlur::CreateTargets(uint32_t width, uint32_t height) {
        RHI::GpuImageDesc desc{};
        desc.Width = width;
        desc.Height = height;
        desc.Format = VK_FORMAT_R16G16B16A16_SFLOAT;
        // TRANSFER_SRC because the frame can end on this image and a capture
        // blits from whatever it ended on.
        desc.Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        desc.DebugName = "MotionBlurOutput";
        if (!RHI::CreateGpuImage(m_Context->GetDevice(), m_Context->GetAllocator(), desc, m_Output)) {
            return false;
        }
        m_Width = width;
        m_Height = height;
        return true;
    }

    void ComputeMotionBlur::DestroyTargets() {
        if (!m_Context) {
            return;
        }
        RHI::DestroyGpuImage(m_Context->GetDevice(), m_Context->GetAllocator(), m_Output);
        m_Width = 0;
        m_Height = 0;
    }

    bool ComputeMotionBlur::Resize(uint32_t width, uint32_t height) {
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

    void ComputeMotionBlur::Render(VkCommandBuffer cmd, RHI::GpuImage& source,
                                   const MotionBlurInputs& inputs,
                                   const ECS::PostProcessSettings& settings) {
        m_Stats.Active = false;
        m_Stats.Width = m_Width;
        m_Stats.Height = m_Height;
        m_Stats.SampleCount = static_cast<uint32_t>(std::clamp(settings.motionBlurSamples, 2, 32));
        m_Stats.Strength = settings.motionBlurScale;

        if (!IsInitialized() || !settings.motionBlurEnabled || m_Width == 0 || !source.IsValid() ||
            inputs.VelocityView == VK_NULL_HANDLE || settings.motionBlurScale <= 0.0f) {
            return;
        }

        MotionBlurUniforms uniforms{};
        uniforms.Resolution = Math::Vec4(static_cast<float>(m_Width), static_cast<float>(m_Height),
                                         1.0f / static_cast<float>(m_Width),
                                         1.0f / static_cast<float>(m_Height));
        uniforms.Params = Math::Vec4(std::clamp(settings.motionBlurScale, 0.0f, 4.0f),
                                     static_cast<float>(m_Stats.SampleCount),
                                     std::clamp(inputs.FrameScale, 0.05f, 8.0f), 0.0f);
        if (m_Uniforms.Mapped) {
            std::memcpy(m_Uniforms.Mapped, &uniforms, sizeof(uniforms));
        }

        RHI::TransitionImage(cmd, m_Output, VK_IMAGE_LAYOUT_GENERAL);

        VkSampler sampler = inputs.Sampler != VK_NULL_HANDLE ? inputs.Sampler : m_Sampler;
        VkDescriptorImageInfo imageInfos[3]{};
        imageInfos[0] = {sampler, source.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imageInfos[1] = {m_Sampler, inputs.VelocityView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imageInfos[2] = {VK_NULL_HANDLE, m_Output.View, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorBufferInfo bufferInfo{m_Uniforms.Buffer, 0, sizeof(MotionBlurUniforms)};

        VkWriteDescriptorSet writes[4]{};
        for (uint32_t i = 0; i < 3; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = m_Set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = i == 2 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                              : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &imageInfos[i];
        }
        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = m_Set;
        writes[3].dstBinding = 3;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[3].pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(m_Context->GetDevice(), 4, writes, 0, nullptr);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_Pipeline.Pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_Pipeline.Layout,
                                0, 1, &m_Set, 0, nullptr);
        vkCmdDispatch(cmd, (m_Width + kGroupSize - 1) / kGroupSize,
                      (m_Height + kGroupSize - 1) / kGroupSize, 1);

        RHI::TransitionImage(cmd, m_Output, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_Stats.Active = true;
    }

    void ComputeMotionBlur::Shutdown() {
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
