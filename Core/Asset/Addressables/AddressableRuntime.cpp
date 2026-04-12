#include "AddressableRuntime.h"

#include "Core/Asset/Bundles/AssetBundleMountService.h"
#include "Core/Log.h"

#include <future>
#include <utility>

namespace Core {
namespace Asset {
namespace Addressables {
namespace {

    AddressableLoadResult MakeLoadFailure(AddressableLoadStatus status,
                                          const std::string& addressKey,
                                          std::string error) {
        AddressableLoadResult result;
        result.Status = status;
        result.AddressKey = addressKey;
        result.Error = std::move(error);
        return result;
    }

    std::string BuildInFlightKey(const std::string& addressKey, AssetType expectedType) {
        return addressKey + "#" + std::to_string(static_cast<uint32_t>(expectedType));
    }

} // namespace

    AddressableRuntimeService& AddressableRuntimeService::Get() {
        static AddressableRuntimeService instance;
        return instance;
    }

    Result<void> AddressableRuntimeService::ActivateCatalog(const AddressablesCatalogData& catalog) {
        std::unordered_map<std::string, size_t> rebuiltIndex;
        rebuiltIndex.reserve(catalog.Entries.size());

        for (size_t i = 0; i < catalog.Entries.size(); ++i) {
            const auto& entry = catalog.Entries[i];
            if (entry.AddressKey.empty() || entry.CookedPath.empty()) {
                return Result<void>::Failure("Catalog activation failed due to invalid entry");
            }
            if (rebuiltIndex.find(entry.AddressKey) != rebuiltIndex.end()) {
                return Result<void>::Failure("Catalog activation failed due to duplicate address key: " + entry.AddressKey);
            }
            rebuiltIndex[entry.AddressKey] = i;
        }

        std::lock_guard lock(m_Mutex);
        m_ActiveCatalog = catalog;
        m_AddressToCatalogIndex = std::move(rebuiltIndex);
        m_CachedAssets.clear();
        m_InFlightLoads.clear();
        m_CancelledTokens.clear();
        return Result<void>::Success();
    }

    Result<void> AddressableRuntimeService::ActivateCatalogFromFile(
        const std::filesystem::path& catalogPath) {
        AddressablesCatalogService catalogService;
        auto loadResult = catalogService.LoadCatalog(catalogPath);
        if (!loadResult.Ok) {
            return Result<void>::Failure(loadResult.Error);
        }
        return ActivateCatalog(loadResult.Value);
    }

    void AddressableRuntimeService::Clear() {
        std::lock_guard lock(m_Mutex);
        m_ActiveCatalog = AddressablesCatalogData{};
        m_AddressToCatalogIndex.clear();
        m_CachedAssets.clear();
        m_InFlightLoads.clear();
        m_CancelledTokens.clear();
        m_Diagnostics = AddressableRuntimeDiagnostics{};
    }

    AddressableLoadTicket AddressableRuntimeService::LoadAddressableAssetAsync(
        const LoadAddressableAssetRequest& request) {
        AddressableLoadTicket ticket;
        ticket.AddressKey = request.AddressKey;

        if (request.AddressKey.empty()) {
            ticket.Future = MakeReadyFuture(MakeLoadFailure(
                AddressableLoadStatus::KeyNotFound,
                request.AddressKey,
                "Address key cannot be empty"));
            return ticket;
        }

        std::shared_future<AddressableLoadResult> sharedFuture;
        std::string inFlightKey = BuildInFlightKey(request.AddressKey, request.ExpectedType);

        {
            std::lock_guard lock(m_Mutex);
            ++m_Diagnostics.TotalLoadRequests;
            ticket.TicketId = m_NextTicketId++;

            if (m_AddressToCatalogIndex.empty()) {
                ticket.Future = MakeReadyFuture(MakeLoadFailure(
                    AddressableLoadStatus::KeyNotFound,
                    request.AddressKey,
                    "No active catalog"));
                return ticket;
            }

            auto cachedIt = m_CachedAssets.find(request.AddressKey);
            if (cachedIt != m_CachedAssets.end() && cachedIt->second.LoadedAsset.IsValid()) {
                ++cachedIt->second.ReferenceCount;
                ++m_Diagnostics.CacheHits;
                AddressableLoadResult cachedResult = cachedIt->second.LoadedAsset;
                cachedResult.IsFromCache = true;
                ticket.Future = MakeReadyFuture(std::move(cachedResult));
                return ticket;
            }

            auto inFlightIt = m_InFlightLoads.find(inFlightKey);
            if (inFlightIt != m_InFlightLoads.end()) {
                ticket.SharedInFlightTicket = true;
                ++m_Diagnostics.SharedInFlightTickets;
                ticket.Future = inFlightIt->second;
                return ticket;
            }

            sharedFuture = std::async(
                std::launch::async,
                [this, request, inFlightKey]() {
                    std::unordered_set<std::string> recursionStack;
                    AddressableLoadResult result = LoadAddressableAssetInternal(
                        request.AddressKey,
                        request.ExpectedType,
                        request.IncludeTransitiveDependencies,
                        request.CancellationToken,
                        recursionStack);

                    std::lock_guard lock(m_Mutex);
                    m_InFlightLoads.erase(inFlightKey);
                    if (!result.IsValid()) {
                        ++m_Diagnostics.FailedLoadRequests;
                    }
                    return result;
                }).share();

            m_InFlightLoads[inFlightKey] = sharedFuture;
        }

        ticket.Future = std::move(sharedFuture);
        return ticket;
    }

    Result<ReleaseAddressableAssetResult> AddressableRuntimeService::ReleaseAddressableAsset(
        const ReleaseAddressableAssetRequest& request) {
        if (request.AddressKey.empty()) {
            return Result<ReleaseAddressableAssetResult>::Failure("ReleaseAddressableAsset requires a non-empty key");
        }

        std::lock_guard lock(m_Mutex);
        std::unordered_set<std::string> releasedKeys;
        return ReleaseAddressableAssetInternalLocked(request.AddressKey, request.Policy, releasedKeys);
    }

    void AddressableRuntimeService::CancelPendingLoad(const std::string& cancellationToken) {
        if (cancellationToken.empty()) {
            return;
        }

        std::lock_guard lock(m_Mutex);
        m_CancelledTokens[cancellationToken] = true;
    }

    bool AddressableRuntimeService::HasActiveCatalog() const {
        std::lock_guard lock(m_Mutex);
        return !m_AddressToCatalogIndex.empty();
    }

    std::optional<AddressableCatalogEntry> AddressableRuntimeService::FindCatalogEntry(
        const std::string& addressKey) const {
        std::lock_guard lock(m_Mutex);

        auto it = m_AddressToCatalogIndex.find(addressKey);
        if (it == m_AddressToCatalogIndex.end()) {
            return std::nullopt;
        }

        return m_ActiveCatalog.Entries[it->second];
    }

    AddressablesCatalogData AddressableRuntimeService::GetActiveCatalogSnapshot() const {
        std::lock_guard lock(m_Mutex);
        return m_ActiveCatalog;
    }

    AddressableRuntimeDiagnostics AddressableRuntimeService::GetDiagnostics() const {
        std::lock_guard lock(m_Mutex);
        return m_Diagnostics;
    }

    AddressableLoadResult AddressableRuntimeService::LoadAddressableAssetInternal(
        const std::string& addressKey,
        AssetType expectedType,
        bool includeTransitiveDependencies,
        const std::string& cancellationToken,
        std::unordered_set<std::string>& recursionStack) {
        AddressableCatalogEntry entry;
        std::vector<std::string> acquiredDependencyKeys;

        {
            std::lock_guard lock(m_Mutex);

            if (IsCancellationRequestedLocked(cancellationToken)) {
                return MakeLoadFailure(
                    AddressableLoadStatus::Cancelled,
                    addressKey,
                    "Addressable load cancelled");
            }

            auto catalogIt = m_AddressToCatalogIndex.find(addressKey);
            if (catalogIt == m_AddressToCatalogIndex.end()) {
                return MakeLoadFailure(
                    AddressableLoadStatus::KeyNotFound,
                    addressKey,
                    "Address key not found in active catalog");
            }

            auto cachedIt = m_CachedAssets.find(addressKey);
            if (cachedIt != m_CachedAssets.end() && cachedIt->second.LoadedAsset.IsValid()) {
                ++cachedIt->second.ReferenceCount;
                AddressableLoadResult cachedResult = cachedIt->second.LoadedAsset;
                cachedResult.IsFromCache = true;
                ++m_Diagnostics.CacheHits;
                return cachedResult;
            }

            entry = m_ActiveCatalog.Entries[catalogIt->second];
        }

        if (recursionStack.find(addressKey) != recursionStack.end()) {
            return MakeLoadFailure(
                AddressableLoadStatus::DependencyCycleDetected,
                addressKey,
                "Dependency cycle detected while loading addressable key");
        }
        recursionStack.insert(addressKey);

        const auto eraseFromStack = [&recursionStack, &addressKey]() {
            recursionStack.erase(addressKey);
        };

        if (includeTransitiveDependencies) {
            for (const auto& dependency : entry.Dependencies) {
                AddressableLoadResult dependencyResult = LoadAddressableAssetInternal(
                    dependency.AddressKey,
                    AssetType::Unknown,
                    true,
                    cancellationToken,
                    recursionStack);

                if (!dependencyResult.IsValid()) {
                    if (!acquiredDependencyKeys.empty()) {
                        std::lock_guard lock(m_Mutex);
                        std::unordered_set<std::string> releasedKeys;
                        for (const auto& acquiredKey : acquiredDependencyKeys) {
                            ReleaseAddressableAssetInternalLocked(
                                acquiredKey,
                                AddressableReleasePolicy::Immediate,
                                releasedKeys);
                        }
                    }
                    eraseFromStack();
                    return MakeLoadFailure(
                        AddressableLoadStatus::DependencyMissing,
                        addressKey,
                        "Failed to load dependency '" + dependency.AddressKey + "': " + dependencyResult.Error);
                }
                acquiredDependencyKeys.push_back(dependency.AddressKey);
            }
        }

        AddressableLoadResult loadedResult = LoadEntryPayload(entry, expectedType);
        if (!loadedResult.IsValid()) {
            if (!acquiredDependencyKeys.empty()) {
                std::lock_guard lock(m_Mutex);
                std::unordered_set<std::string> releasedKeys;
                for (const auto& acquiredKey : acquiredDependencyKeys) {
                    ReleaseAddressableAssetInternalLocked(
                        acquiredKey,
                        AddressableReleasePolicy::Immediate,
                        releasedKeys);
                }
            }
            eraseFromStack();
            return loadedResult;
        }

        {
            std::lock_guard lock(m_Mutex);
            if (IsCancellationRequestedLocked(cancellationToken)) {
                if (!acquiredDependencyKeys.empty()) {
                    std::unordered_set<std::string> releasedKeys;
                    for (const auto& acquiredKey : acquiredDependencyKeys) {
                        ReleaseAddressableAssetInternalLocked(
                            acquiredKey,
                            AddressableReleasePolicy::Immediate,
                            releasedKeys);
                    }
                }
                eraseFromStack();
                return MakeLoadFailure(
                    AddressableLoadStatus::Cancelled,
                    addressKey,
                    "Addressable load cancelled before activation");
            }

            auto& cached = m_CachedAssets[addressKey];
            cached.LoadedAsset = loadedResult;
            cached.ReferenceCount += 1;
            cached.DependencyKeys.clear();
            cached.DependencyKeys.reserve(entry.Dependencies.size());
            for (const auto& dependency : entry.Dependencies) {
                cached.DependencyKeys.push_back(dependency.AddressKey);
            }
        }

        eraseFromStack();
        return loadedResult;
    }

    AddressableLoadResult AddressableRuntimeService::LoadEntryPayload(
        const AddressableCatalogEntry& entry,
        AssetType expectedType) {
        if (expectedType != AssetType::Unknown && expectedType != entry.AssetKind) {
            return MakeLoadFailure(
                AddressableLoadStatus::TypeMismatch,
                entry.AddressKey,
                "Expected type '" + std::string(GetAssetTypeName(expectedType)) +
                    "' but catalog entry is '" + std::string(GetAssetTypeName(entry.AssetKind)) + "'");
        }

        AddressableLoadResult result;
        result.Status = AddressableLoadStatus::Success;
        result.AddressKey = entry.AddressKey;
        result.LoadedType = entry.AssetKind;

        std::filesystem::path cookedPath(entry.CookedPath);
        auto mountedOverride = Bundles::AssetBundleMountService::Get().ResolveMountedAddress(entry.AddressKey);
        if (mountedOverride.has_value()) {
            cookedPath = mountedOverride->ResolvedCookedPath;
        }
        result.ResolvedCookedPath = cookedPath;
        switch (entry.AssetKind) {
            case AssetType::Texture:
                result.Texture = AssetLoader::LoadTexture(cookedPath);
                if (!result.Texture.IsValid) {
                    return MakeLoadFailure(AddressableLoadStatus::LoadFailed, entry.AddressKey, "Failed to load texture");
                }
                return result;
            case AssetType::Mesh:
                result.Mesh = AssetLoader::LoadMesh(cookedPath);
                if (!result.Mesh.IsValid) {
                    return MakeLoadFailure(AddressableLoadStatus::LoadFailed, entry.AddressKey, "Failed to load mesh");
                }
                return result;
            case AssetType::Shader:
                result.Shader = AssetLoader::LoadShader(cookedPath);
                if (!result.Shader.IsValid) {
                    return MakeLoadFailure(AddressableLoadStatus::LoadFailed, entry.AddressKey, "Failed to load shader");
                }
                return result;
            case AssetType::Prefab:
                result.StructuredAsset = AssetLoader::LoadPrefab(cookedPath);
                break;
            case AssetType::VisualScriptGraph:
                result.StructuredAsset = AssetLoader::LoadVisualScriptGraph(cookedPath);
                break;
            case AssetType::Timeline:
                result.StructuredAsset = AssetLoader::LoadTimeline(cookedPath);
                break;
            case AssetType::Scene:
                result.StructuredAsset = AssetLoader::LoadSceneAsset(cookedPath);
                break;
            case AssetType::WorldPartitionCell:
                result.StructuredAsset = AssetLoader::LoadWorldPartitionCellAsset(cookedPath);
                break;
            case AssetType::HierarchicalLOD:
                result.StructuredAsset = AssetLoader::LoadHierarchicalLODAsset(cookedPath);
                break;
            default:
                return MakeLoadFailure(
                    AddressableLoadStatus::TypeMismatch,
                    entry.AddressKey,
                    "Addressable runtime does not support this asset type yet");
        }

        if (!result.StructuredAsset.IsValid) {
            return MakeLoadFailure(
                AddressableLoadStatus::LoadFailed,
                entry.AddressKey,
                "Failed to load structured asset payload");
        }
        return result;
    }

    Result<ReleaseAddressableAssetResult> AddressableRuntimeService::ReleaseAddressableAssetInternalLocked(
        const std::string& addressKey,
        AddressableReleasePolicy policy,
        std::unordered_set<std::string>& releasedKeys) {
        if (releasedKeys.find(addressKey) != releasedKeys.end()) {
            ReleaseAddressableAssetResult repeatRelease;
            repeatRelease.AddressKey = addressKey;
            return Result<ReleaseAddressableAssetResult>::Success(std::move(repeatRelease));
        }
        releasedKeys.insert(addressKey);

        auto cacheIt = m_CachedAssets.find(addressKey);
        if (cacheIt == m_CachedAssets.end()) {
            return Result<ReleaseAddressableAssetResult>::Failure(
                "ReleaseAddressableAsset failed because key is not loaded: " + addressKey);
        }

        if (cacheIt->second.ReferenceCount == 0) {
            return Result<ReleaseAddressableAssetResult>::Failure(
                "ReleaseAddressableAsset refcount underflow for key: " + addressKey);
        }

        cacheIt->second.ReferenceCount -= 1;

        ReleaseAddressableAssetResult releaseResult;
        releaseResult.AddressKey = addressKey;
        releaseResult.RemainingReferenceCount = cacheIt->second.ReferenceCount;

        const bool shouldEvict = (cacheIt->second.ReferenceCount == 0) &&
            (policy == AddressableReleasePolicy::Immediate || policy == AddressableReleasePolicy::MemoryPressure);
        if (!shouldEvict) {
            return Result<ReleaseAddressableAssetResult>::Success(std::move(releaseResult));
        }

        const std::vector<std::string> dependencyKeys = cacheIt->second.DependencyKeys;
        m_CachedAssets.erase(cacheIt);
        releaseResult.Evicted = true;

        for (const std::string& dependencyKey : dependencyKeys) {
            auto cascadeResult = ReleaseAddressableAssetInternalLocked(
                dependencyKey,
                AddressableReleasePolicy::Immediate,
                releasedKeys);
            if (!cascadeResult.Ok) {
                ENGINE_CORE_WARN("Addressable dependency release warning for '{}': {}",
                                 dependencyKey,
                                 cascadeResult.Error);
            }
        }

        return Result<ReleaseAddressableAssetResult>::Success(std::move(releaseResult));
    }

    std::shared_future<AddressableLoadResult> AddressableRuntimeService::MakeReadyFuture(
        AddressableLoadResult result) const {
        std::promise<AddressableLoadResult> promise;
        promise.set_value(std::move(result));
        return promise.get_future().share();
    }

    bool AddressableRuntimeService::IsCancellationRequestedLocked(
        const std::string& cancellationToken) const {
        if (cancellationToken.empty()) {
            return false;
        }
        auto tokenIt = m_CancelledTokens.find(cancellationToken);
        return tokenIt != m_CancelledTokens.end() && tokenIt->second;
    }

    AddressableLoadTicket LoadAddressableAssetAsync(const LoadAddressableAssetRequest& request) {
        return AddressableRuntimeService::Get().LoadAddressableAssetAsync(request);
    }

    Result<ReleaseAddressableAssetResult> ReleaseAddressableAsset(
        const ReleaseAddressableAssetRequest& request) {
        return AddressableRuntimeService::Get().ReleaseAddressableAsset(request);
    }

} // namespace Addressables
} // namespace Asset
} // namespace Core

