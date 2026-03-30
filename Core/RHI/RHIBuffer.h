#pragma once

#include <cstdint>
#include <cstddef>

namespace Core {
namespace RHI {

    enum class BufferUsage {
        Vertex,
        Index,
        Uniform,
        Storage,
        Staging
    };

    struct BufferDescriptor {
        std::size_t size;
        BufferUsage usage;
        bool mapped;
    };

    class RHIBuffer {
    public:
        virtual ~RHIBuffer() = default;

        virtual void Map(void** outData) = 0;
        virtual void Unmap() = 0;
        virtual std::size_t GetSize() const = 0;
    };

} // namespace RHI
} // namespace Core
