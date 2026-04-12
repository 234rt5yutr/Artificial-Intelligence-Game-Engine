#pragma once

#include "AssetHotReloadTypes.h"

#include <deque>
#include <mutex>
#include <unordered_set>

namespace Core {
namespace Asset {
namespace HotReload {

    class AssetHotReloadService {
    public:
        static AssetHotReloadService& Get();

        Result<HotReloadAssetResult> HotReloadAssetAtRuntime(const HotReloadAssetRequest& request);
        std::vector<HotReloadEventRecord> GetRecentEvents() const;
        void ClearEvents();

        void PumpFrameSafePoint();
        uint64_t GetCurrentFrameIndex() const;

    private:
        AssetHotReloadService() = default;

        static std::string NormalizePathKey(const std::string& rawPath);
        static bool IsPathMatch(const std::string& componentPath,
                                const std::unordered_set<std::string>& normalizedImpactedPaths);

        void PushEventLocked(HotReloadEventRecord eventRecord);

    private:
        mutable std::mutex m_Mutex;
        std::deque<HotReloadEventRecord> m_EventHistory;
        uint64_t m_NextEventId = 1;
        uint64_t m_FrameIndex = 0;
        size_t m_MaxEventHistory = 128;
    };

    Result<HotReloadAssetResult> HotReloadAssetAtRuntime(const HotReloadAssetRequest& request);

} // namespace HotReload
} // namespace Asset
} // namespace Core

