#pragma once

#include "Core/Asset/Addressables/AddressablesCatalogTypes.h"
#include "Core/Math/Math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Core::Renderer {

template <typename T>
using Result = Core::Asset::Addressables::Result<T>;

enum class VirtualShadowCacheEventType : uint8_t {
    CacheHit = 0,
    CacheMiss = 1,
    CacheInsert = 2,
    CacheEvict = 3,
    FallbackRequired = 4
};

struct VirtualShadowCacheEvent {
    VirtualShadowCacheEventType type = VirtualShadowCacheEventType::CacheHit;
    uint64_t cacheKey = 0;
    uint64_t frameIndex = 0;
    std::string reason;
};

struct VirtualShadowCacheEntryRequest {
    uint64_t cacheKey = 0;
    uint32_t estimatedBytes = 0;
    uint8_t qualityTier = 0;
    bool forceRefresh = false;
};

struct VirtualShadowCacheBuildRequest {
    std::vector<VirtualShadowCacheEntryRequest> entries;
    uint64_t memoryBudgetBytes = 64ULL * 1024ULL * 1024ULL;
    uint64_t frameIndex = 0;
    bool allowEviction = true;
};

struct VirtualShadowCacheBuildResult {
    std::vector<uint64_t> cacheHits;
    std::vector<uint64_t> cacheMisses;
    std::vector<uint64_t> inserted;
    std::vector<uint64_t> evicted;
    std::vector<uint64_t> fallbackKeys;
    std::vector<VirtualShadowCacheEvent> events;
    uint64_t residentBytes = 0;
    bool fallbackRequired = false;
};

Result<VirtualShadowCacheBuildResult> BuildVirtualShadowMapCache(const VirtualShadowCacheBuildRequest& request);

} // namespace Core::Renderer
