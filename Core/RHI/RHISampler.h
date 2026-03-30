#pragma once

#include <cstdint>

namespace Core {
namespace RHI {

    enum class FilterMode {
        Nearest,
        Linear
    };

    enum class AddressMode {
        Repeat,
        MirroredRepeat,
        ClampToEdge,
        ClampToBorder
    };

    struct SamplerDescriptor {
        FilterMode minFilter = FilterMode::Linear;
        FilterMode magFilter = FilterMode::Linear;
        FilterMode mipmapMode = FilterMode::Linear;
        AddressMode addressModeU = AddressMode::Repeat;
        AddressMode addressModeV = AddressMode::Repeat;
        AddressMode addressModeW = AddressMode::Repeat;
        float mipLodBias = 0.0f;
        float maxAnisotropy = 1.0f;
        float minLod = 0.0f;
        float maxLod = 1.0f;
    };

    class RHISampler {
    public:
        virtual ~RHISampler() = default;
    };

} // namespace RHI
} // namespace Core
