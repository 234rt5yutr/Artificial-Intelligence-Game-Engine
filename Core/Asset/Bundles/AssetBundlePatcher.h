#pragma once

#include "AssetBundleMountService.h"

namespace Core {
namespace Asset {
namespace Bundles {

    class AssetBundlePatcherService {
    public:
        Result<PatchAssetBundleDeltaResult> PatchAssetBundleDelta(
            const PatchAssetBundleDeltaRequest& request) const;
    };

    Result<PatchAssetBundleDeltaResult> PatchAssetBundleDelta(
        const PatchAssetBundleDeltaRequest& request);

} // namespace Bundles
} // namespace Asset
} // namespace Core

