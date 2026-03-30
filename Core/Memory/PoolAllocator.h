#pragma once

#include "Allocator.h"
#include <cstddef>

namespace Core {
namespace Memory {

class PoolAllocator : public Allocator {
public:
    PoolAllocator(size_t objectSize, size_t objectAlignment, size_t capacity);
    ~PoolAllocator() override;

    // Disallow copying and moving
    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;
    PoolAllocator(PoolAllocator&&) = delete;
    PoolAllocator& operator=(PoolAllocator&&) = delete;

    void* Allocate(size_t size, size_t alignment = alignof(std::max_align_t)) override;
    void Free(void* ptr) override;
    void Init() override;

private:
    struct Node {
        Node* next;
    };

    size_t m_ObjectSize;
    size_t m_ObjectAlignment;
    size_t m_Capacity;
    size_t m_BlockSize; // Adjusted size with alignment to fit nodes

    void* m_StartPtr;
    Node* m_FreeList;
};

} // namespace Memory
} // namespace Core
