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


        // Largest velocity per tile. Comparing squared magnitudes avoids a square
        // root per texel for a result only used to pick a winner.
        const char* kTileMaxShader = R"GLSL(
#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D sceneVelocity;
layout(binding = 1, rg16f) uniform writeonly image2D tileMax;
layout(binding = 2) uniform Params {
    vec4 resolution;
    vec4 params;
    vec4 tile;   // x tile size, y columns, z rows
} mb;

void main() {
    ivec2 tileCoord = ivec2(gl_GlobalInvocationID.xy);
    if (tileCoord.x >= int(mb.tile.y) || tileCoord.y >= int(mb.tile.z)) {
        return;
    }

    int size = int(mb.tile.x);
    ivec2 base = tileCoord * size;
    vec2 best = vec2(0.0);
    float bestLength = 0.0;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            ivec2 coord = base + ivec2(x, y);
            if (coord.x >= int(mb.resolution.x) || coord.y >= int(mb.resolution.y)) {
                continue;
            }
            vec2 velocity = texelFetch(sceneVelocity, coord, 0).rg;
            float len = dot(velocity, velocity);
            if (len > bestLength) {
                bestLength = len;
                best = velocity;
            }
        }
    }
    imageStore(tileMax, tileCoord, vec4(best, 0.0, 0.0));
}
)GLSL";

        // Largest of a tile and its eight neighbours. This is what lets a blur
        // reach one tile beyond the geometry that generated it, which is the
        // whole reason the tiles exist.
        const char* kNeighbourMaxShader = R"GLSL(
#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D tileMax;
layout(binding = 1, rg16f) uniform writeonly image2D neighbourMax;
layout(binding = 2) uniform Params {
    vec4 resolution;
    vec4 params;
    vec4 tile;
} mb;

void main() {
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = ivec2(int(mb.tile.y), int(mb.tile.z));
    if (coord.x >= size.x || coord.y >= size.y) {
        return;
    }

    vec2 best = vec2(0.0);
    float bestLength = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 tap = clamp(coord + ivec2(x, y), ivec2(0), size - 1);
            vec2 velocity = texelFetch(tileMax, tap, 0).rg;
            float len = dot(velocity, velocity);
            if (len > bestLength) {
                bestLength = len;
                best = velocity;
            }
        }
    }
    imageStore(neighbourMax, coord, vec4(best, 0.0, 0.0));
}
)GLSL";

        const char* kMotionBlurShader = R"GLSL(
#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D sceneColor;
layout(binding = 1) uniform sampler2D sceneVelocity;
layout(binding = 2, rgba16f) uniform writeonly image2D outColor;
layout(binding = 3) uniform Params {
    vec4 resolution;   // xy size, zw 1/size
    vec4 params;       // x strength, y sample count, z frame scale
    vec4 tile;         // x tile size, y columns, z rows
} mb;
layout(binding = 4) uniform sampler2D neighbourMax;

void main() {
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = ivec2(mb.resolution.xy);
    if (coord.x >= size.x || coord.y >= size.y) {
        return;
    }

    vec3 centre = texelFetch(sceneColor, coord, 0).rgb;
    vec2 ownVelocity = texelFetch(sceneVelocity, coord, 0).rg;

    // Gather along the largest velocity nearby rather than this pixel's own. A
    // background pixel next to a fast object has no velocity, and gathering
    // along zero is what left the object blurring strictly inside its own
    // outline.
    ivec2 tileCoord = clamp(coord / int(mb.tile.x), ivec2(0),
                            ivec2(int(mb.tile.y) - 1, int(mb.tile.z) - 1));
    vec2 velocity = texelFetch(neighbourMax, tileCoord, 0).rg * mb.params.x * mb.params.z;

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

    float ownTravel = length(ownVelocity * mb.params.x * mb.params.z * mb.resolution.xy);

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

        // A tap counts if either end of the pair is actually moving: the moving
        // sample spreading onto a still background, or a still background being
        // covered by something that is. A pair where neither moves contributes
        // nothing, which keeps a still region beside a fast one sharp.
        vec2 tapVelocity = texture(sceneVelocity, tapUV).rg;
        float tapTravel = length(tapVelocity * mb.params.x * mb.params.z * mb.resolution.xy);
        float tapWeight = clamp(max(tapTravel, ownTravel) / max(travel, 1e-3), 0.0, 1.0);
        if (tapWeight <= 0.0) {
            continue;
        }

        sum += texture(sceneColor, tapUV).rgb * tapWeight;
        weight += tapWeight;
    }

    imageStore(outColor, coord, vec4(sum / max(weight, 1e-3), 1.0));
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
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,   // neighbourhood maximum
        });
        if (m_SetLayout == VK_NULL_HANDLE) {
            return false;
        }

        m_Pipeline = RHI::CreateComputePipeline(device, m_Context->GetPipelineCache(),
                                                kMotionBlurShader, "motion_blur", {m_SetLayout}, 0);
        if (!m_Pipeline.IsValid()) {
            return false;
        }

        // Three sets now: the blur itself plus the two tile reductions.
        const VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6},
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
        if (m_Sampler == VK_NULL_HANDLE) {
            return false;
        }
        return CreateTilePipelines();
    }

    bool ComputeMotionBlur::CreateTilePipelines() {
        VkDevice device = m_Context->GetDevice();

        m_TileSetLayout = RHI::CreateComputeSetLayout(device, {
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        });
        if (m_TileSetLayout == VK_NULL_HANDLE) {
            return false;
        }

        m_TilePipeline = RHI::CreateComputePipeline(device, m_Context->GetPipelineCache(),
                                                    kTileMaxShader, "velocity_tile_max",
                                                    {m_TileSetLayout}, 0);
        m_NeighbourPipeline = RHI::CreateComputePipeline(device, m_Context->GetPipelineCache(),
                                                         kNeighbourMaxShader,
                                                         "velocity_neighbour_max",
                                                         {m_TileSetLayout}, 0);
        if (!m_TilePipeline.IsValid() || !m_NeighbourPipeline.IsValid()) {
            return false;
        }

        VkDescriptorSetLayout layouts[2] = {m_TileSetLayout, m_TileSetLayout};
        VkDescriptorSet sets[2] = {};
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_DescriptorPool;
        allocInfo.descriptorSetCount = 2;
        allocInfo.pSetLayouts = layouts;
        if (vkAllocateDescriptorSets(device, &allocInfo, sets) != VK_SUCCESS) {
            return false;
        }
        m_TileSet = sets[0];
        m_NeighbourSet = sets[1];

        return RHI::CreateGpuBuffer(m_Context->GetAllocator(), sizeof(MotionBlurUniforms),
                                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, m_TileUniforms);
    }

    void ComputeMotionBlur::BuildTiles(VkCommandBuffer cmd, const MotionBlurInputs& inputs) {
        RHI::TransitionImage(cmd, m_TileMax, VK_IMAGE_LAYOUT_GENERAL);

        auto dispatchReduction = [&](const RHI::ComputePipeline& pipeline, VkDescriptorSet set,
                                     VkImageView source, RHI::GpuImage& target) {
            VkDescriptorImageInfo imageInfos[2]{};
            imageInfos[0] = {m_Sampler, source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            imageInfos[1] = {VK_NULL_HANDLE, target.View, VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorBufferInfo bufferInfo{m_TileUniforms.Buffer, 0,
                                              sizeof(MotionBlurUniforms)};

            VkWriteDescriptorSet writes[3]{};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = set;
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].pImageInfo = &imageInfos[0];
            writes[1] = writes[0];
            writes[1].dstBinding = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[1].pImageInfo = &imageInfos[1];
            writes[2] = writes[0];
            writes[2].dstBinding = 2;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[2].pImageInfo = nullptr;
            writes[2].pBufferInfo = &bufferInfo;
            vkUpdateDescriptorSets(m_Context->GetDevice(), 3, writes, 0, nullptr);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Layout,
                                    0, 1, &set, 0, nullptr);
            vkCmdDispatch(cmd, (m_TileColumns + 7) / 8, (m_TileRows + 7) / 8, 1);
        };

        dispatchReduction(m_TilePipeline, m_TileSet, inputs.VelocityView, m_TileMax);
        RHI::TransitionImage(cmd, m_TileMax, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        RHI::TransitionImage(cmd, m_NeighbourMax, VK_IMAGE_LAYOUT_GENERAL);
        dispatchReduction(m_NeighbourPipeline, m_NeighbourSet, m_TileMax.View, m_NeighbourMax);
        RHI::TransitionImage(cmd, m_NeighbourMax, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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

        m_TileColumns = (width + kTileSize - 1) / kTileSize;
        m_TileRows = (height + kTileSize - 1) / kTileSize;

        RHI::GpuImageDesc tileDesc{};
        tileDesc.Width = m_TileColumns;
        tileDesc.Height = m_TileRows;
        tileDesc.Format = VK_FORMAT_R16G16_SFLOAT;
        // TRANSFER_SRC so these can be captured: the dilation between the two is
        // the mechanism, and it is invisible in the final image.
        tileDesc.Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        tileDesc.DebugName = "VelocityTileMax";
        if (!RHI::CreateGpuImage(m_Context->GetDevice(), m_Context->GetAllocator(), tileDesc,
                                 m_TileMax)) {
            return false;
        }
        tileDesc.DebugName = "VelocityNeighbourMax";
        if (!RHI::CreateGpuImage(m_Context->GetDevice(), m_Context->GetAllocator(), tileDesc,
                                 m_NeighbourMax)) {
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
        RHI::DestroyGpuImage(m_Context->GetDevice(), m_Context->GetAllocator(), m_TileMax);
        RHI::DestroyGpuImage(m_Context->GetDevice(), m_Context->GetAllocator(), m_NeighbourMax);
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
        uniforms.TileParams = Math::Vec4(static_cast<float>(kTileSize),
                                         static_cast<float>(m_TileColumns),
                                         static_cast<float>(m_TileRows), 0.0f);
        if (m_Uniforms.Mapped) {
            std::memcpy(m_Uniforms.Mapped, &uniforms, sizeof(uniforms));
        }
        if (m_TileUniforms.Mapped) {
            std::memcpy(m_TileUniforms.Mapped, &uniforms, sizeof(uniforms));
        }

        // The reductions run first: the blur reads what they produce.
        BuildTiles(cmd, inputs);

        RHI::TransitionImage(cmd, m_Output, VK_IMAGE_LAYOUT_GENERAL);

        VkSampler sampler = inputs.Sampler != VK_NULL_HANDLE ? inputs.Sampler : m_Sampler;
        VkDescriptorImageInfo imageInfos[3]{};
        imageInfos[0] = {sampler, source.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imageInfos[1] = {m_Sampler, inputs.VelocityView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imageInfos[2] = {VK_NULL_HANDLE, m_Output.View, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorBufferInfo bufferInfo{m_Uniforms.Buffer, 0, sizeof(MotionBlurUniforms)};

        VkWriteDescriptorSet writes[5]{};
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

        VkDescriptorImageInfo neighbourInfo{m_Sampler, m_NeighbourMax.View,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = m_Set;
        writes[4].dstBinding = 4;
        writes[4].descriptorCount = 1;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[4].pImageInfo = &neighbourInfo;
        vkUpdateDescriptorSets(m_Context->GetDevice(), 5, writes, 0, nullptr);

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
        RHI::DestroyGpuBuffer(m_Context->GetAllocator(), m_TileUniforms);
        RHI::DestroyComputePipeline(device, m_Pipeline);
        RHI::DestroyComputePipeline(device, m_TilePipeline);
        RHI::DestroyComputePipeline(device, m_NeighbourPipeline);
        if (m_TileSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, m_TileSetLayout, nullptr);
            m_TileSetLayout = VK_NULL_HANDLE;
        }
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
