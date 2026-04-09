#include "PoolAllocator.h"
#include "Core/Profile.h"
#include "Core/Log.h"
#include <cstdlib>
#include <cstdint>
#include <algorithm>
#include <limits>

namespace Core {
namespace Memory {

PoolAllocator::PoolAllocator(size_t objectSize, size_t objectAlignment, size_t capacity)
    : m_ObjectSize(objectSize), 
      m_ObjectAlignment(objectAlignment), 
      m_Capacity(capacity),
      m_StartPtr(nullptr),
      m_FreeList(nullptr) {
    
    // SECURITY: Validate capacity to prevent underflow in Init()
    if (m_Capacity == 0) {
        m_Capacity = 1;  // Minimum capacity of 1
    }
    
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

bool PoolAllocator::Init() {
    PROFILE_FUNCTION();
    if (m_StartPtr != nullptr) return true;  // Already initialized

    // SECURITY: Check for integer overflow before allocation
    if (m_Capacity == 0 || m_BlockSize > SIZE_MAX / m_Capacity) {
        ENGINE_CORE_ERROR("PoolAllocator::Init - capacity/size overflow check failed");
        m_FreeList = nullptr;
        return false;  // Overflow would occur - fail safely
    }

    size_t totalSize = m_BlockSize * m_Capacity;
    
    // Allocate continuous memory block for the pool
    m_StartPtr = std::malloc(totalSize);

    // SECURITY: Check for malloc failure
    if (m_StartPtr == nullptr) {
        ENGINE_CORE_ERROR("PoolAllocator::Init failed to allocate {} bytes", totalSize);
        m_FreeList = nullptr;
        return false;
    }

    m_FreeList = static_cast<Node*>(m_StartPtr);

    // Initialize the free list by chaining nodes
    Node* current = m_FreeList;
    for (size_t i = 0; i < m_Capacity - 1; ++i) {
        current->next = reinterpret_cast<Node*>(reinterpret_cast<char*>(current) + m_BlockSize);
        current = current->next;
    }
    current->next = nullptr;
    
    return true;
}

void* PoolAllocator::Allocate(size_t size, size_t alignment) {
    PROFILE_FUNCTION();
    
    // SECURITY: Validate that request can be satisfied
    if (size > m_ObjectSize || (alignment > 0 && alignment > m_ObjectAlignment)) {
        return nullptr;  // Cannot satisfy alignment/size requirement
    }
    
    if (m_FreeList == nullptr) {
        return nullptr; // Pool is full or uninitialized
    }

    Node* freeNode = m_FreeList;
    m_FreeList = m_FreeList->next;
    return freeNode;
}

void PoolAllocator::Free(void* ptr) {
    PROFILE_FUNCTION();
    if (ptr == nullptr || m_StartPtr == nullptr) {
        return;
    }

    // SECURITY: Bounds check - ensure ptr is within our pool
    uintptr_t ptrAddr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t startAddr = reinterpret_cast<uintptr_t>(m_StartPtr);
    uintptr_t endAddr = startAddr + (m_BlockSize * m_Capacity);
    
    if (ptrAddr < startAddr || ptrAddr >= endAddr) {
        return;  // ptr not from this pool - reject silently
    }
    
    // SECURITY: Check alignment to block boundaries
    if ((ptrAddr - startAddr) % m_BlockSize != 0) {
        return;  // Misaligned pointer - reject
    }

    // SECURITY: Detect double-free by checking if already in free list
    Node* node = static_cast<Node*>(ptr);
    Node* current = m_FreeList;
    while (current != nullptr) {
        if (current == node) {
            // Double-free detected - log and return without corrupting free list
            ENGINE_CORE_ERROR("PoolAllocator: Double-free detected for address {:p}", ptr);
            return;
        }
        current = current->next;
    }

    // Cast the returned pointer back to a Node and attach it to the front of the free list
    node->next = m_FreeList;
    m_FreeList = node;
}

} // namespace Memory
} // namespace Core
