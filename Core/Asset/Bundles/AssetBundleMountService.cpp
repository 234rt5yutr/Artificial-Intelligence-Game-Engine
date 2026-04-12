#include "AssetBundleMountService.h"

#include "Core/Log.h"
#include "Core/Security/PathValidator.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace Core {
namespace Asset {
namespace Bundles {
namespace {

    uint64_t ComputeManifestIntegritySeed(const AssetBundleManifest& manifest) {
        uint64_t hash = 14695981039346656037ull;
        auto accumulateString = [&hash](const std::string& value) {
            for (char c : value) {
                hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
                hash *= 1099511628211ull;
            }
        };

        accumulateString(manifest.BundleId);
        accumulateString(manifest.BundleVersion);
        accumulateString(manifest.Platform);
        for (const auto& entry : manifest.Entries) {
            accumulateString(entry.AddressKey);
            accumulateString(entry.CookedPath);
            hash ^= entry.AssetId;
            hash *= 1099511628211ull;
            hash ^= entry.SizeBytes;
            hash *= 1099511628211ull;
        }
        return hash;
    }

    std::string ToHexString(uint64_t value) {
        std::ostringstream stream;
        stream << std::hex << std::setw(16) << std::setfill('0') << value;
        return stream.str();
    }

} // namespace

    AssetBundleMountService& AssetBundleMountService::Get() {
        static AssetBundleMountService instance;
        return instance;
    }

    Result<MountAssetBundleResult> AssetBundleMountService::MountAssetBundle(
        const MountAssetBundleRequest& request) {
        if (request.BundleManifestPath.empty()) {
            return Result<MountAssetBundleResult>::Failure("MountAssetBundle requires a bundle manifest path");
        }

        if (!Security::PathValidator::QuickValidate(request.BundleManifestPath)) {
            return Result<MountAssetBundleResult>::Failure("Bundle manifest path validation failed");
        }
        if (!request.BundlePayloadPath.empty() &&
            !Security::PathValidator::QuickValidate(request.BundlePayloadPath)) {
            return Result<MountAssetBundleResult>::Failure("Bundle payload path validation failed");
        }

        AssetBundleBuilderService builderService;
        auto loadResult = builderService.LoadBundleManifest(request.BundleManifestPath);
        if (!loadResult.Ok) {
            return Result<MountAssetBundleResult>::Failure(loadResult.Error);
        }

        AssetBundleManifest manifest = std::move(loadResult.Value);
        if (manifest.BundleId.empty()) {
            return Result<MountAssetBundleResult>::Failure("Bundle manifest missing bundleId");
        }

        if (request.VerifyIntegrity) {
            const std::string computedDigest = ToHexString(ComputeManifestIntegritySeed(manifest));
            if (!manifest.SourceDigest.empty() && manifest.SourceDigest != computedDigest) {
                return Result<MountAssetBundleResult>::Failure(
                    "Bundle integrity check failed for " + manifest.BundleId);
            }
        }

        {
            std::lock_guard lock(m_Mutex);

            if (request.ConflictPolicy == MountConflictPolicy::Fail ||
                request.ConflictPolicy == MountConflictPolicy::KeepExisting) {
                std::vector<BundleEntryRecord> filteredEntries;
                filteredEntries.reserve(manifest.Entries.size());

                for (const auto& entry : manifest.Entries) {
                    auto existingIt = m_MountTable.find(entry.AddressKey);
                    const bool hasConflict = existingIt != m_MountTable.end() &&
                        existingIt->second.BundleId != manifest.BundleId;

                    if (hasConflict && request.ConflictPolicy == MountConflictPolicy::Fail) {
                        return Result<MountAssetBundleResult>::Failure(
                            "Mount conflict for address key '" + entry.AddressKey + "'");
                    }
                    if (hasConflict && request.ConflictPolicy == MountConflictPolicy::KeepExisting) {
                        continue;
                    }

                    filteredEntries.push_back(entry);
                }

                manifest.Entries = std::move(filteredEntries);
            }

            MountedBundleState state;
            state.Manifest = manifest;
            state.ManifestPath = request.BundleManifestPath;
            state.PayloadPath = request.BundlePayloadPath;
            state.MountPoint = request.MountPoint;
            state.Priority = request.MountPriority;
            state.Tier = request.MountTier;

            m_MountedBundles[manifest.BundleId] = std::move(state);

            auto rebuildResult = RebuildMountTableLocked();
            if (!rebuildResult.Ok) {
                m_MountedBundles.erase(manifest.BundleId);
                return Result<MountAssetBundleResult>::Failure(rebuildResult.Error);
            }
        }

        MountAssetBundleResult result;
        result.BundleId = manifest.BundleId;
        result.BundleVersion = manifest.BundleVersion;
        result.MountedEntryCount = static_cast<uint32_t>(manifest.Entries.size());
        result.AppliedPriority = request.MountPriority;
        ENGINE_CORE_INFO("Mounted asset bundle '{}' with {} entries",
                         result.BundleId,
                         result.MountedEntryCount);
        return Result<MountAssetBundleResult>::Success(std::move(result));
    }

    Result<void> AssetBundleMountService::UnmountAssetBundle(const std::string& bundleId) {
        if (bundleId.empty()) {
            return Result<void>::Failure("UnmountAssetBundle requires a non-empty bundleId");
        }

        std::lock_guard lock(m_Mutex);
        auto it = m_MountedBundles.find(bundleId);
        if (it == m_MountedBundles.end()) {
            return Result<void>::Failure("Bundle is not mounted: " + bundleId);
        }

        m_MountedBundles.erase(it);
        auto rebuildResult = RebuildMountTableLocked();
        if (!rebuildResult.Ok) {
            return rebuildResult;
        }
        return Result<void>::Success();
    }

    std::optional<MountTableEntry> AssetBundleMountService::ResolveMountedAddress(
        const std::string& addressKey) const {
        std::lock_guard lock(m_Mutex);
        auto it = m_MountTable.find(addressKey);
        if (it == m_MountTable.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::vector<MountTableEntry> AssetBundleMountService::SnapshotMountTable() const {
        std::lock_guard lock(m_Mutex);
        std::vector<MountTableEntry> snapshot;
        snapshot.reserve(m_MountTable.size());
        for (const auto& [_, entry] : m_MountTable) {
            snapshot.push_back(entry);
        }
        std::sort(
            snapshot.begin(),
            snapshot.end(),
            [](const MountTableEntry& a, const MountTableEntry& b) {
                if (a.AddressKey == b.AddressKey) {
                    return a.BundleId < b.BundleId;
                }
                return a.AddressKey < b.AddressKey;
            });
        return snapshot;
    }

    std::optional<AssetBundleManifest> AssetBundleMountService::GetMountedManifest(
        const std::string& bundleId) const {
        std::lock_guard lock(m_Mutex);
        auto it = m_MountedBundles.find(bundleId);
        if (it == m_MountedBundles.end()) {
            return std::nullopt;
        }
        return it->second.Manifest;
    }

    std::optional<std::filesystem::path> AssetBundleMountService::GetMountedManifestPath(
        const std::string& bundleId) const {
        std::lock_guard lock(m_Mutex);
        auto it = m_MountedBundles.find(bundleId);
        if (it == m_MountedBundles.end()) {
            return std::nullopt;
        }
        return it->second.ManifestPath;
    }

    Result<void> AssetBundleMountService::ReplaceMountedManifest(
        const std::string& bundleId,
        const AssetBundleManifest& manifest,
        const std::filesystem::path& manifestPath) {
        std::lock_guard lock(m_Mutex);
        auto it = m_MountedBundles.find(bundleId);
        if (it == m_MountedBundles.end()) {
            return Result<void>::Failure("Cannot replace manifest for non-mounted bundle: " + bundleId);
        }

        it->second.Manifest = manifest;
        if (!manifestPath.empty()) {
            it->second.ManifestPath = manifestPath;
        }

        return RebuildMountTableLocked();
    }

    int32_t AssetBundleMountService::TierRank(BundleMountTier tier) {
        switch (tier) {
            case BundleMountTier::Base: return 0;
            case BundleMountTier::Dlc: return 1;
            case BundleMountTier::Mod: return 2;
            case BundleMountTier::Hotfix: return 3;
            default: return 0;
        }
    }

    Result<void> AssetBundleMountService::RebuildMountTableLocked() {
        m_MountTable.clear();

        std::vector<std::pair<std::string, const MountedBundleState*>> orderedBundles;
        orderedBundles.reserve(m_MountedBundles.size());
        for (const auto& [bundleId, state] : m_MountedBundles) {
            orderedBundles.emplace_back(bundleId, &state);
        }

        std::sort(
            orderedBundles.begin(),
            orderedBundles.end(),
            [](const auto& lhs, const auto& rhs) {
                if (lhs.second->Priority != rhs.second->Priority) {
                    return lhs.second->Priority > rhs.second->Priority;
                }
                const int32_t lhsTier = TierRank(lhs.second->Tier);
                const int32_t rhsTier = TierRank(rhs.second->Tier);
                if (lhsTier != rhsTier) {
                    return lhsTier > rhsTier;
                }
                return lhs.first < rhs.first;
            });

        for (const auto& [bundleId, state] : orderedBundles) {
            for (const auto& entry : state->Manifest.Entries) {
                if (m_MountTable.find(entry.AddressKey) != m_MountTable.end()) {
                    continue;
                }

                std::filesystem::path resolvedPath(entry.CookedPath);
                if (!resolvedPath.is_absolute() && !state->MountPoint.empty()) {
                    resolvedPath = state->MountPoint / resolvedPath;
                }

                MountTableEntry mountTableEntry;
                mountTableEntry.AddressKey = entry.AddressKey;
                mountTableEntry.BundleId = bundleId;
                mountTableEntry.BundleVersion = state->Manifest.BundleVersion;
                mountTableEntry.ResolvedCookedPath = resolvedPath.string();
                mountTableEntry.MountPriority = state->Priority;
                mountTableEntry.Tier = state->Tier;
                m_MountTable[entry.AddressKey] = std::move(mountTableEntry);
            }
        }

        return Result<void>::Success();
    }

    Result<MountAssetBundleResult> MountAssetBundle(const MountAssetBundleRequest& request) {
        return AssetBundleMountService::Get().MountAssetBundle(request);
    }

} // namespace Bundles
} // namespace Asset
} // namespace Core

