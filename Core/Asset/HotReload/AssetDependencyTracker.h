#pragma once

#include "Core/Asset/Addressables/AddressableRuntime.h"

#include <string>
#include <vector>

namespace Core {
namespace Asset {
namespace HotReload {

    template <typename T>
    using Result = Core::Asset::Addressables::Result<T>;

    struct TrackAssetDependencyGraphRequest {
        std::vector<std::string> RootAddressKeys;
        bool IncludeReverseDependents = true;
        bool IncludeTransitive = true;
        bool DetectCycles = true;
    };

    struct DependencyGraphNodeSnapshot {
        std::string AddressKey;
        std::vector<std::string> DirectDependencies;
        std::vector<std::string> TransitiveDependencies;
        std::vector<std::string> ReverseDependents;
    };

    struct DependencyGraphSnapshot {
        std::vector<DependencyGraphNodeSnapshot> Nodes;
        bool HasCycle = false;
        std::vector<std::string> CyclePath;
        std::string Digest;
    };

    class AssetDependencyTrackerService {
    public:
        static AssetDependencyTrackerService& Get();

        Result<DependencyGraphSnapshot> TrackAssetDependencyGraph(
            const TrackAssetDependencyGraphRequest& request) const;
    };

    Result<DependencyGraphSnapshot> TrackAssetDependencyGraph(
        const TrackAssetDependencyGraphRequest& request);

} // namespace HotReload
} // namespace Asset
} // namespace Core

