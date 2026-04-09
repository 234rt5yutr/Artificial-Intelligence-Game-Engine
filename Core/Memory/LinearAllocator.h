#pragma once

#include "Allocator.h"
#include <cstddef>

namespace Core {
namespace Memory {

class LinearAllocator : public Allocator {
public:
    explicit LinearAllocator(size_t totalSize);
    ~LinearAllocator() override;

    // Disallow copying and moving
    LinearAllocator(const LinearAllocator&) = delete;
    LinearAllocator& operator=(const LinearAllocator&) = delete;
    LinearAllocator(LinearAllocator&&) = delete;
    LinearAllocator& operator=(LinearAllocator&&) = delete;

    [[nodiscard]] void* Allocate(size_t size, size_t alignment = alignof(std::max_align_t)) override;
    
    // Linear allocators do not support freeing individual blocks. This will be a no-op.
    void Free(void* ptr) override;
    
    // Returns true if initialization succeeded, false if memory allocation failed
    [[nodiscard]] bool Init() override;

    // Resets the allocator offset back to zero to allow memory reuse
    void Reset();

    // Returns the total memory size
    size_t GetTotalSize() const { return m_TotalSize; }
    
    // Returns the currently used memory size
    size_t GetUsedSize() const { return m_Offset; }
    
    // Returns true if the allocator has been successfully initialized
    bool IsInitialized() const { return m_StartPtr != nullptr; }

private:
    size_t m_TotalSize;
    size_t m_Offset;
    void* m_StartPtr;
};

} // namespace Memory
} // namespace Core
