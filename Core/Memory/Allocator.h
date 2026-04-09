#pragma once

#include <cstddef>

namespace Core {
namespace Memory {

class Allocator {
public:
    virtual ~Allocator() noexcept = default;

    // Allocate an aligned block of memory
    [[nodiscard]] virtual void* Allocate(size_t size, size_t alignment = alignof(std::max_align_t)) = 0;
    
    // Free a previously allocated block of memory
    virtual void Free(void* ptr) = 0;
    
    // Initialize the allocator. Returns true on success, false on failure.
    [[nodiscard]] virtual bool Init() = 0;
};

} // namespace Memory
} // namespace Core