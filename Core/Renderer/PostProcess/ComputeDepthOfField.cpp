#include "ComputeDepthOfField.h"

#include "Core/ECS/Components/PostProcessComponent.h"
#include "Core/Log.h"
#include "Core/RHI/Vulkan/VulkanContext.h"

#include <algorithm>
#include <cstring>

namespace Core {
namespace Renderer {

    namespace {

        constexpr uint32_t kGroupSize = 8;

        const char* kDofShader = R"GLSL(
#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D sceneColor;
layout(binding = 1) uniform sampler2D sceneDepth;
layout(binding = 2, rgba16f) uniform writeonly image2D outColor;
layout(binding = 3) uniform Params {
    mat4 view;
    mat4 invViewProjection;
    vec4 resolution;   // xy size, zw 1/size
    vec4 params;       // x focal distance, y focal range, z max blur in pixels
} dof;

// A 16-tap sunflower disc. Even spacing over the disc rather than a grid, so the
// bokeh reads as round instead of square, and 16 is where more taps stop being
// visible at these radii.
const vec2 kDisc[16] = vec2[16](
    vec2( 0.0000,  0.2500), vec2(-0.3248,  0.2135), vec2(-0.3936, -0.2003), vec2(-0.0668, -0.4954),
    vec2( 0.3423, -0.4557), vec2( 0.5915, -0.1339), vec2( 0.5136,  0.3428), vec2( 0.1272,  0.6503),
    vec2(-0.3403,  0.6183), vec2(-0.6650,  0.2846), vec2(-0.7315, -0.2286), vec2(-0.4586, -0.6529),
    vec2( 0.0184, -0.8221), vec2( 0.4964, -0.6871), vec2( 0.8080, -0.2609), vec2( 0.8256,  0.3033)
);

float ViewDepthAt(vec2 uv) {
    float depth = texture(sceneDepth, uv).r;
    vec4 world = dof.invViewProjection * vec4(uv * 2.0 - 1.0, depth, 1.0);
    world /= max(world.w, 1e-6);
    return -(dof.view * vec4(world.xyz, 1.0)).z;
}

// Circle of confusion, normalised to [0,1]. Zero at the focal plane and growing
// to the full blur radius once a surface is a focal range away from it.
float CircleOfConfusion(float viewDepth) {
    float distance = abs(viewDepth - dof.params.x);
    return clamp(distance / max(dof.params.y, 1e-3), 0.0, 1.0);
}

void main() {
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = ivec2(dof.resolution.xy);
    if (coord.x >= size.x || coord.y >= size.y) {
        return;
    }

    vec2 uv = (vec2(coord) + 0.5) * dof.resolution.zw;
    vec3 centre = texelFetch(sceneColor, coord, 0).rgb;
    float centreDepth = ViewDepthAt(uv);
    float coc = CircleOfConfusion(centreDepth);

    float radius = coc * dof.params.z;
    if (radius < 0.75) {
        // Inside the focal plane the gather would only resample the same texel.
        imageStore(outColor, coord, vec4(centre, 1.0));
        return;
    }

    vec3 sum = centre;
    float weight = 1.0;
    for (int i = 0; i < 16; ++i) {
        vec2 tapUV = uv + kDisc[i] * radius * dof.resolution.zw;
        vec3 tapColor = texture(sceneColor, tapUV).rgb;

        // Weight by the tap's own circle of confusion, not the centre's. A sharp
        // object in front must not smear onto the blurred background behind it:
        // it is in focus, so it has no business contributing to anything else.
        float tapCoc = CircleOfConfusion(ViewDepthAt(tapUV));
        float tapWeight = clamp(tapCoc * dof.params.z - length(kDisc[i]) * radius + 1.0, 0.0, 1.0);
        sum += tapColor * tapWeight;
        weight += tapWeight;
    }

    imageStore(outColor, coord, vec4(sum / max(weight, 1e-4), 1.0));
}
)GLSL";

    } // namespace

    ComputeDepthOfField::~ComputeDepthOfField() {
        Shutdown();
    }

    bool ComputeDepthOfField::Initialize(RHI::VulkanContext* context) {
        if (!context || context->GetDevice() == VK_NULL_HANDLE) {
            return false;
        }
        Shutdown();
        m_Context = context;
        if (!CreatePipeline()) {
            Shutdown();
            return false;
        }
        ENGINE_CORE_INFO("Depth of field ready");
        return true;
    }

    bool ComputeDepthOfField::CreatePipeline() {
        VkDevice device = m_Context->GetDevice();

        m_SetLayout = RHI::CreateComputeSetLayout(device, {
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,   // lit colour
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,   // depth
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        });
        if (m_SetLayout == VK_NULL_HANDLE) {
            return false;
        }

        m_Pipeline = RHI::CreateComputePipeline(device, m_Context->GetPipelineCache(),
                                                kDofShader, "depth_of_field", {m_SetLayout}, 0);
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

        if (!RHI::CreateGpuBuffer(m_Context->GetAllocator(), sizeof(DofUniforms),
                                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, m_Uniforms)) {
            return false;
        }

        m_Sampler = RHI::CreateClampedSampler(device, VK_FILTER_LINEAR);
        return m_Sampler != VK_NULL_HANDLE;
    }

    bool ComputeDepthOfField::CreateTargets(uint32_t width, uint32_t height) {
        RHI::GpuImageDesc desc{};
        desc.Width = width;
        desc.Height = height;
        desc.Format = VK_FORMAT_R16G16B16A16_SFLOAT;
        // TRANSFER_SRC because a frame can end on this image, and a capture blits
        // from whatever the frame ended on.
        desc.Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        desc.DebugName = "DepthOfFieldOutput";
        if (!RHI::CreateGpuImage(m_Context->GetDevice(), m_Context->GetAllocator(), desc, m_Output)) {
            return false;
        }
        m_Width = width;
        m_Height = height;
        return true;
    }

    void ComputeDepthOfField::DestroyTargets() {
        if (!m_Context) {
            return;
        }
        RHI::DestroyGpuImage(m_Context->GetDevice(), m_Context->GetAllocator(), m_Output);
        m_Width = 0;
        m_Height = 0;
    }

    bool ComputeDepthOfField::Resize(uint32_t width, uint32_t height) {
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

    void ComputeDepthOfField::Render(VkCommandBuffer cmd, RHI::GpuImage& source,
                                     const DepthOfFieldInputs& inputs,
                                     const ECS::PostProcessSettings& settings) {
        m_Stats.Active = false;
        m_Stats.Width = m_Width;
        m_Stats.Height = m_Height;
        m_Stats.FocalDistance = settings.dofFocalDistance;
        m_Stats.FocalRange = settings.dofFocalRange;

        // dofMaxBlur is authored as a fraction of the frame rather than pixels,
        // so the same setting means the same visual blur at any resolution.
        const float maxBlurPixels = std::clamp(settings.dofMaxBlur, 0.0f, 8.0f) *
                                    static_cast<float>(m_Height) * 0.01f;
        m_Stats.MaxBlurPixels = maxBlurPixels;

        if (!IsInitialized() || !settings.dofEnabled || m_Width == 0 || !source.IsValid() ||
            inputs.DepthView == VK_NULL_HANDLE || maxBlurPixels <= 0.0f) {
            return;
        }

        DofUniforms uniforms{};
        uniforms.View = inputs.View;
        uniforms.InverseViewProjection = inputs.InverseViewProjection;
        uniforms.Resolution = Math::Vec4(static_cast<float>(m_Width), static_cast<float>(m_Height),
                                         1.0f / static_cast<float>(m_Width),
                                         1.0f / static_cast<float>(m_Height));
        uniforms.Params = Math::Vec4(std::max(settings.dofFocalDistance, 0.0f),
                                     std::max(settings.dofFocalRange, 1e-3f),
                                     maxBlurPixels, 0.0f);
        if (m_Uniforms.Mapped) {
            std::memcpy(m_Uniforms.Mapped, &uniforms, sizeof(uniforms));
        }

        RHI::TransitionImage(cmd, m_Output, VK_IMAGE_LAYOUT_GENERAL);

        VkSampler sampler = inputs.Sampler != VK_NULL_HANDLE ? inputs.Sampler : m_Sampler;
        VkDescriptorImageInfo imageInfos[3]{};
        imageInfos[0] = {sampler, source.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imageInfos[1] = {m_Sampler, inputs.DepthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imageInfos[2] = {VK_NULL_HANDLE, m_Output.View, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorBufferInfo bufferInfo{m_Uniforms.Buffer, 0, sizeof(DofUniforms)};

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

    void ComputeDepthOfField::Shutdown() {
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
