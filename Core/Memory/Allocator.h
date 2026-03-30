#pragma once

#include <cstddef>

namespace Core {
namespace Memory {

class Allocator {
public:
    virtual ~Allocator() = default;

    // Allocate an aligned block of memory
    virtual void* Allocate(size_t size, size_t alignment = alignof(std::max_align_t)) = 0;
    
    // Free a previously allocated block of memory
    virtual void Free(void* ptr) = 0;
    
    // Initialize the allocator
    virtual void Init() = 0;
};

} // namespace Memory
} // namespace Core