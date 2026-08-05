#include "TextureLibrary.h"

#include "Core/Log.h"
#include "Core/RHI/Vulkan/VulkanContext.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Core {
namespace Renderer {

    namespace {

        VkSampler CreateSampler(VkDevice device, VkSamplerAddressMode addressMode, float maxLod) {
            VkSamplerCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            info.magFilter = VK_FILTER_LINEAR;
            info.minFilter = VK_FILTER_LINEAR;
            info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            info.addressModeU = addressMode;
            info.addressModeV = addressMode;
            info.addressModeW = addressMode;
            info.minLod = 0.0f;
            info.maxLod = maxLod;
            info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
            // Anisotropy is left off: it needs the device feature enabled and a
            // per-device max, and trilinear is already correct, just softer at
            // grazing angles.
            info.anisotropyEnable = VK_FALSE;

            VkSampler sampler = VK_NULL_HANDLE;
            if (vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS) {
                ENGINE_CORE_ERROR("TextureLibrary: sampler creation failed");
                return VK_NULL_HANDLE;
            }
            return sampler;
        }

        uint32_t MipCountFor(uint32_t width, uint32_t height) {
            return 1u + static_cast<uint32_t>(
                       std::floor(std::log2(static_cast<float>(std::max(width, height)))));
        }

    } // namespace

    TextureLibrary& TextureLibrary::Get() {
        static TextureLibrary instance;
        return instance;
    }

    bool TextureLibrary::Initialize(RHI::VulkanContext* context) {
        if (!context || context->GetDevice() == VK_NULL_HANDLE) {
            return false;
        }
        Shutdown();
        m_Context = context;

        VkDevice device = context->GetDevice();
        m_RepeatSampler = CreateSampler(device, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_LOD_CLAMP_NONE);
        m_ClampSampler = CreateSampler(device, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_LOD_CLAMP_NONE);
        if (m_RepeatSampler == VK_NULL_HANDLE || m_ClampSampler == VK_NULL_HANDLE) {
            Shutdown();
            return false;
        }

        ENGINE_CORE_INFO("Texture library ready");
        return true;
    }

    void TextureLibrary::Clear() {
        if (!m_Context) {
            m_Textures.clear();
            m_NameToIndex.clear();
            return;
        }
        VkDevice device = m_Context->GetDevice();
        VmaAllocator allocator = m_Context->GetAllocator();
        vkDeviceWaitIdle(device);
        for (auto& texture : m_Textures) {
            RHI::DestroyGpuImage(device, allocator, texture.Image);
        }
        m_Textures.clear();
        m_NameToIndex.clear();
        m_Stats = TextureLibraryStats{};
        ++m_Revision;
    }

    void TextureLibrary::Shutdown() {
        if (!m_Context) {
            return;
        }
        Clear();

        VkDevice device = m_Context->GetDevice();
        for (VkSampler* sampler : {&m_RepeatSampler, &m_ClampSampler}) {
            if (*sampler != VK_NULL_HANDLE) {
                vkDestroySampler(device, *sampler, nullptr);
                *sampler = VK_NULL_HANDLE;
            }
        }
        m_Context = nullptr;
    }

    uint32_t TextureLibrary::FindTexture(const std::string& name) const {
        auto it = m_NameToIndex.find(name);
        return it == m_NameToIndex.end() ? UINT32_MAX : it->second;
    }

    const GpuTexture* TextureLibrary::GetTexture(uint32_t index) const {
        return index < m_Textures.size() ? &m_Textures[index] : nullptr;
    }

    uint32_t TextureLibrary::LoadFromFile(const std::string& name,
                                          const std::string& path,
                                          const TextureImportOptions& options) {
        if (!m_Context) {
            ENGINE_CORE_ERROR("TextureLibrary::LoadFromFile before Initialize");
            return UINT32_MAX;
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        // Forced to 4 channels: Vulkan support for 3-channel formats is optional
        // on desktop and absent on plenty of hardware.
        stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels || width <= 0 || height <= 0) {
            ENGINE_CORE_ERROR("TextureLibrary: failed to load '{}': {}",
                              path, stbi_failure_reason() ? stbi_failure_reason() : "unknown");
            if (pixels) {
                stbi_image_free(pixels);
            }
            ++m_Stats.FailedLoads;
            return UINT32_MAX;
        }

        const uint32_t index = LoadFromMemory(name, pixels, static_cast<uint32_t>(width),
                                              static_cast<uint32_t>(height), options);
        stbi_image_free(pixels);

        if (index != UINT32_MAX) {
            m_Textures[index].SourcePath = path;
            m_Textures[index].Channels = static_cast<uint32_t>(channels);
        }
        return index;
    }

    uint32_t TextureLibrary::LoadFromEncodedMemory(const std::string& name,
                                                  const uint8_t* encoded,
                                                  std::size_t byteCount,
                                                  const TextureImportOptions& options) {
        if (!m_Context || !encoded || byteCount == 0) {
            ++m_Stats.FailedLoads;
            return UINT32_MAX;
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load_from_memory(encoded, static_cast<int>(byteCount),
                                                &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels || width <= 0 || height <= 0) {
            ENGINE_CORE_ERROR("TextureLibrary: failed to decode embedded image '{}': {}",
                              name, stbi_failure_reason() ? stbi_failure_reason() : "unknown");
            if (pixels) {
                stbi_image_free(pixels);
            }
            ++m_Stats.FailedLoads;
            return UINT32_MAX;
        }

        const uint32_t index = LoadFromMemory(name, pixels, static_cast<uint32_t>(width),
                                              static_cast<uint32_t>(height), options);
        stbi_image_free(pixels);
        if (index != UINT32_MAX) {
            m_Textures[index].Channels = static_cast<uint32_t>(channels);
            m_Textures[index].SourcePath = "<embedded>";
        }
        return index;
    }

    uint32_t TextureLibrary::LoadFromMemory(const std::string& name,
                                            const uint8_t* pixels,
                                            uint32_t width,
                                            uint32_t height,
                                            const TextureImportOptions& options) {
        if (!m_Context || !pixels || width == 0 || height == 0) {
            ++m_Stats.FailedLoads;
            return UINT32_MAX;
        }

        GpuTexture texture;
        texture.Name = name;
        texture.Width = width;
        texture.Height = height;
        texture.SRGB = options.SRGB;
        texture.MipLevels = options.GenerateMips ? MipCountFor(width, height) : 1u;
        texture.SizeBytes = static_cast<uint64_t>(width) * height * 4u;

        if (!UploadTexture(texture, pixels, options)) {
            ++m_Stats.FailedLoads;
            return UINT32_MAX;
        }

        // Replacing an existing name keeps the index stable so materials that
        // already resolved against it do not have to be rebound.
        auto existing = m_NameToIndex.find(name);
        if (existing != m_NameToIndex.end()) {
            const uint32_t index = existing->second;
            vkDeviceWaitIdle(m_Context->GetDevice());
            m_Stats.TotalBytes -= m_Textures[index].SizeBytes;
            RHI::DestroyGpuImage(m_Context->GetDevice(), m_Context->GetAllocator(),
                                 m_Textures[index].Image);
            m_Textures[index] = std::move(texture);
            m_Stats.TotalBytes += m_Textures[index].SizeBytes;
            ++m_Revision;
            ENGINE_CORE_INFO("Texture '{}' replaced ({}x{}, {} mips)", name, width, height,
                             m_Textures[index].MipLevels);
            return index;
        }

        const uint32_t index = static_cast<uint32_t>(m_Textures.size());
        m_Stats.TotalBytes += texture.SizeBytes;
        m_Textures.push_back(std::move(texture));
        m_NameToIndex[name] = index;
        m_Stats.TextureCount = static_cast<uint32_t>(m_Textures.size());
        ++m_Revision;

        ENGINE_CORE_INFO("Texture '{}' loaded ({}x{}, {} mips, {})", name, width, height,
                         m_Textures[index].MipLevels, options.SRGB ? "sRGB" : "linear");
        return index;
    }

    bool TextureLibrary::UploadTexture(GpuTexture& texture, const uint8_t* pixels,
                                       const TextureImportOptions& options) {
        VkDevice device = m_Context->GetDevice();
        VmaAllocator allocator = m_Context->GetAllocator();

        RHI::GpuImageDesc desc{};
        desc.Width = texture.Width;
        desc.Height = texture.Height;
        desc.MipLevels = texture.MipLevels;
        desc.Format = options.SRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        // TRANSFER_SRC as well as DST: mip generation blits level N-1 into N, so
        // every level is both a source and a destination at some point.
        desc.Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        desc.DebugName = texture.Name.c_str();
        if (!RHI::CreateGpuImage(device, allocator, desc, texture.Image)) {
            return false;
        }

        const VkDeviceSize imageBytes = static_cast<VkDeviceSize>(texture.Width) * texture.Height * 4u;
        RHI::GpuBuffer staging{};
        if (!RHI::CreateGpuBuffer(allocator, imageBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, staging)) {
            RHI::DestroyGpuImage(device, allocator, texture.Image);
            return false;
        }
        std::memcpy(staging.Mapped, pixels, static_cast<std::size_t>(imageBytes));

        VkCommandBuffer cmd = RHI::BeginImmediateCommands(device, m_Context->GetCommandPool());
        if (cmd == VK_NULL_HANDLE) {
            RHI::DestroyGpuBuffer(allocator, staging);
            RHI::DestroyGpuImage(device, allocator, texture.Image);
            return false;
        }

        RHI::TransitionImageRange(cmd, texture.Image.Image, VK_IMAGE_ASPECT_COLOR_BIT,
                                  0, texture.MipLevels,
                                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel = 0;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {texture.Width, texture.Height, 1};
        vkCmdCopyBufferToImage(cmd, staging.Buffer, texture.Image.Image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        // Successive halving blits. Each level waits for the one above it to be
        // readable, so the chain is built in a single command buffer.
        int32_t mipWidth = static_cast<int32_t>(texture.Width);
        int32_t mipHeight = static_cast<int32_t>(texture.Height);
        for (uint32_t mip = 1; mip < texture.MipLevels; ++mip) {
            RHI::TransitionImageRange(cmd, texture.Image.Image, VK_IMAGE_ASPECT_COLOR_BIT,
                                      mip - 1, 1,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            const int32_t nextWidth = std::max(mipWidth / 2, 1);
            const int32_t nextHeight = std::max(mipHeight / 2, 1);

            VkImageBlit blit{};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = mip - 1;
            blit.srcSubresource.layerCount = 1;
            blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = mip;
            blit.dstSubresource.layerCount = 1;
            blit.dstOffsets[1] = {nextWidth, nextHeight, 1};

            vkCmdBlitImage(cmd,
                           texture.Image.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           texture.Image.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &blit, VK_FILTER_LINEAR);

            RHI::TransitionImageRange(cmd, texture.Image.Image, VK_IMAGE_ASPECT_COLOR_BIT,
                                      mip - 1, 1,
                                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            mipWidth = nextWidth;
            mipHeight = nextHeight;
        }

        // The last level was never blitted from, so it is still TRANSFER_DST.
        RHI::TransitionImageRange(cmd, texture.Image.Image, VK_IMAGE_ASPECT_COLOR_BIT,
                                  texture.MipLevels - 1, 1,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        const bool submitted = RHI::EndImmediateCommands(device, m_Context->GetCommandPool(),
                                                         m_Context->GetGraphicsQueue(), cmd);
        RHI::DestroyGpuBuffer(allocator, staging);

        if (!submitted) {
            RHI::DestroyGpuImage(device, allocator, texture.Image);
            return false;
        }

        // Every subresource ended up here; record it so later transitions start
        // from the right place.
        texture.Image.Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        return true;
    }

} // namespace Renderer
} // namespace Core
