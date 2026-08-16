#include "EnvironmentProbe.h"

#include "Core/Log.h"
#include "Core/RHI/Vulkan/VulkanContext.h"

#include <algorithm>
#include <cstring>

namespace Core {
namespace Renderer {

    namespace {

        // Vulkan's cube face order, and the up vector each one needs. Getting a
        // single one of these wrong shows up as a reflection that is mirrored or
        // rotated only when you look in one direction, which is a miserable
        // thing to notice later.
        struct FaceBasis {
            Math::Vec3 Forward;
            Math::Vec3 Up;
        };

        constexpr FaceBasis kFaces[EnvironmentProbe::kFaceCount] = {
            {{ 1.0f,  0.0f,  0.0f}, {0.0f, -1.0f,  0.0f}},   // +X
            {{-1.0f,  0.0f,  0.0f}, {0.0f, -1.0f,  0.0f}},   // -X
            {{ 0.0f,  1.0f,  0.0f}, {0.0f,  0.0f,  1.0f}},   // +Y
            {{ 0.0f, -1.0f,  0.0f}, {0.0f,  0.0f, -1.0f}},   // -Y
            {{ 0.0f,  0.0f,  1.0f}, {0.0f, -1.0f,  0.0f}},   // +Z
            {{ 0.0f,  0.0f, -1.0f}, {0.0f, -1.0f,  0.0f}},   // -Z
        };


        // Importance-sampled GGX convolution. Each mip is the environment as a
        // surface of that roughness would gather it, which is what makes a rough
        // metal look rough rather than merely out of focus.
        const char* kPrefilterShader = R"GLSL(
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0) uniform samplerCube sourceCube;
layout(binding = 1, rgba16f) uniform writeonly image2DArray targetMip;
layout(binding = 2) uniform Params {
    vec4 params;   // x roughness, y mip size, z sample count
} pf;

const float PI = 3.14159265359;

// Face index to direction, matching Vulkan's cube face order. The same table the
// bake uses, expressed the other way round.
vec3 DirectionFor(uint face, vec2 uv) {
    vec2 t = uv * 2.0 - 1.0;
    if (face == 0u) return normalize(vec3( 1.0, -t.y, -t.x));
    if (face == 1u) return normalize(vec3(-1.0, -t.y,  t.x));
    if (face == 2u) return normalize(vec3( t.x,  1.0,  t.y));
    if (face == 3u) return normalize(vec3( t.x, -1.0, -t.y));
    if (face == 4u) return normalize(vec3( t.x, -t.y,  1.0));
    return normalize(vec3(-t.x, -t.y, -1.0));
}

// Hammersley: a low-discrepancy sequence, so a few dozen samples cover the lobe
// evenly instead of clumping the way random ones do.
float RadicalInverse(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec3 ImportanceSampleGGX(vec2 xi, vec3 normal, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    vec3 half = vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
    vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);
    return normalize(tangent * half.x + bitangent * half.y + normal * half.z);
}

void main() {
    ivec3 coord = ivec3(gl_GlobalInvocationID);
    int size = int(pf.params.y);
    if (coord.x >= size || coord.y >= size || coord.z >= 6) {
        return;
    }

    vec2 uv = (vec2(coord.xy) + 0.5) / float(size);
    vec3 normal = DirectionFor(uint(coord.z), uv);
    // The usual split-sum simplification: view, normal and reflection are all
    // assumed equal, which is what lets one cube serve every viewing angle.
    vec3 view = normal;

    float roughness = pf.params.x;
    uint samples = uint(pf.params.z);
    vec3 colour = vec3(0.0);
    float weight = 0.0;

    for (uint i = 0u; i < samples; ++i) {
        vec2 xi = vec2(float(i) / float(samples), RadicalInverse(i));
        vec3 halfway = ImportanceSampleGGX(xi, normal, roughness);
        vec3 light = normalize(2.0 * dot(view, halfway) * halfway - view);

        float ndotl = dot(normal, light);
        if (ndotl <= 0.0) {
            continue;
        }
        // Sampling mip 0 would alias badly at high roughness, where each sample
        // stands for a wide solid angle; a small bias trades that for blur the
        // convolution was going to apply anyway.
        colour += textureLod(sourceCube, light, roughness * 4.0).rgb * ndotl;
        weight += ndotl;
    }

    imageStore(targetMip, coord, vec4(colour / max(weight, 1e-4), 1.0));
}
)GLSL";

    } // namespace

    EnvironmentProbe::~EnvironmentProbe() {
        Shutdown();
    }

    bool EnvironmentProbe::Initialize(RHI::VulkanContext* context, uint32_t resolution) {
        if (!context || context->GetDevice() == VK_NULL_HANDLE) {
            return false;
        }
        Shutdown();
        m_Context = context;
        m_Resolution = std::clamp(resolution, 32u, 1024u);

        m_MipLevels = 1;
        for (uint32_t size = m_Resolution; size > 1; size /= 2) {
            ++m_MipLevels;
        }

        RHI::GpuImageDesc desc{};
        desc.Width = m_Resolution;
        desc.Height = m_Resolution;
        desc.MipLevels = m_MipLevels;
        desc.ArrayLayers = kFaceCount;
        desc.Cube = true;
        desc.Format = VK_FORMAT_R16G16B16A16_SFLOAT;
        desc.Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        desc.DebugName = "EnvironmentProbe";
        if (!RHI::CreateGpuImage(m_Context->GetDevice(), m_Context->GetAllocator(), desc, m_Cube)) {
            Shutdown();
            return false;
        }

        m_Sampler = RHI::CreateClampedSampler(m_Context->GetDevice(), VK_FILTER_LINEAR);
        if (m_Sampler == VK_NULL_HANDLE) {
            Shutdown();
            return false;
        }

        if (!CreatePrefilter()) {
            ENGINE_CORE_WARN("Environment probe prefilter unavailable; mips stay a plain blur");
        }

        m_Stats.Resolution = m_Resolution;
        m_Stats.MipLevels = m_MipLevels;
        ENGINE_CORE_INFO("Environment probe ready ({}x{} cube, {} mips)",
                         m_Resolution, m_Resolution, m_MipLevels);
        return true;
    }

    bool EnvironmentProbe::CreatePrefilter() {
        VkDevice device = m_Context->GetDevice();
        const uint32_t mips = std::min(m_MipLevels, kMaxMips);

        // One 2D array view per mip. The cube view is for sampling; a storage
        // write needs a view that is not a cube.
        for (uint32_t mip = 0; mip < mips; ++mip) {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = m_Cube.Image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            viewInfo.format = m_Cube.Format;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = mip;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = kFaceCount;
            if (vkCreateImageView(device, &viewInfo, nullptr, &m_MipViews[mip]) != VK_SUCCESS) {
                return false;
            }
        }

        m_PrefilterSetLayout = RHI::CreateComputeSetLayout(device, {
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        });
        if (m_PrefilterSetLayout == VK_NULL_HANDLE) {
            return false;
        }
        m_PrefilterPipeline = RHI::CreateComputePipeline(device, m_Context->GetPipelineCache(),
                                                         kPrefilterShader, "probe_prefilter",
                                                         {m_PrefilterSetLayout}, 0);
        if (!m_PrefilterPipeline.IsValid()) {
            return false;
        }

        const VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, mips},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, mips},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, mips},
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = mips;
        poolInfo.poolSizeCount = 3;
        poolInfo.pPoolSizes = poolSizes;
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_PrefilterPool) != VK_SUCCESS) {
            return false;
        }

        for (uint32_t mip = 0; mip < mips; ++mip) {
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = m_PrefilterPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &m_PrefilterSetLayout;
            if (vkAllocateDescriptorSets(device, &allocInfo, &m_PrefilterSets[mip]) != VK_SUCCESS) {
                return false;
            }
            if (!RHI::CreateGpuBuffer(m_Context->GetAllocator(), sizeof(Math::Vec4),
                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true,
                                      m_PrefilterUniforms[mip])) {
                return false;
            }
        }
        return true;
    }

    void EnvironmentProbe::PrefilterMips(VkCommandBuffer cmd) {
        if (!m_PrefilterPipeline.IsValid()) {
            return;
        }
        const uint32_t mips = std::min(m_MipLevels, kMaxMips);

        // Mip 0 stays the captured environment - a mirror should reflect what was
        // there, not a convolution of it. Everything above it is rebuilt.
        RHI::TransitionImage(cmd, m_Cube, VK_IMAGE_LAYOUT_GENERAL);

        for (uint32_t mip = 1; mip < mips; ++mip) {
            const uint32_t size = std::max(m_Resolution >> mip, 1u);
            const float roughness = static_cast<float>(mip) / static_cast<float>(mips - 1);
            // Rougher levels need more samples: the lobe is wider, so the same
            // count would leave it visibly noisy.
            const float sampleCount = 32.0f + roughness * 96.0f;

            const Math::Vec4 params(roughness, static_cast<float>(size), sampleCount, 0.0f);
            if (m_PrefilterUniforms[mip].Mapped) {
                std::memcpy(m_PrefilterUniforms[mip].Mapped, &params, sizeof(params));
            }

            VkDescriptorImageInfo sourceInfo{m_Sampler, m_Cube.View, VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo targetInfo{VK_NULL_HANDLE, m_MipViews[mip],
                                             VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorBufferInfo bufferInfo{m_PrefilterUniforms[mip].Buffer, 0,
                                              sizeof(Math::Vec4)};

            VkWriteDescriptorSet writes[3]{};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = m_PrefilterSets[mip];
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].pImageInfo = &sourceInfo;
            writes[1] = writes[0];
            writes[1].dstBinding = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[1].pImageInfo = &targetInfo;
            writes[2] = writes[0];
            writes[2].dstBinding = 2;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[2].pImageInfo = nullptr;
            writes[2].pBufferInfo = &bufferInfo;
            vkUpdateDescriptorSets(m_Context->GetDevice(), 3, writes, 0, nullptr);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_PrefilterPipeline.Pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    m_PrefilterPipeline.Layout, 0, 1, &m_PrefilterSets[mip],
                                    0, nullptr);
            vkCmdDispatch(cmd, (size + 7) / 8, (size + 7) / 8, kFaceCount);

            // Each level is written before the next reads the cube again.
            VkMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier,
                                 0, nullptr, 0, nullptr);
        }

        RHI::TransitionImage(cmd, m_Cube, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void EnvironmentProbe::RequestBake(const Math::Vec3& position) {
        if (!IsInitialized()) {
            return;
        }
        m_Position = position;
        m_Face = 0;
        m_Baking = true;
        // The old cube stays live until the new one is complete: swapping a
        // half-baked probe in would show three fresh faces and three stale ones.
        m_Stats.Baking = true;
        m_Stats.FacesCaptured = 0;
        m_Stats.Position = position;
    }

    void EnvironmentProbe::GetFaceView(Math::Mat4& view, Math::Mat4& projection) const {
        const FaceBasis& face = kFaces[std::min(m_Face, kFaceCount - 1)];
        view = glm::lookAt(m_Position, m_Position + face.Forward, face.Up);

        // Ninety degrees, square: anything else leaves seams between faces.
        projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.05f, 500.0f);
        projection[1][1] *= -1.0f;
    }

    void EnvironmentProbe::PrepareForSampling(VkCommandBuffer cmd) {
        if (!IsInitialized()) {
            return;
        }
        RHI::TransitionImage(cmd, m_Cube, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void EnvironmentProbe::CaptureFace(VkCommandBuffer cmd, RHI::GpuImage& source) {
        if (!IsInitialized() || !m_Baking || !source.IsValid()) {
            return;
        }

        const VkImageLayout sourceLayout = source.Layout;
        RHI::TransitionImage(cmd, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        RHI::TransitionImage(cmd, m_Cube, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // The frame is whatever the render resolution is and rarely square, so
        // this is a scaling blit rather than a copy. Faces come out square
        // because the destination is, which is what the cube needs.
        VkImageBlit blit{};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.layerCount = 1;
        blit.srcOffsets[1] = {static_cast<int32_t>(source.Extent.width),
                              static_cast<int32_t>(source.Extent.height), 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.baseArrayLayer = m_Face;
        blit.dstSubresource.layerCount = 1;
        blit.dstOffsets[1] = {static_cast<int32_t>(m_Resolution),
                              static_cast<int32_t>(m_Resolution), 1};
        vkCmdBlitImage(cmd, source.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       m_Cube.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                       VK_FILTER_LINEAR);

        RHI::TransitionImage(cmd, source, sourceLayout);

        ++m_Face;
        m_Stats.FacesCaptured = m_Face;
        if (m_Face >= kFaceCount) {
            // The box chain first, because the prefilter samples the cube with a
            // mip bias and needs something below level zero to read.
            GenerateMips(cmd);
            PrefilterMips(cmd);
            m_Baking = false;
            m_Ready = true;
            m_Stats.Baking = false;
            m_Stats.Ready = true;
            ENGINE_CORE_INFO("Environment probe baked at ({:.1f}, {:.1f}, {:.1f})",
                             m_Position.x, m_Position.y, m_Position.z);
        } else {
            RHI::TransitionImage(cmd, m_Cube, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }

    void EnvironmentProbe::GenerateMips(VkCommandBuffer cmd) {
        // Successive halving blits across all six faces at once. Roughness picks
        // a mip from the chain, so a rough surface reflects a blurred
        // environment - not the right convolution, but the right direction.
        int32_t width = static_cast<int32_t>(m_Resolution);
        int32_t height = static_cast<int32_t>(m_Resolution);

        for (uint32_t mip = 1; mip < m_MipLevels; ++mip) {
            RHI::TransitionImageRange(cmd, m_Cube.Image, VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 1,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, kFaceCount);

            VkImageBlit blit{};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = mip - 1;
            blit.srcSubresource.layerCount = kFaceCount;
            blit.srcOffsets[1] = {width, height, 1};
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = mip;
            blit.dstSubresource.layerCount = kFaceCount;
            width = std::max(width / 2, 1);
            height = std::max(height / 2, 1);
            blit.dstOffsets[1] = {width, height, 1};

            vkCmdBlitImage(cmd, m_Cube.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           m_Cube.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                           VK_FILTER_LINEAR);

            RHI::TransitionImageRange(cmd, m_Cube.Image, VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 1,
                                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kFaceCount);
        }

        RHI::TransitionImageRange(cmd, m_Cube.Image, VK_IMAGE_ASPECT_COLOR_BIT,
                                  m_MipLevels - 1, 1,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kFaceCount);
        // The tracked layout is now what every mip agrees on.
        m_Cube.Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    void EnvironmentProbe::Shutdown() {
        if (!m_Context) {
            return;
        }
        VkDevice device = m_Context->GetDevice();
        vkDeviceWaitIdle(device);

        for (VkImageView& view : m_MipViews) {
            if (view != VK_NULL_HANDLE) {
                vkDestroyImageView(device, view, nullptr);
                view = VK_NULL_HANDLE;
            }
        }
        for (auto& buffer : m_PrefilterUniforms) {
            RHI::DestroyGpuBuffer(m_Context->GetAllocator(), buffer);
        }
        RHI::DestroyComputePipeline(device, m_PrefilterPipeline);
        if (m_PrefilterPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, m_PrefilterPool, nullptr);
            m_PrefilterPool = VK_NULL_HANDLE;
        }
        if (m_PrefilterSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, m_PrefilterSetLayout, nullptr);
            m_PrefilterSetLayout = VK_NULL_HANDLE;
        }
        RHI::DestroyGpuImage(device, m_Context->GetAllocator(), m_Cube);
        if (m_Sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, m_Sampler, nullptr);
            m_Sampler = VK_NULL_HANDLE;
        }
        m_Baking = false;
        m_Ready = false;
        m_Face = 0;
        m_Stats = EnvironmentProbeStats{};
        m_Context = nullptr;
    }

} // namespace Renderer
} // namespace Core
