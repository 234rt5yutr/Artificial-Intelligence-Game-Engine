#include "Core/Renderer/RenderGraph/TransientResourceAllocator.h"

#include <algorithm>
#include <vector>

namespace Core::Renderer {
namespace {

struct AliasGroupState {
    uint32_t GroupId = 0;
    uint32_t LastPassIndex = 0;
    uint64_t CapacityBytes = 0;
};

} // namespace

TransientResourceAllocationReport TransientResourceAllocator::Allocate(
    const std::vector<TransientResourceRequest>& requests,
    const bool allowAliasing) const {
    std::vector<TransientResourceRequest> sortedRequests = requests;
    std::sort(
        sortedRequests.begin(),
        sortedRequests.end(),
        [](const TransientResourceRequest& lhs, const TransientResourceRequest& rhs) {
            if (lhs.FirstPassIndex != rhs.FirstPassIndex) {
                return lhs.FirstPassIndex < rhs.FirstPassIndex;
            }
            return lhs.Resource < rhs.Resource;
        });

    TransientResourceAllocationReport report{};
    std::vector<AliasGroupState> aliasGroups;
    uint32_t nextGroupId = 0;

    for (const TransientResourceRequest& request : sortedRequests) {
        report.TotalRequestedBytes += request.EstimatedSizeBytes;

        uint32_t assignedGroup = nextGroupId;
        bool reusedGroup = false;

        if (allowAliasing && !request.Imported) {
            for (AliasGroupState& group : aliasGroups) {
                if (request.FirstPassIndex > group.LastPassIndex) {
                    assignedGroup = group.GroupId;
                    group.LastPassIndex = request.LastPassIndex;
                    group.CapacityBytes = std::max(group.CapacityBytes, request.EstimatedSizeBytes);
                    reusedGroup = true;
                    break;
                }
            }
        }

        if (!reusedGroup) {
            AliasGroupState group{};
            group.GroupId = nextGroupId;
            group.LastPassIndex = request.LastPassIndex;
            group.CapacityBytes = request.EstimatedSizeBytes;
            aliasGroups.push_back(group);
            assignedGroup = nextGroupId;
            ++nextGroupId;
        }

        TransientResourceAliasAssignment assignment{};
        assignment.Resource = request.Resource;
        assignment.AliasGroup = assignedGroup;
        report.Assignments.push_back(std::move(assignment));
    }

    report.AliasGroupCount = static_cast<uint32_t>(aliasGroups.size());

    for (const AliasGroupState& group : aliasGroups) {
        report.TotalAllocatedBytes += group.CapacityBytes;
    }

    if (report.TotalRequestedBytes > report.TotalAllocatedBytes) {
        report.AliasSavingsBytes = report.TotalRequestedBytes - report.TotalAllocatedBytes;
    }

    return report;
}

} // namespace Core::Renderer

