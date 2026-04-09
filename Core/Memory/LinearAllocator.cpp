#include "LinearAllocator.h"
#include "Core/Profile.h"
#include "Core/Log.h"
#include <cstdlib>
#include <cstdint>
#include <memory>

namespace Core {
namespace Memory {

LinearAllocator::LinearAllocator(size_t totalSize)
    : m_TotalSize(totalSize), m_Offset(0), m_StartPtr(nullptr) {
    // SECURITY: Ensure minimum size to prevent useless allocator
    if (m_TotalSize == 0) {
        m_TotalSize = 1;
    }
}

LinearAllocator::~LinearAllocator() {
    PROFILE_FUNCTION();
    if (m_StartPtr) {
        std::free(m_StartPtr);
        m_StartPtr = nullptr;
    }
}

bool LinearAllocator::Init() {
    PROFILE_FUNCTION();
    if (m_StartPtr == nullptr) {
        m_StartPtr = std::malloc(m_TotalSize);
        if (m_StartPtr == nullptr) {
            ENGINE_CORE_ERROR("LinearAllocator::Init failed to allocate {} bytes", m_TotalSize);
            return false;
        }
    }
    return true;
}

void* LinearAllocator::Allocate(size_t size, size_t alignment) {
    PROFILE_FUNCTION();
    
    // SECURITY: Validate inputs
    if (m_StartPtr == nullptr || size == 0) {
        return nullptr;
    }

    // SECURITY: Ensure alignment is at least 1 to prevent division issues
    if (alignment == 0) {
        alignment = 1;
    }

    std::uintptr_t currentAddress = reinterpret_cast<std::uintptr_t>(m_StartPtr) + m_Offset;
    std::size_t padding = 0;

    // Calculate required padding for alignment
    if (currentAddress % alignment != 0) {
        padding = alignment - (currentAddress % alignment);
    }

    // SECURITY: Safe overflow check - avoid integer overflow in bounds checking
    size_t remainingSpace = m_TotalSize - m_Offset;
    if (padding > remainingSpace || size > remainingSpace - padding) {
        return nullptr;  // Out of memory
    }

    m_Offset += padding;
    std::uintptr_t nextAddress = currentAddress + padding;
    m_Offset += size;

    return reinterpret_cast<void*>(nextAddress);
}

void LinearAllocator::Free(void* /*ptr*/) {
    // Linear allocator does not support freeing individual allocations.
    // Memory is freed all at once using Reset().
}

void LinearAllocator::Reset() {
    m_Offset = 0;
}

} // namespace Memory
} // namespace Core
