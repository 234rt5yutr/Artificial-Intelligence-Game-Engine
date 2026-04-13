#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Core::Renderer {

struct TransientResourceRequest {
    std::string Resource;
    uint32_t FirstPassIndex = 0;
    uint32_t LastPassIndex = 0;
    uint64_t EstimatedSizeBytes = 0;
    bool Imported = false;
};

struct TransientResourceAliasAssignment {
    std::string Resource;
    uint32_t AliasGroup = 0;
};

struct TransientResourceAllocationReport {
    std::vector<TransientResourceAliasAssignment> Assignments;
    uint32_t AliasGroupCount = 0;
    uint64_t TotalRequestedBytes = 0;
    uint64_t TotalAllocatedBytes = 0;
    uint64_t AliasSavingsBytes = 0;
};

class TransientResourceAllocator {
public:
    TransientResourceAllocationReport Allocate(
        const std::vector<TransientResourceRequest>& requests,
        bool allowAliasing) const;
};

} // namespace Core::Renderer

