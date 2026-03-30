#pragma once

#include <cstdint>

namespace Core {
namespace RHI {

    enum class TextureFormat {
        RGBA8_UNORM,
        R32_FLOAT,
        D32_SFLOAT,
    };

    enum class TextureUsage {
        Sampled,
        Storage,
        RenderTarget,
        DepthStencil
    };

    struct TextureDescriptor {
        uint32_t width;
        uint32_t height;
        uint32_t depth;
        TextureFormat format;
        TextureUsage usage;
    };

    class RHITexture {
    public:
        virtual ~RHITexture() = default;

        virtual uint32_t GetWidth() const = 0;
        virtual uint32_t GetHeight() const = 0;
        virtual uint32_t GetDepth() const = 0;
        virtual TextureFormat GetFormat() const = 0;
    };

} // namespace RHI
} // namespace Core
