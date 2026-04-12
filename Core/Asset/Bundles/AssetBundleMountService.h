#pragma once

#include "AssetBundleBuilder.h"

#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Core {
namespace Asset {
namespace Bundles {

    class AssetBundleMountService {
    public:
        static AssetBundleMountService& Get();

        Result<MountAssetBundleResult> MountAssetBundle(const MountAssetBundleRequest& request);
        Result<void> UnmountAssetBundle(const std::string& bundleId);

        std::optional<MountTableEntry> ResolveMountedAddress(const std::string& addressKey) const;
        std::vector<MountTableEntry> SnapshotMountTable() const;

        std::optional<AssetBundleManifest> GetMountedManifest(const std::string& bundleId) const;
        std::optional<std::filesystem::path> GetMountedManifestPath(const std::string& bundleId) const;

        Result<void> ReplaceMountedManifest(const std::string& bundleId,
                                            const AssetBundleManifest& manifest,
                                            const std::filesystem::path& manifestPath);

    private:
        AssetBundleMountService() = default;

        struct MountedBundleState {
            AssetBundleManifest Manifest;
            std::filesystem::path ManifestPath;
            std::filesystem::path PayloadPath;
            std::filesystem::path MountPoint;
            int32_t Priority = 0;
            BundleMountTier Tier = BundleMountTier::Base;
        };

        static int32_t TierRank(BundleMountTier tier);

        Result<void> RebuildMountTableLocked();

    private:
        mutable std::mutex m_Mutex;
        std::unordered_map<std::string, MountedBundleState> m_MountedBundles;
        std::unordered_map<std::string, MountTableEntry> m_MountTable;
    };

    Result<MountAssetBundleResult> MountAssetBundle(const MountAssetBundleRequest& request);

} // namespace Bundles
} // namespace Asset
} // namespace Core

