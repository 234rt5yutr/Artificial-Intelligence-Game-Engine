#pragma once

#include <cstdint>

namespace Core {
namespace RHI {

    enum class TextureFormat {
        RGBA8_UNORM,
        RGBA16_SFLOAT,      // HDR support for particles
        R32_FLOAT,
        D32_SFLOAT,
    };

    enum class TextureUsage {
        Sampled,
        Storage,
        RenderTarget,
        ColorAttachment,    // Color render target
        DepthStencil
    };

    enum class TextureDimension {
        Texture1D,
        Texture2D,
        Texture3D,
        TextureCube
    };

    struct TextureDescriptor {
        TextureDimension dimension = TextureDimension::Texture2D;
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t depth = 1;
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        TextureFormat format = TextureFormat::RGBA8_UNORM;
        TextureUsage usage = TextureUsage::Sampled;
    };

    class RHITexture {
    public:
        virtual ~RHITexture() = default;

        virtual TextureDimension GetDimension() const = 0;
        virtual uint32_t GetWidth() const = 0;
        virtual uint32_t GetHeight() const = 0;
        virtual uint32_t GetDepth() const = 0;
        virtual uint32_t GetMipLevels() const = 0;
        virtual uint32_t GetArrayLayers() const = 0;
        virtual TextureFormat GetFormat() const = 0;
        virtual TextureUsage GetUsage() const = 0;
    };

} // namespace RHI
} // namespace Core
