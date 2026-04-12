#include "AssetBundlePatcher.h"

#include "Core/Log.h"
#include "Core/Security/PathValidator.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>

namespace Core {
namespace Asset {
namespace Bundles {
namespace {

    std::string ToHexString(uint64_t value) {
        std::ostringstream stream;
        stream << std::hex << std::setw(16) << std::setfill('0') << value;
        return stream.str();
    }

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

    Result<void> RestoreBackup(const std::filesystem::path& backupPath,
                               const std::filesystem::path& targetPath) {
        std::error_code errorCode;
        if (!std::filesystem::exists(backupPath, errorCode) || errorCode) {
            return Result<void>::Failure("Backup manifest missing during rollback: " + backupPath.string());
        }

        std::filesystem::copy_file(
            backupPath,
            targetPath,
            std::filesystem::copy_options::overwrite_existing,
            errorCode);
        if (errorCode) {
            return Result<void>::Failure("Failed to restore backup manifest: " + errorCode.message());
        }

        return Result<void>::Success();
    }

} // namespace

    Result<PatchAssetBundleDeltaResult> AssetBundlePatcherService::PatchAssetBundleDelta(
        const PatchAssetBundleDeltaRequest& request) const {
        if (request.TargetBundleId.empty()) {
            return Result<PatchAssetBundleDeltaResult>::Failure("PatchAssetBundleDelta requires target bundle id");
        }
        if (request.TargetManifestPath.empty()) {
            return Result<PatchAssetBundleDeltaResult>::Failure("PatchAssetBundleDelta requires target manifest path");
        }
        if (request.DeltaManifestPath.empty()) {
            return Result<PatchAssetBundleDeltaResult>::Failure("PatchAssetBundleDelta requires delta manifest path");
        }
        if (!Security::PathValidator::QuickValidate(request.TargetManifestPath) ||
            !Security::PathValidator::QuickValidate(request.DeltaManifestPath)) {
            return Result<PatchAssetBundleDeltaResult>::Failure("PatchAssetBundleDelta path validation failed");
        }

        AssetBundleBuilderService builderService;
        auto targetManifestResult = builderService.LoadBundleManifest(request.TargetManifestPath);
        if (!targetManifestResult.Ok) {
            return Result<PatchAssetBundleDeltaResult>::Failure(targetManifestResult.Error);
        }

        const std::string previousVersion = targetManifestResult.Value.BundleVersion;
        AssetBundleManifest patchedManifest = std::move(targetManifestResult.Value);
        if (patchedManifest.BundleId != request.TargetBundleId) {
            return Result<PatchAssetBundleDeltaResult>::Failure(
                "Target manifest bundle id does not match request target bundle id");
        }

        std::ifstream deltaFile(request.DeltaManifestPath);
        if (!deltaFile) {
            return Result<PatchAssetBundleDeltaResult>::Failure(
                "Failed to open delta manifest: " + request.DeltaManifestPath.string());
        }

        nlohmann::json deltaPayload;
        try {
            deltaFile >> deltaPayload;
        } catch (const nlohmann::json::parse_error& parseError) {
            return Result<PatchAssetBundleDeltaResult>::Failure(
                "Failed to parse delta manifest JSON: " + std::string(parseError.what()));
        }

        const std::string deltaTargetBundleId = deltaPayload.value("targetBundleId", std::string{});
        if (!deltaTargetBundleId.empty() && deltaTargetBundleId != request.TargetBundleId) {
            return Result<PatchAssetBundleDeltaResult>::Failure(
                "Delta manifest target bundle id mismatch");
        }

        const std::string fromVersion = deltaPayload.value("fromVersion", std::string{});
        const std::string toVersion = deltaPayload.value("toVersion", std::string{});
        if (!fromVersion.empty() && fromVersion != patchedManifest.BundleVersion) {
            return Result<PatchAssetBundleDeltaResult>::Failure(
                "Delta manifest fromVersion does not match current bundle version");
        }
        if (!toVersion.empty()) {
            patchedManifest.BundleVersion = toVersion;
        }

        if (!deltaPayload.contains("changedEntries") || !deltaPayload["changedEntries"].is_array()) {
            return Result<PatchAssetBundleDeltaResult>::Failure("Delta manifest missing changedEntries");
        }

        std::unordered_map<std::string, size_t> entryIndexByAddress;
        for (size_t i = 0; i < patchedManifest.Entries.size(); ++i) {
            entryIndexByAddress[patchedManifest.Entries[i].AddressKey] = i;
        }

        uint32_t changedEntryCount = 0;
        for (const auto& changedEntryPayload : deltaPayload["changedEntries"]) {
            BundleEntryRecord changedEntry;
            changedEntry.AddressKey = changedEntryPayload.value("addressKey", std::string{});
            changedEntry.AssetId = changedEntryPayload.value("assetId", 0ull);
            changedEntry.AssetKind = static_cast<AssetType>(changedEntryPayload.value("assetKind", 0u));
            changedEntry.CookedPath = changedEntryPayload.value("cookedPath", std::string{});
            changedEntry.SizeBytes = changedEntryPayload.value("sizeBytes", 0ull);
            changedEntry.ChunkId = changedEntryPayload.value("chunkId", 0u);
            changedEntry.OffsetBytes = changedEntryPayload.value("offsetBytes", 0ull);

            if (changedEntry.AddressKey.empty() || changedEntry.CookedPath.empty()) {
                return Result<PatchAssetBundleDeltaResult>::Failure(
                    "Delta manifest changed entry is missing required addressKey/cookedPath");
            }

            auto existingIt = entryIndexByAddress.find(changedEntry.AddressKey);
            if (existingIt != entryIndexByAddress.end()) {
                patchedManifest.Entries[existingIt->second] = changedEntry;
            } else {
                entryIndexByAddress[changedEntry.AddressKey] = patchedManifest.Entries.size();
                patchedManifest.Entries.push_back(changedEntry);
            }
            ++changedEntryCount;
        }

        std::sort(
            patchedManifest.Entries.begin(),
            patchedManifest.Entries.end(),
            [](const BundleEntryRecord& a, const BundleEntryRecord& b) {
                if (a.AddressKey == b.AddressKey) {
                    return a.AssetId < b.AssetId;
                }
                return a.AddressKey < b.AddressKey;
            });

        patchedManifest.SourceDigest = ToHexString(ComputeManifestIntegritySeed(patchedManifest));

        const std::filesystem::path stagingDirectory =
            request.StagingDirectory.empty()
                ? (request.TargetManifestPath.parent_path() / "bundle_patch_staging")
                : request.StagingDirectory;
        std::error_code errorCode;
        std::filesystem::create_directories(stagingDirectory, errorCode);
        if (errorCode) {
            return Result<PatchAssetBundleDeltaResult>::Failure(
                "Failed to create patch staging directory: " + errorCode.message());
        }

        const std::filesystem::path stagedManifestPath =
            stagingDirectory / (request.TargetManifestPath.filename().string() + ".staged");
        auto saveResult = builderService.SaveBundleManifest(patchedManifest, stagedManifestPath);
        if (!saveResult.Ok) {
            return Result<PatchAssetBundleDeltaResult>::Failure(saveResult.Error);
        }

        const std::filesystem::path backupManifestPath =
            request.TargetManifestPath.parent_path() /
            (request.TargetManifestPath.filename().string() + ".backup");
        if (request.RequireBackup) {
            std::filesystem::copy_file(
                request.TargetManifestPath,
                backupManifestPath,
                std::filesystem::copy_options::overwrite_existing,
                errorCode);
            if (errorCode) {
                return Result<PatchAssetBundleDeltaResult>::Failure(
                    "Failed to create manifest backup: " + errorCode.message());
            }
        }

        PatchAssetBundleDeltaResult patchResult;
        patchResult.TargetBundleId = request.TargetBundleId;
        patchResult.PreviousVersion = fromVersion.empty() ? previousVersion : fromVersion;
        patchResult.UpdatedVersion = patchedManifest.BundleVersion;
        patchResult.ChangedEntryCount = changedEntryCount;

        std::filesystem::remove(request.TargetManifestPath, errorCode);
        errorCode.clear();
        std::filesystem::rename(stagedManifestPath, request.TargetManifestPath, errorCode);
        if (errorCode) {
            patchResult.RolledBack = true;
            if (request.RequireBackup) {
                auto restoreResult = RestoreBackup(backupManifestPath, request.TargetManifestPath);
                if (!restoreResult.Ok) {
                    return Result<PatchAssetBundleDeltaResult>::Failure(restoreResult.Error);
                }
            }
            return Result<PatchAssetBundleDeltaResult>::Failure(
                "Failed to apply patched manifest atomically: " + errorCode.message());
        }

        auto mountedManifest = AssetBundleMountService::Get().GetMountedManifest(request.TargetBundleId);
        if (mountedManifest.has_value()) {
            auto replaceResult = AssetBundleMountService::Get().ReplaceMountedManifest(
                request.TargetBundleId,
                patchedManifest,
                request.TargetManifestPath);
            if (!replaceResult.Ok) {
                patchResult.RolledBack = true;
                if (request.RequireBackup) {
                    auto restoreResult = RestoreBackup(backupManifestPath, request.TargetManifestPath);
                    if (!restoreResult.Ok) {
                        return Result<PatchAssetBundleDeltaResult>::Failure(restoreResult.Error);
                    }
                }
                return Result<PatchAssetBundleDeltaResult>::Failure(
                    "Patched manifest applied but mount table refresh failed: " + replaceResult.Error);
            }
        }

        patchResult.Success = true;
        ENGINE_CORE_INFO("Patched bundle '{}' from version '{}' to '{}' ({} changed entries)",
                         patchResult.TargetBundleId,
                         patchResult.PreviousVersion,
                         patchResult.UpdatedVersion,
                         patchResult.ChangedEntryCount);
        return Result<PatchAssetBundleDeltaResult>::Success(std::move(patchResult));
    }

    Result<PatchAssetBundleDeltaResult> PatchAssetBundleDelta(
        const PatchAssetBundleDeltaRequest& request) {
        AssetBundlePatcherService service;
        return service.PatchAssetBundleDelta(request);
    }

} // namespace Bundles
} // namespace Asset
} // namespace Core

