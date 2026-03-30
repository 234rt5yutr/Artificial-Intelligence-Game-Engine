#include "PoolAllocator.h"
#include "Core/Profile.h"
#include <cstdlib>
#include <algorithm>

namespace Core {
namespace Memory {

PoolAllocator::PoolAllocator(size_t objectSize, size_t objectAlignment, size_t capacity)
    : m_ObjectSize(objectSize), 
      m_ObjectAlignment(objectAlignment), 
      m_Capacity(capacity),
      m_StartPtr(nullptr),
      m_FreeList(nullptr) {
    
    // Ensure the block size is at least as large as a Node pointer to store the free list
    m_BlockSize = std::max(objectSize, sizeof(Node));
    
    // Adjust block size for alignment
    if (m_ObjectAlignment != 0 && m_BlockSize % m_ObjectAlignment != 0) {
        m_BlockSize += m_ObjectAlignment - (m_BlockSize % m_ObjectAlignment);
    }
}

PoolAllocator::~PoolAllocator() {
    PROFILE_FUNCTION();
    if (m_StartPtr) {
        std::free(m_StartPtr);
        m_StartPtr = nullptr;
    }
}

void PoolAllocator::Init() {
    PROFILE_FUNCTION();
    if (m_StartPtr != nullptr) return;

    // Allocate continuous memory block for the pool
    m_StartPtr = std::malloc(m_BlockSize * m_Capacity);

    m_FreeList = static_cast<Node*>(m_StartPtr);

    // Initialize the free list by chaining nodes
    Node* current = m_FreeList;
    for (size_t i = 0; i < m_Capacity - 1; ++i) {
        current->next = reinterpret_cast<Node*>(reinterpret_cast<char*>(current) + m_BlockSize);
        current = current->next;
    }
    current->next = nullptr;
}

void* PoolAllocator::Allocate(size_t /*size*/, size_t /*alignment*/) {
    PROFILE_FUNCTION();
    if (m_FreeList == nullptr) {
        return nullptr; // Pool is full or uninitialized
    }

    Node* freeNode = m_FreeList;
    m_FreeList = m_FreeList->next;
    return freeNode;
}

void PoolAllocator::Free(void* ptr) {
    PROFILE_FUNCTION();
    if (ptr == nullptr) {
        return;
    }

    // Cast the returned pointer back to a Node and attach it to the front of the free list
    Node* node = static_cast<Node*>(ptr);
    node->next = m_FreeList;
    m_FreeList = node;
}

} // namespace Memory
} // namespace Core
