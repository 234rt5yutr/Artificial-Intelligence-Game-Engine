#pragma once

// Texture import and residency.
//
// The material graph could declare texture slots but nothing could fill them:
// the engine had no image import path at all, so every `TextureSample` node
// resolved to a 1x1 white placeholder and the whole authoring surface was
// decorative. `AssetCooker`/`AssetLoader` read and write a cooked binary format,
// but nothing ever produced one from a source image.
//
// This loads real images through stb_image (already a declared dependency),
// uploads them with a full mip chain, and hands out indices that a material's
// texture slots resolve against by name.

#include "Core/RHI/Vulkan/VulkanGpuResources.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Core {
namespace RHI { class VulkanContext; }

namespace Renderer {

    struct GpuTexture {
        RHI::GpuImage Image;
        std::string Name;
        std::string SourcePath;
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint32_t MipLevels = 1;
        uint32_t Channels = 4;
        // Base colour and emissive are authored in sRGB; normal, roughness, and
        // metallic are linear data and must not be gamma-decoded on sample.
        bool SRGB = true;
        uint64_t SizeBytes = 0;
    };

    struct TextureImportOptions {
        // sRGB for colour, linear for data maps. Getting this wrong on a normal
        // map bends the lighting in a way that is easy to mistake for a shading
        // bug.
        bool SRGB = true;
        bool GenerateMips = true;
        // Repeat suits tiling surface maps; clamp suits UI and lookup tables.
        bool Repeat = true;
    };

    struct TextureLibraryStats {
        uint32_t TextureCount = 0;
        uint64_t TotalBytes = 0;
        uint32_t FailedLoads = 0;
    };

    class TextureLibrary {
    public:
        static TextureLibrary& Get();

        bool Initialize(RHI::VulkanContext* context);
        void Shutdown();
        bool IsInitialized() const { return m_Context != nullptr; }

        // Loads an image from disk and uploads it. Returns the index, or
        // UINT32_MAX on failure. Re-loading an existing name replaces it.
        uint32_t LoadFromFile(const std::string& name,
                              const std::string& path,
                              const TextureImportOptions& options = {});

        // Registers raw RGBA8 pixels. Used by the glTF importer for embedded
        // images, which never touch the filesystem.
        uint32_t LoadFromMemory(const std::string& name,
                                const uint8_t* pixels,
                                uint32_t width,
                                uint32_t height,
                                const TextureImportOptions& options = {});

        // Decodes an encoded image already in memory (a PNG or JPEG embedded in
        // a GLB, say). Same formats as LoadFromFile - it is the same decoder.
        uint32_t LoadFromEncodedMemory(const std::string& name,
                                       const uint8_t* encoded,
                                       std::size_t byteCount,
                                       const TextureImportOptions& options = {});

        uint32_t FindTexture(const std::string& name) const;
        const GpuTexture* GetTexture(uint32_t index) const;
        uint32_t GetTextureCount() const { return static_cast<uint32_t>(m_Textures.size()); }
        const std::vector<GpuTexture>& GetTextures() const { return m_Textures; }

        VkSampler GetSampler(bool repeat) const { return repeat ? m_RepeatSampler : m_ClampSampler; }
        // Bumped whenever a texture is added or replaced, so material descriptor
        // sets know they have to be rewritten.
        uint64_t GetRevision() const { return m_Revision; }

        const TextureLibraryStats& GetStats() const { return m_Stats; }
        void Clear();

    private:
        TextureLibrary() = default;

        // Uploads through a staging buffer and blits the mip chain. Returns
        // false and leaves nothing registered on failure.
        bool UploadTexture(GpuTexture& texture, const uint8_t* pixels, const TextureImportOptions& options);

        RHI::VulkanContext* m_Context = nullptr;
        std::vector<GpuTexture> m_Textures;
        std::unordered_map<std::string, uint32_t> m_NameToIndex;
        VkSampler m_RepeatSampler = VK_NULL_HANDLE;
        VkSampler m_ClampSampler = VK_NULL_HANDLE;
        uint64_t m_Revision = 0;
        TextureLibraryStats m_Stats{};
    };

} // namespace Renderer
} // namespace Core
