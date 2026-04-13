#include "Core/Renderer/VirtualShadows/VirtualShadowMapCache.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace Core::Renderer {
namespace {

struct CacheEntry {
    uint32_t estimatedBytes = 0;
    uint64_t lastUsedFrame = 0;
    uint8_t qualityTier = 0;
};

struct CacheState {
    std::unordered_map<uint64_t, CacheEntry> entries;
    uint64_t residentBytes = 0;
};

CacheState& GetCacheState() {
    static CacheState state{};
    return state;
}

void EmitEvent(
    VirtualShadowCacheBuildResult& result,
    const VirtualShadowCacheEventType type,
    const uint64_t cacheKey,
    const uint64_t frameIndex,
    std::string reason) {
    result.events.push_back(VirtualShadowCacheEvent{type, cacheKey, frameIndex, std::move(reason)});
}

void TryEvictEntries(CacheState& state, const VirtualShadowCacheBuildRequest& request, VirtualShadowCacheBuildResult& result) {
    if (!request.allowEviction || state.residentBytes <= request.memoryBudgetBytes) {
        return;
    }

    struct Candidate {
        uint64_t key = 0;
        uint64_t lastUsedFrame = 0;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(state.entries.size());
    for (const auto& [key, value] : state.entries) {
        candidates.push_back(Candidate{key, value.lastUsedFrame});
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const Candidate& lhs, const Candidate& rhs) {
            if (lhs.lastUsedFrame != rhs.lastUsedFrame) {
                return lhs.lastUsedFrame < rhs.lastUsedFrame;
            }
            return lhs.key < rhs.key;
        });

    for (const Candidate& candidate : candidates) {
        if (state.residentBytes <= request.memoryBudgetBytes) {
            break;
        }

        const auto it = state.entries.find(candidate.key);
        if (it == state.entries.end()) {
            continue;
        }

        const uint32_t bytes = it->second.estimatedBytes;
        state.residentBytes = bytes > state.residentBytes ? 0ULL : state.residentBytes - bytes;
        state.entries.erase(it);
        result.evicted.push_back(candidate.key);
        EmitEvent(result, VirtualShadowCacheEventType::CacheEvict, candidate.key, request.frameIndex, "memory-budget");
    }
}

} // namespace

Result<VirtualShadowCacheBuildResult> BuildVirtualShadowMapCache(const VirtualShadowCacheBuildRequest& request) {
    VirtualShadowCacheBuildResult result{};
    CacheState& cacheState = GetCacheState();

    for (const VirtualShadowCacheEntryRequest& entryRequest : request.entries) {
        if (entryRequest.cacheKey == 0ULL) {
            continue;
        }

        const auto existingIt = cacheState.entries.find(entryRequest.cacheKey);
        if (existingIt != cacheState.entries.end() && !entryRequest.forceRefresh) {
            existingIt->second.lastUsedFrame = request.frameIndex;
            result.cacheHits.push_back(entryRequest.cacheKey);
            EmitEvent(result, VirtualShadowCacheEventType::CacheHit, entryRequest.cacheKey, request.frameIndex, "resident");
            continue;
        }

        result.cacheMisses.push_back(entryRequest.cacheKey);
        EmitEvent(result, VirtualShadowCacheEventType::CacheMiss, entryRequest.cacheKey, request.frameIndex, "missing");

        const uint32_t bytes = std::max(1U, entryRequest.estimatedBytes);
        cacheState.entries[entryRequest.cacheKey] = CacheEntry{
            bytes,
            request.frameIndex,
            entryRequest.qualityTier
        };
        cacheState.residentBytes += bytes;
        result.inserted.push_back(entryRequest.cacheKey);
        EmitEvent(result, VirtualShadowCacheEventType::CacheInsert, entryRequest.cacheKey, request.frameIndex, "insert");
    }

    TryEvictEntries(cacheState, request, result);

    result.residentBytes = cacheState.residentBytes;
    if (cacheState.residentBytes > request.memoryBudgetBytes) {
        result.fallbackRequired = true;
        for (const auto& [key, _] : cacheState.entries) {
            result.fallbackKeys.push_back(key);
            EmitEvent(result, VirtualShadowCacheEventType::FallbackRequired, key, request.frameIndex, "budget-exceeded");
        }
    }

    return Result<VirtualShadowCacheBuildResult>::Success(std::move(result));
}

} // namespace Core::Renderer
