#include "LinearAllocator.h"
#include "Core/Profile.h"
#include <cstdlib>
#include <memory>

namespace Core {
namespace Memory {

LinearAllocator::LinearAllocator(size_t totalSize)
    : m_TotalSize(totalSize), m_Offset(0), m_StartPtr(nullptr) {
}

LinearAllocator::~LinearAllocator() {
    PROFILE_FUNCTION();
    if (m_StartPtr) {
        std::free(m_StartPtr);
        m_StartPtr = nullptr;
    }
}

void LinearAllocator::Init() {
    PROFILE_FUNCTION();
    if (m_StartPtr == nullptr) {
        m_StartPtr = std::malloc(m_TotalSize);
    }
}

void* LinearAllocator::Allocate(size_t size, size_t alignment) {
    PROFILE_FUNCTION();
    if (m_StartPtr == nullptr) {
        return nullptr; // Allocator not initialized
    }

    std::size_t currentAddress = reinterpret_cast<std::size_t>(m_StartPtr) + m_Offset;
    std::size_t padding = 0;

    // Calculate required padding for alignment
    if (alignment != 0 && currentAddress % alignment != 0) {
        padding = alignment - (currentAddress % alignment);
    }

    if (m_Offset + padding + size > m_TotalSize) {
        return nullptr; // Out of memory
    }

    m_Offset += padding;
    std::size_t nextAddress = currentAddress + padding;
    m_Offset += size;

    return reinterpret_cast<void*>(nextAddress);
}

void LinearAllocator::Free(void* ptr) {
    // Linear allocator does not support freeing individual allocations.
    // Memory is freed all at once using Reset().
}

void LinearAllocator::Reset() {
    m_Offset = 0;
}

} // namespace Memory
} // namespace Core
