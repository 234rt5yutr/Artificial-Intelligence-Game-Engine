#include "AssetDependencyTracker.h"

#include <algorithm>
#include <functional>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Core {
namespace Asset {
namespace HotReload {
namespace {

    std::string ToHexString(uint64_t value) {
        std::ostringstream stream;
        stream << std::hex << std::setw(16) << std::setfill('0') << value;
        return stream.str();
    }

    std::string ComputeSnapshotDigest(const DependencyGraphSnapshot& snapshot) {
        uint64_t hash = 14695981039346656037ull;
        const auto accumulate = [&hash](const std::string& value) {
            for (char c : value) {
                hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
                hash *= 1099511628211ull;
            }
        };

        accumulate(snapshot.HasCycle ? "1" : "0");
        for (const auto& node : snapshot.Nodes) {
            accumulate(node.AddressKey);
            for (const auto& dep : node.DirectDependencies) {
                accumulate(dep);
            }
            for (const auto& dep : node.TransitiveDependencies) {
                accumulate(dep);
            }
            for (const auto& dep : node.ReverseDependents) {
                accumulate(dep);
            }
        }
        for (const auto& cycleNode : snapshot.CyclePath) {
            accumulate(cycleNode);
        }
        return ToHexString(hash);
    }

} // namespace

    AssetDependencyTrackerService& AssetDependencyTrackerService::Get() {
        static AssetDependencyTrackerService instance;
        return instance;
    }

    Result<DependencyGraphSnapshot> AssetDependencyTrackerService::TrackAssetDependencyGraph(
        const TrackAssetDependencyGraphRequest& request) const {
        const auto catalog = Addressables::AddressableRuntimeService::Get().GetActiveCatalogSnapshot();
        if (catalog.Entries.empty()) {
            return Result<DependencyGraphSnapshot>::Failure("TrackAssetDependencyGraph requires an active catalog");
        }

        std::unordered_map<std::string, std::vector<std::string>> dependencyMap;
        std::unordered_map<std::string, std::vector<std::string>> reverseMap;
        dependencyMap.reserve(catalog.Entries.size());
        reverseMap.reserve(catalog.Entries.size());

        for (const auto& entry : catalog.Entries) {
            auto& deps = dependencyMap[entry.AddressKey];
            deps.reserve(entry.Dependencies.size());
            for (const auto& dependency : entry.Dependencies) {
                deps.push_back(dependency.AddressKey);
                reverseMap[dependency.AddressKey].push_back(entry.AddressKey);
            }
            if (reverseMap.find(entry.AddressKey) == reverseMap.end()) {
                reverseMap[entry.AddressKey] = {};
            }
        }

        for (auto& [_, deps] : dependencyMap) {
            std::sort(deps.begin(), deps.end());
            deps.erase(std::unique(deps.begin(), deps.end()), deps.end());
        }
        for (auto& [_, reverseDeps] : reverseMap) {
            std::sort(reverseDeps.begin(), reverseDeps.end());
            reverseDeps.erase(std::unique(reverseDeps.begin(), reverseDeps.end()), reverseDeps.end());
        }

        std::vector<std::string> rootKeys = request.RootAddressKeys;
        if (rootKeys.empty()) {
            rootKeys.reserve(dependencyMap.size());
            for (const auto& [key, _] : dependencyMap) {
                rootKeys.push_back(key);
            }
        }

        std::sort(rootKeys.begin(), rootKeys.end());
        rootKeys.erase(std::unique(rootKeys.begin(), rootKeys.end()), rootKeys.end());

        std::unordered_map<std::string, std::vector<std::string>> transitiveCache;
        std::function<std::vector<std::string>(const std::string&, std::unordered_set<std::string>&)> gatherTransitive;
        gatherTransitive = [&](const std::string& key, std::unordered_set<std::string>& path) -> std::vector<std::string> {
            auto cacheIt = transitiveCache.find(key);
            if (cacheIt != transitiveCache.end()) {
                return cacheIt->second;
            }

            std::vector<std::string> resolved;
            auto depsIt = dependencyMap.find(key);
            if (depsIt != dependencyMap.end()) {
                for (const std::string& dependencyKey : depsIt->second) {
                    if (path.find(dependencyKey) != path.end()) {
                        continue;
                    }
                    resolved.push_back(dependencyKey);
                    path.insert(dependencyKey);
                    std::vector<std::string> nested = gatherTransitive(dependencyKey, path);
                    resolved.insert(resolved.end(), nested.begin(), nested.end());
                    path.erase(dependencyKey);
                }
            }

            std::sort(resolved.begin(), resolved.end());
            resolved.erase(std::unique(resolved.begin(), resolved.end()), resolved.end());
            transitiveCache[key] = resolved;
            return resolved;
        };

        DependencyGraphSnapshot snapshot;
        std::unordered_set<std::string> includedKeys;
        for (const std::string& rootKey : rootKeys) {
            includedKeys.insert(rootKey);
            if (request.IncludeTransitive) {
                std::unordered_set<std::string> path;
                path.insert(rootKey);
                std::vector<std::string> transitive = gatherTransitive(rootKey, path);
                includedKeys.insert(transitive.begin(), transitive.end());
            }
        }

        std::vector<std::string> orderedKeys(includedKeys.begin(), includedKeys.end());
        std::sort(orderedKeys.begin(), orderedKeys.end());
        for (const std::string& key : orderedKeys) {
            DependencyGraphNodeSnapshot node;
            node.AddressKey = key;
            node.DirectDependencies = dependencyMap[key];
            if (request.IncludeTransitive) {
                std::unordered_set<std::string> path;
                path.insert(key);
                node.TransitiveDependencies = gatherTransitive(key, path);
            }
            if (request.IncludeReverseDependents) {
                node.ReverseDependents = reverseMap[key];
            }
            snapshot.Nodes.push_back(std::move(node));
        }

        if (request.DetectCycles) {
            std::unordered_set<std::string> visited;
            std::unordered_set<std::string> recursionStack;
            std::vector<std::string> traversalPath;

            std::function<bool(const std::string&)> findCycle = [&](const std::string& key) {
                if (recursionStack.find(key) != recursionStack.end()) {
                    snapshot.HasCycle = true;
                    auto cycleStart = std::find(traversalPath.begin(), traversalPath.end(), key);
                    if (cycleStart != traversalPath.end()) {
                        snapshot.CyclePath.assign(cycleStart, traversalPath.end());
                        snapshot.CyclePath.push_back(key);
                    } else {
                        snapshot.CyclePath = {key, key};
                    }
                    return true;
                }
                if (visited.find(key) != visited.end()) {
                    return false;
                }

                visited.insert(key);
                recursionStack.insert(key);
                traversalPath.push_back(key);

                for (const std::string& dependency : dependencyMap[key]) {
                    if (findCycle(dependency)) {
                        return true;
                    }
                }

                recursionStack.erase(key);
                traversalPath.pop_back();
                return false;
            };

            for (const auto& [key, _] : dependencyMap) {
                if (findCycle(key)) {
                    break;
                }
            }
        }

        snapshot.Digest = ComputeSnapshotDigest(snapshot);
        return Result<DependencyGraphSnapshot>::Success(std::move(snapshot));
    }

    Result<DependencyGraphSnapshot> TrackAssetDependencyGraph(
        const TrackAssetDependencyGraphRequest& request) {
        return AssetDependencyTrackerService::Get().TrackAssetDependencyGraph(request);
    }

} // namespace HotReload
} // namespace Asset
} // namespace Core

