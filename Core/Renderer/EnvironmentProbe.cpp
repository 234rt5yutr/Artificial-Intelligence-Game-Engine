#include "EnvironmentProbe.h"

#include "Core/Log.h"
#include "Core/RHI/Vulkan/VulkanContext.h"

#include <algorithm>

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
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
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

        m_Stats.Resolution = m_Resolution;
        m_Stats.MipLevels = m_MipLevels;
        ENGINE_CORE_INFO("Environment probe ready ({}x{} cube, {} mips)",
                         m_Resolution, m_Resolution, m_MipLevels);
        return true;
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
            GenerateMips(cmd);
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
