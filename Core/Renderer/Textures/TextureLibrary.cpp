#include "TextureLibrary.h"

#include "Core/Log.h"
#include "Core/RHI/Vulkan/VulkanContext.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_DXT_IMPLEMENTATION
#include <stb_dxt.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

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

        // BC3: 4x4 texels per 16-byte block. A mip smaller than a block still
        // occupies one, which is why the block counts round up rather than down.
        constexpr uint32_t kBlockSize = 4;
        constexpr uint32_t kBlockBytes = 16;

        uint32_t BlocksAcross(uint32_t extent) {
            return (std::max(extent, 1u) + kBlockSize - 1) / kBlockSize;
        }

        uint64_t CompressedLevelBytes(uint32_t width, uint32_t height) {
            return static_cast<uint64_t>(BlocksAcross(width)) * BlocksAcross(height) * kBlockBytes;
        }

        // Compresses one RGBA8 level into BC3. Blocks that run off the edge of a
        // non-multiple-of-four image are padded by clamping, not by zero: a black
        // border would bleed in through the lower mips.
        void CompressLevelToBC3(const uint8_t* rgba, uint32_t width, uint32_t height, uint8_t* out) {
            const uint32_t blocksX = BlocksAcross(width);
            const uint32_t blocksY = BlocksAcross(height);

            for (uint32_t blockY = 0; blockY < blocksY; ++blockY) {
                for (uint32_t blockX = 0; blockX < blocksX; ++blockX) {
                    uint8_t block[kBlockSize * kBlockSize * 4];
                    for (uint32_t y = 0; y < kBlockSize; ++y) {
                        const uint32_t sourceY = std::min(blockY * kBlockSize + y, height - 1);
                        for (uint32_t x = 0; x < kBlockSize; ++x) {
                            const uint32_t sourceX = std::min(blockX * kBlockSize + x, width - 1);
                            const uint8_t* texel = rgba + (static_cast<std::size_t>(sourceY) * width + sourceX) * 4;
                            uint8_t* target = block + (static_cast<std::size_t>(y) * kBlockSize + x) * 4;
                            target[0] = texel[0];
                            target[1] = texel[1];
                            target[2] = texel[2];
                            target[3] = texel[3];
                        }
                    }
                    // Alpha on: BC3 rather than BC1, so a cutout or blended
                    // texture survives the round trip.
                    stb_compress_dxt_block(out + (static_cast<std::size_t>(blockY) * blocksX + blockX) * kBlockBytes,
                                           block, 1, STB_DXT_NORMAL);
                }
            }
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

        // BC support is a format-feature query, not a device feature. Desktop
        // GPUs all have it; plenty of mobile ones do not, and asking rather than
        // assuming is the difference between a compressed texture and a device
        // lost.
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(context->GetPhysicalDevice(),
                                            VK_FORMAT_BC3_UNORM_BLOCK, &properties);
        m_SupportsBlockCompression =
            (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
        m_Stats.BlockCompressionSupported = m_SupportsBlockCompression;

        ENGINE_CORE_INFO("Texture library ready (BC3 block compression {})",
                         m_SupportsBlockCompression ? "supported" : "unavailable on this device");
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

        texture.UncompressedBytes = texture.SizeBytes;
        texture.Compressed = options.Compress && m_SupportsBlockCompression;

        const bool uploaded = texture.Compressed
                                  ? UploadCompressedTexture(texture, pixels, options)
                                  : UploadTexture(texture, pixels, options);
        if (!uploaded) {
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
            m_Stats.UncompressedBytes -= m_Textures[index].UncompressedBytes;
            m_Stats.CompressedTextures -= m_Textures[index].Compressed ? 1u : 0u;
            RHI::DestroyGpuImage(m_Context->GetDevice(), m_Context->GetAllocator(),
                                 m_Textures[index].Image);
            m_Textures[index] = std::move(texture);
            m_Stats.TotalBytes += m_Textures[index].SizeBytes;
            m_Stats.UncompressedBytes += m_Textures[index].UncompressedBytes;
            m_Stats.CompressedTextures += m_Textures[index].Compressed ? 1u : 0u;
            ++m_Revision;
            ENGINE_CORE_INFO("Texture '{}' replaced ({}x{}, {} mips)", name, width, height,
                             m_Textures[index].MipLevels);
            return index;
        }

        const uint32_t index = static_cast<uint32_t>(m_Textures.size());
        m_Stats.TotalBytes += texture.SizeBytes;
        m_Stats.UncompressedBytes += texture.UncompressedBytes;
        m_Stats.CompressedTextures += texture.Compressed ? 1u : 0u;
        m_Textures.push_back(std::move(texture));
        m_NameToIndex[name] = index;
        m_Stats.TextureCount = static_cast<uint32_t>(m_Textures.size());
        ++m_Revision;

        ENGINE_CORE_INFO("Texture '{}' loaded ({}x{}, {} mips, {}, {})", name, width, height,
                         m_Textures[index].MipLevels, options.SRGB ? "sRGB" : "linear",
                         m_Textures[index].Compressed ? "BC3" : "RGBA8");
        return index;
    }

    bool TextureLibrary::UploadCompressedTexture(GpuTexture& texture, const uint8_t* pixels,
                                                 const TextureImportOptions& options) {
        VkDevice device = m_Context->GetDevice();
        VmaAllocator allocator = m_Context->GetAllocator();

        RHI::GpuImageDesc desc{};
        desc.Width = texture.Width;
        desc.Height = texture.Height;
        desc.MipLevels = texture.MipLevels;
        desc.Format = options.SRGB ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK;
        // No TRANSFER_SRC: a compressed image cannot be blitted, which is why
        // the mip chain is built on the CPU below rather than on the GPU.
        desc.Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        desc.DebugName = texture.Name.c_str();
        if (!RHI::CreateGpuImage(device, allocator, desc, texture.Image)) {
            return false;
        }

        // Build every level first so the total staging size is known up front.
        struct Level {
            uint32_t Width;
            uint32_t Height;
            uint64_t Offset;
            uint64_t Bytes;
        };
        std::vector<Level> levels;
        levels.reserve(texture.MipLevels);

        uint64_t totalBytes = 0;
        uint32_t levelWidth = texture.Width;
        uint32_t levelHeight = texture.Height;
        for (uint32_t mip = 0; mip < texture.MipLevels; ++mip) {
            const uint64_t bytes = CompressedLevelBytes(levelWidth, levelHeight);
            levels.push_back({levelWidth, levelHeight, totalBytes, bytes});
            totalBytes += bytes;
            levelWidth = std::max(levelWidth / 2, 1u);
            levelHeight = std::max(levelHeight / 2, 1u);
        }

        RHI::GpuBuffer staging{};
        if (!RHI::CreateGpuBuffer(allocator, totalBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, staging)) {
            RHI::DestroyGpuImage(device, allocator, texture.Image);
            return false;
        }

        auto* stagingBytes = static_cast<uint8_t*>(staging.Mapped);
        std::vector<uint8_t> levelPixels;
        const uint8_t* sourcePixels = pixels;

        for (uint32_t mip = 0; mip < texture.MipLevels; ++mip) {
            const Level& level = levels[mip];
            if (mip > 0) {
                // Downsample from the original each time rather than from the
                // previous mip: repeated halving of an already-resampled image
                // compounds its error.
                levelPixels.assign(static_cast<std::size_t>(level.Width) * level.Height * 4, 0);
                // sRGB data must be resampled in linear light; averaging gamma-
                // encoded texels darkens every mip.
                if (options.SRGB) {
                    stbir_resize_uint8_srgb(pixels, static_cast<int>(texture.Width),
                                            static_cast<int>(texture.Height), 0,
                                            levelPixels.data(), static_cast<int>(level.Width),
                                            static_cast<int>(level.Height), 0, STBIR_RGBA);
                } else {
                    stbir_resize_uint8_linear(pixels, static_cast<int>(texture.Width),
                                              static_cast<int>(texture.Height), 0,
                                              levelPixels.data(), static_cast<int>(level.Width),
                                              static_cast<int>(level.Height), 0, STBIR_RGBA);
                }
                sourcePixels = levelPixels.data();
            }
            CompressLevelToBC3(sourcePixels, level.Width, level.Height,
                               stagingBytes + level.Offset);
        }

        VkCommandBuffer cmd = RHI::BeginImmediateCommands(device, m_Context->GetCommandPool());
        if (cmd == VK_NULL_HANDLE) {
            RHI::DestroyGpuBuffer(allocator, staging);
            RHI::DestroyGpuImage(device, allocator, texture.Image);
            return false;
        }

        RHI::TransitionImageRange(cmd, texture.Image.Image, VK_IMAGE_ASPECT_COLOR_BIT,
                                  0, texture.MipLevels,
                                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        std::vector<VkBufferImageCopy> copies(texture.MipLevels);
        for (uint32_t mip = 0; mip < texture.MipLevels; ++mip) {
            copies[mip] = {};
            copies[mip].bufferOffset = levels[mip].Offset;
            copies[mip].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copies[mip].imageSubresource.mipLevel = mip;
            copies[mip].imageSubresource.layerCount = 1;
            copies[mip].imageExtent = {levels[mip].Width, levels[mip].Height, 1};
        }
        vkCmdCopyBufferToImage(cmd, staging.Buffer, texture.Image.Image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<uint32_t>(copies.size()), copies.data());

        RHI::TransitionImageRange(cmd, texture.Image.Image, VK_IMAGE_ASPECT_COLOR_BIT,
                                  0, texture.MipLevels,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        const bool submitted = RHI::EndImmediateCommands(device, m_Context->GetCommandPool(),
                                                         m_Context->GetGraphicsQueue(), cmd);
        RHI::DestroyGpuBuffer(allocator, staging);

        if (!submitted) {
            RHI::DestroyGpuImage(device, allocator, texture.Image);
            return false;
        }

        texture.Image.Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        texture.SizeBytes = totalBytes;
        return true;
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
