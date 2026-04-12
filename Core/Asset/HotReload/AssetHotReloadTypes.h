#pragma once

#include "AssetDependencyTracker.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Core {
namespace ECS {
    class Scene;
}

namespace Asset {
namespace HotReload {

    enum class HotReloadTrigger : uint8_t {
        Manual = 0,
        FileChange,
        PatchApply
    };

    struct HotReloadAssetRequest {
        std::vector<std::string> AddressKeys;
        ECS::Scene* TargetScene = nullptr;
        HotReloadTrigger Trigger = HotReloadTrigger::Manual;
        bool DryRun = false;
        bool StrictMode = true;
        bool PreserveAudioPlaybackPosition = true;
    };

    struct HotReloadAssetResult {
        bool Success = false;
        bool RolledBack = false;
        uint64_t EventId = 0;
        uint64_t AppliedFrameIndex = 0;

        uint32_t PreloadedAssetCount = 0;
        uint32_t ReboundMeshComponentCount = 0;
        uint32_t ReboundSkeletalComponentCount = 0;
        uint32_t ReboundAudioSourceCount = 0;

        std::vector<std::string> ImpactedAddressKeys;
        std::vector<std::string> FailedAddressKeys;
        std::string Diagnostics;
    };

    struct HotReloadEventRecord {
        uint64_t EventId = 0;
        uint64_t AppliedFrameIndex = 0;
        HotReloadTrigger Trigger = HotReloadTrigger::Manual;
        bool Success = false;
        bool RolledBack = false;
        std::vector<std::string> ImpactedAddressKeys;
        std::vector<std::string> FailedAddressKeys;
        std::string Diagnostics;
    };

} // namespace HotReload
} // namespace Asset
} // namespace Core

