#pragma once

#include "AddressableRuntimeTypes.h"
#include "AddressablesCatalog.h"

#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Core {
namespace Asset {
namespace Addressables {

    class AddressableRuntimeService {
    public:
        static AddressableRuntimeService& Get();

        Result<void> ActivateCatalog(const AddressablesCatalogData& catalog);
        Result<void> ActivateCatalogFromFile(const std::filesystem::path& catalogPath);
        void Clear();

        AddressableLoadTicket LoadAddressableAssetAsync(const LoadAddressableAssetRequest& request);
        Result<ReleaseAddressableAssetResult> ReleaseAddressableAsset(
            const ReleaseAddressableAssetRequest& request);
        void CancelPendingLoad(const std::string& cancellationToken);

        bool HasActiveCatalog() const;
        std::optional<AddressableCatalogEntry> FindCatalogEntry(const std::string& addressKey) const;
        AddressablesCatalogData GetActiveCatalogSnapshot() const;
        AddressableRuntimeDiagnostics GetDiagnostics() const;

    private:
        AddressableRuntimeService() = default;

        struct CachedAssetState {
            AddressableLoadResult LoadedAsset;
            uint32_t ReferenceCount = 0;
            std::vector<std::string> DependencyKeys;
        };

        AddressableLoadResult LoadAddressableAssetInternal(
            const std::string& addressKey,
            AssetType expectedType,
            bool includeTransitiveDependencies,
            const std::string& cancellationToken,
            std::unordered_set<std::string>& recursionStack);

        AddressableLoadResult LoadEntryPayload(
            const AddressableCatalogEntry& entry,
            AssetType expectedType);

        Result<ReleaseAddressableAssetResult> ReleaseAddressableAssetInternalLocked(
            const std::string& addressKey,
            AddressableReleasePolicy policy,
            std::unordered_set<std::string>& releasedKeys);

        std::shared_future<AddressableLoadResult> MakeReadyFuture(
            AddressableLoadResult result) const;

        bool IsCancellationRequestedLocked(const std::string& cancellationToken) const;

    private:
        mutable std::mutex m_Mutex;

        AddressablesCatalogData m_ActiveCatalog;
        std::unordered_map<std::string, size_t> m_AddressToCatalogIndex;
        std::unordered_map<std::string, CachedAssetState> m_CachedAssets;
        std::unordered_map<std::string, std::shared_future<AddressableLoadResult>> m_InFlightLoads;
        std::unordered_map<std::string, bool> m_CancelledTokens;
        uint64_t m_NextTicketId = 1;
        AddressableRuntimeDiagnostics m_Diagnostics;
    };

    AddressableLoadTicket LoadAddressableAssetAsync(const LoadAddressableAssetRequest& request);

    Result<ReleaseAddressableAssetResult> ReleaseAddressableAsset(
        const ReleaseAddressableAssetRequest& request);

} // namespace Addressables
} // namespace Asset
} // namespace Core

