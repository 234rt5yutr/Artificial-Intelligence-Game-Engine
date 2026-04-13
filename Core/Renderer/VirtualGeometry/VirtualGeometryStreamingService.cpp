#include "Core/Renderer/VirtualGeometry/VirtualGeometryStreamingService.h"

#include "Core/Renderer/Mesh.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Core::Renderer {
namespace {

struct PagePriority {
    uint32_t PageId = 0;
    float Score = 0.0f;
};

struct ResidencyState {
    std::unordered_set<uint32_t> ResidentPages;
    std::unordered_map<uint32_t, uint64_t> LastTouchFrame;
    std::unordered_set<uint32_t> FailedPages;
    uint64_t ResidentBytes = 0;
};

class VirtualGeometryStreamingServiceImpl {
public:
    Result<VirtualGeometryStreamResult> Stream(const VirtualGeometryStreamRequest& request) {
        if (request.Metadata == nullptr) {
            return Result<VirtualGeometryStreamResult>::Failure("VIRTUAL_GEOMETRY_STREAM_INVALID_METADATA");
        }

        const VirtualGeometryMetadata& metadata = *request.Metadata;
        VirtualGeometryStreamResult result{};
        result.BudgetSaturated = false;

        std::lock_guard<std::mutex> lock(m_Mutex);
        ResidencyState& state = m_StateByMetadata[metadata.MetadataKey];

        const std::unordered_set<uint32_t> forcedFailures(request.ForceFailPageIds.begin(), request.ForceFailPageIds.end());
        std::vector<PagePriority> candidates = GatherCandidates(metadata, request, state);
        std::sort(
            candidates.begin(),
            candidates.end(),
            [](const PagePriority& lhs, const PagePriority& rhs) {
                if (lhs.Score != rhs.Score) {
                    return lhs.Score > rhs.Score;
                }
                return lhs.PageId < rhs.PageId;
            });

        uint64_t streamedBytesThisFrame = 0;
        uint32_t streamInCount = 0;

        for (const PagePriority& candidate : candidates) {
            if (state.ResidentPages.find(candidate.PageId) != state.ResidentPages.end()) {
                state.LastTouchFrame[candidate.PageId] = request.FrameIndex;
                result.PromotedPages.push_back(candidate.PageId);
                EmitEvent(result, VirtualGeometryResidencyEventType::Promoted, candidate.PageId, request.FrameIndex, candidate.Score, "resident-visible");
                continue;
            }

            result.RequestedPages.push_back(candidate.PageId);
            EmitEvent(result, VirtualGeometryResidencyEventType::Requested, candidate.PageId, request.FrameIndex, candidate.Score, "visibility-request");

            if (streamInCount >= request.MaxPagesToStreamIn) {
                result.FallbackPages.push_back(candidate.PageId);
                continue;
            }

            const uint64_t pageBytes = EstimatePageSizeBytes(metadata, candidate.PageId);
            if (streamedBytesThisFrame + pageBytes > request.StreamInBudgetBytes) {
                result.BudgetSaturated = true;
                result.FallbackPages.push_back(candidate.PageId);
                continue;
            }

            if (forcedFailures.find(candidate.PageId) != forcedFailures.end()) {
                state.FailedPages.insert(candidate.PageId);
                result.FailedPages.push_back(candidate.PageId);
                result.FallbackPages.push_back(candidate.PageId);
                EmitEvent(result, VirtualGeometryResidencyEventType::Failed, candidate.PageId, request.FrameIndex, candidate.Score, "forced-failure");
                continue;
            }

            state.ResidentPages.insert(candidate.PageId);
            state.FailedPages.erase(candidate.PageId);
            state.LastTouchFrame[candidate.PageId] = request.FrameIndex;
            state.ResidentBytes += pageBytes;
            streamedBytesThisFrame += pageBytes;
            ++streamInCount;

            result.LoadedPages.push_back(candidate.PageId);
            EmitEvent(result, VirtualGeometryResidencyEventType::Loaded, candidate.PageId, request.FrameIndex, candidate.Score, "streamed-in");
        }

        EvictPages(metadata, request, candidates, state, result);

        result.ResidentBytes = state.ResidentBytes;
        result.BudgetSaturation = request.MemoryBudgetBytes == 0ULL
            ? 0.0f
            : static_cast<float>(static_cast<double>(state.ResidentBytes) / static_cast<double>(request.MemoryBudgetBytes));
        result.QueuePressure = request.MaxPagesToStreamIn == 0U
            ? static_cast<float>(result.RequestedPages.size())
            : static_cast<float>(result.RequestedPages.size()) / static_cast<float>(request.MaxPagesToStreamIn);
        result.RequiresFallbackRendering = !result.FallbackPages.empty() || !result.FailedPages.empty();

        result.ResidentPages.assign(state.ResidentPages.begin(), state.ResidentPages.end());
        std::sort(result.ResidentPages.begin(), result.ResidentPages.end());
        std::sort(result.RequestedPages.begin(), result.RequestedPages.end());
        std::sort(result.LoadedPages.begin(), result.LoadedPages.end());
        std::sort(result.PromotedPages.begin(), result.PromotedPages.end());
        std::sort(result.EvictedPages.begin(), result.EvictedPages.end());
        std::sort(result.FailedPages.begin(), result.FailedPages.end());
        std::sort(result.FallbackPages.begin(), result.FallbackPages.end());

        UpdateDiagnosticsLocked(request, state, result);

        if (request.SourceMesh != nullptr) {
            request.SourceMesh->SetVirtualGeometryPagesResident(!result.RequiresFallbackRendering);
            request.SourceMesh->SetVirtualGeometryFallbackActive(result.RequiresFallbackRendering);
        }

        return Result<VirtualGeometryStreamResult>::Success(std::move(result));
    }

    VirtualGeometryStreamingDiagnostics GetDiagnostics() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Diagnostics;
    }

    void Reset() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_StateByMetadata.clear();
        m_Diagnostics = VirtualGeometryStreamingDiagnostics{};
    }

private:
    static float ComputePageScore(
        const VirtualGeometryPageRecord& page,
        const Math::Vec3& cameraPosition,
        const Math::Vec3& cameraVelocity) {
        const Math::Vec3 toPage = page.BoundsCenter - cameraPosition;
        const float distanceSquared = glm::dot(toPage, toPage);
        const float visibilityScore = 1.0f / (1.0f + distanceSquared);
        const float speed = glm::length(cameraVelocity);
        float velocityBias = 0.0f;
        if (speed > 0.0f && distanceSquared > std::numeric_limits<float>::epsilon()) {
            const Math::Vec3 direction = glm::normalize(toPage);
            const Math::Vec3 velocityDirection = glm::normalize(cameraVelocity);
            velocityBias = 0.25f * glm::max(glm::dot(direction, velocityDirection), 0.0f);
        }
        return visibilityScore + velocityBias;
    }

    static uint64_t EstimatePageSizeBytes(const VirtualGeometryMetadata& metadata, const uint32_t pageId) {
        if (pageId >= metadata.Pages.size()) {
            return 0ULL;
        }
        return metadata.Pages[pageId].EstimatedSizeBytes;
    }

    static std::vector<PagePriority> GatherCandidates(
        const VirtualGeometryMetadata& metadata,
        const VirtualGeometryStreamRequest& request,
        const ResidencyState& state) {
        std::unordered_map<uint32_t, float> bestScoreByPage;

        for (const uint32_t clusterId : request.VisibleClusterIds) {
            if (clusterId >= metadata.Clusters.size()) {
                continue;
            }
            const VirtualGeometryClusterRecord& cluster = metadata.Clusters[clusterId];
            if (cluster.PageId >= metadata.Pages.size()) {
                continue;
            }
            const VirtualGeometryPageRecord& page = metadata.Pages[cluster.PageId];
            float score = ComputePageScore(page, request.CameraPosition, request.CameraVelocity);
            if (state.FailedPages.find(cluster.PageId) != state.FailedPages.end()) {
                score *= 0.5f;
            }

            const auto existing = bestScoreByPage.find(cluster.PageId);
            if (existing == bestScoreByPage.end() || score > existing->second) {
                bestScoreByPage[cluster.PageId] = score;
            }
        }

        std::vector<PagePriority> candidates;
        candidates.reserve(bestScoreByPage.size());
        for (const auto& pair : bestScoreByPage) {
            candidates.push_back(PagePriority{pair.first, pair.second});
        }
        return candidates;
    }

    static void EmitEvent(
        VirtualGeometryStreamResult& result,
        const VirtualGeometryResidencyEventType type,
        const uint32_t pageId,
        const uint64_t frameIndex,
        const float score,
        const std::string& reason) {
        result.Events.push_back(VirtualGeometryResidencyEvent{
            type,
            pageId,
            frameIndex,
            score,
            reason
        });
    }

    static void EvictPages(
        const VirtualGeometryMetadata& metadata,
        const VirtualGeometryStreamRequest& request,
        const std::vector<PagePriority>& candidates,
        ResidencyState& state,
        VirtualGeometryStreamResult& result) {
        if (state.ResidentPages.empty()) {
            return;
        }

        std::unordered_set<uint32_t> visibleCandidatePages;
        visibleCandidatePages.reserve(candidates.size());
        for (const PagePriority& candidate : candidates) {
            visibleCandidatePages.insert(candidate.PageId);
        }

        struct EvictionCandidate {
            uint32_t PageId = 0;
            uint64_t LastTouchFrame = 0;
        };
        std::vector<EvictionCandidate> evictionCandidates;
        evictionCandidates.reserve(state.ResidentPages.size());

        for (const uint32_t residentPage : state.ResidentPages) {
            if (visibleCandidatePages.find(residentPage) != visibleCandidatePages.end()) {
                continue;
            }
            uint64_t lastTouch = 0;
            const auto touchIt = state.LastTouchFrame.find(residentPage);
            if (touchIt != state.LastTouchFrame.end()) {
                lastTouch = touchIt->second;
            }
            evictionCandidates.push_back(EvictionCandidate{residentPage, lastTouch});
        }

        std::sort(
            evictionCandidates.begin(),
            evictionCandidates.end(),
            [](const EvictionCandidate& lhs, const EvictionCandidate& rhs) {
                if (lhs.LastTouchFrame != rhs.LastTouchFrame) {
                    return lhs.LastTouchFrame < rhs.LastTouchFrame;
                }
                return lhs.PageId < rhs.PageId;
            });

        uint32_t evictedCount = 0;
        for (const EvictionCandidate& candidate : evictionCandidates) {
            if (evictedCount >= request.MaxPagesToEvict) {
                break;
            }
            if (state.ResidentBytes <= request.MemoryBudgetBytes) {
                break;
            }

            const uint64_t pageBytes = EstimatePageSizeBytes(metadata, candidate.PageId);
            state.ResidentPages.erase(candidate.PageId);
            state.LastTouchFrame.erase(candidate.PageId);
            state.ResidentBytes = pageBytes > state.ResidentBytes ? 0ULL : state.ResidentBytes - pageBytes;
            ++evictedCount;

            result.EvictedPages.push_back(candidate.PageId);
            EmitEvent(
                result,
                VirtualGeometryResidencyEventType::Evicted,
                candidate.PageId,
                request.FrameIndex,
                0.0f,
                "lru-memory-pressure");
        }
    }

    void UpdateDiagnosticsLocked(
        const VirtualGeometryStreamRequest& request,
        const ResidencyState& state,
        const VirtualGeometryStreamResult& result) {
        m_Diagnostics.PendingRequestCount = static_cast<uint32_t>(result.RequestedPages.size() > result.LoadedPages.size()
            ? result.RequestedPages.size() - result.LoadedPages.size()
            : 0U);
        m_Diagnostics.ResidentPageCount = static_cast<uint32_t>(state.ResidentPages.size());
        m_Diagnostics.ResidentBytes = state.ResidentBytes;
        m_Diagnostics.MemoryBudgetBytes = request.MemoryBudgetBytes;
        m_Diagnostics.QueuePressure = result.QueuePressure;
        m_Diagnostics.BudgetSaturation = result.BudgetSaturation;
        m_Diagnostics.BudgetSaturated = result.BudgetSaturated || result.BudgetSaturation >= 1.0f;
        m_Diagnostics.LastRequestedCount = static_cast<uint32_t>(result.RequestedPages.size());
        m_Diagnostics.LastLoadedCount = static_cast<uint32_t>(result.LoadedPages.size());
        m_Diagnostics.LastEvictedCount = static_cast<uint32_t>(result.EvictedPages.size());
        m_Diagnostics.LastFailedCount = static_cast<uint32_t>(result.FailedPages.size());
    }

    mutable std::mutex m_Mutex;
    std::unordered_map<uint64_t, ResidencyState> m_StateByMetadata;
    VirtualGeometryStreamingDiagnostics m_Diagnostics;
};

VirtualGeometryStreamingServiceImpl& GetImpl() {
    static VirtualGeometryStreamingServiceImpl impl;
    return impl;
}

} // namespace

Result<VirtualGeometryStreamResult> VirtualGeometryStreamingService::Stream(const VirtualGeometryStreamRequest& request) {
    return GetImpl().Stream(request);
}

VirtualGeometryStreamingDiagnostics VirtualGeometryStreamingService::GetDiagnostics() const {
    return GetImpl().GetDiagnostics();
}

void VirtualGeometryStreamingService::Reset() {
    GetImpl().Reset();
}

VirtualGeometryStreamingService& GetVirtualGeometryStreamingService() {
    static VirtualGeometryStreamingService service;
    return service;
}

Result<VirtualGeometryStreamResult> StreamVirtualGeometryPages(const VirtualGeometryStreamRequest& request) {
    return GetVirtualGeometryStreamingService().Stream(request);
}

VirtualGeometryStreamingDiagnostics GetVirtualGeometryStreamingDiagnostics() {
    return GetVirtualGeometryStreamingService().GetDiagnostics();
}

} // namespace Core::Renderer

