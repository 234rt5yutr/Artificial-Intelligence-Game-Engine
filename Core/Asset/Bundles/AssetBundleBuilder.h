#pragma once

#include "AssetBundleTypes.h"

namespace Core {
namespace Asset {
namespace Bundles {

    class AssetBundleBuilderService {
    public:
        Result<BuildAssetBundleResult> BuildAssetBundle(const BuildAssetBundleRequest& request) const;

        Result<AssetBundleManifest> LoadBundleManifest(const std::filesystem::path& manifestPath) const;
        Result<void> SaveBundleManifest(const AssetBundleManifest& manifest,
                                        const std::filesystem::path& manifestPath) const;
    };

    Result<BuildAssetBundleResult> BuildAssetBundle(const BuildAssetBundleRequest& request);

} // namespace Bundles
} // namespace Asset
} // namespace Core

