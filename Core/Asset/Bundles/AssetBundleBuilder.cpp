#include "AssetBundleBuilder.h"

#include "Core/Log.h"

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

    std::string ToCompressionModeString(BundleCompressionMode mode) {
        switch (mode) {
            case BundleCompressionMode::None: return "none";
            case BundleCompressionMode::Fast: return "fast";
            case BundleCompressionMode::Balanced: return "balanced";
            case BundleCompressionMode::Max: return "max";
            default: return "balanced";
        }
    }

    BundleCompressionMode ParseCompressionMode(const std::string& mode) {
        if (mode == "none") {
            return BundleCompressionMode::None;
        }
        if (mode == "fast") {
            return BundleCompressionMode::Fast;
        }
        if (mode == "max") {
            return BundleCompressionMode::Max;
        }
        return BundleCompressionMode::Balanced;
    }

    uint64_t ComputeDeterministicHash(const std::vector<BundleEntryRecord>& entries) {
        uint64_t hash = 14695981039346656037ull;
        for (const auto& entry : entries) {
            for (char c : entry.AddressKey) {
                hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
                hash *= 1099511628211ull;
            }
            for (char c : entry.CookedPath) {
                hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
                hash *= 1099511628211ull;
            }
            hash ^= entry.AssetId;
            hash *= 1099511628211ull;
            hash ^= static_cast<uint64_t>(entry.AssetKind);
            hash *= 1099511628211ull;
            hash ^= entry.SizeBytes;
            hash *= 1099511628211ull;
        }
        return hash;
    }

    nlohmann::json SerializeManifest(const AssetBundleManifest& manifest) {
        nlohmann::json payload = nlohmann::json::object();
        payload["schemaVersion"] = manifest.SchemaVersion;
        payload["bundleId"] = manifest.BundleId;
        payload["bundleVersion"] = manifest.BundleVersion;
        payload["platform"] = manifest.Platform;
        payload["compressionMode"] = ToCompressionModeString(manifest.CompressionMode);
        payload["generatedAtEpochSeconds"] = manifest.GeneratedAtEpochSeconds;
        payload["sourceDigest"] = manifest.SourceDigest;

        payload["chunks"] = nlohmann::json::array();
        for (const auto& chunk : manifest.Chunks) {
            payload["chunks"].push_back({
                {"chunkId", chunk.ChunkId},
                {"offsetBytes", chunk.OffsetBytes},
                {"sizeBytes", chunk.SizeBytes},
                {"integrityHash", chunk.IntegrityHash}
            });
        }

        payload["entries"] = nlohmann::json::array();
        for (const auto& entry : manifest.Entries) {
            payload["entries"].push_back({
                {"addressKey", entry.AddressKey},
                {"assetId", entry.AssetId},
                {"assetKind", static_cast<uint32_t>(entry.AssetKind)},
                {"cookedPath", entry.CookedPath},
                {"sizeBytes", entry.SizeBytes},
                {"chunkId", entry.ChunkId},
                {"offsetBytes", entry.OffsetBytes}
            });
        }
        return payload;
    }

} // namespace

    Result<BuildAssetBundleResult> AssetBundleBuilderService::BuildAssetBundle(
        const BuildAssetBundleRequest& request) const {
        if (request.BundleName.empty()) {
            return Result<BuildAssetBundleResult>::Failure("BuildAssetBundle requires a bundle name");
        }
        if (request.OutputBundlePath.empty() || request.OutputManifestPath.empty()) {
            return Result<BuildAssetBundleResult>::Failure("BuildAssetBundle requires output payload and manifest paths");
        }
        if (request.Entries.empty()) {
            return Result<BuildAssetBundleResult>::Failure("BuildAssetBundle requires at least one entry");
        }

        std::vector<BundleEntryRecord> entries = request.Entries;
        std::sort(
            entries.begin(),
            entries.end(),
            [](const BundleEntryRecord& a, const BundleEntryRecord& b) {
                if (a.AddressKey == b.AddressKey) {
                    return a.AssetId < b.AssetId;
                }
                return a.AddressKey < b.AddressKey;
            });

        const uint32_t chunkSize = std::max<uint32_t>(request.ChunkSizeBytes, 64u * 1024u);
        uint32_t currentChunkId = 0;
        uint64_t currentChunkSize = 0;
        uint64_t currentOffset = 0;
        std::unordered_map<uint32_t, std::vector<size_t>> chunkToEntryIndices;

        for (size_t i = 0; i < entries.size(); ++i) {
            auto& entry = entries[i];
            if (entry.SizeBytes == 0) {
                std::error_code ec;
                entry.SizeBytes = std::filesystem::file_size(entry.CookedPath, ec);
                if (ec) {
                    entry.SizeBytes = 1;
                }
            }

            if (currentChunkSize > 0 && currentChunkSize + entry.SizeBytes > chunkSize) {
                ++currentChunkId;
                currentChunkSize = 0;
            }

            entry.ChunkId = currentChunkId;
            entry.OffsetBytes = currentOffset;
            currentChunkSize += entry.SizeBytes;
            currentOffset += entry.SizeBytes;
            chunkToEntryIndices[entry.ChunkId].push_back(i);
        }

        AssetBundleManifest manifest;
        manifest.SchemaVersion = ASSET_BUNDLE_SCHEMA_VERSION;
        manifest.BundleVersion = request.BundleVersion;
        manifest.Platform = request.Platform;
        manifest.CompressionMode = request.CompressionMode;
        manifest.GeneratedAtEpochSeconds = 0;
        manifest.Entries = entries;

        const uint64_t sourceHash = ComputeDeterministicHash(entries);
        manifest.SourceDigest = ToHexString(sourceHash);
        manifest.BundleId = request.BundleName + "-" + ToHexString(sourceHash);

        for (uint32_t chunkId = 0; chunkId <= currentChunkId; ++chunkId) {
            BundleChunkRecord chunk;
            chunk.ChunkId = chunkId;
            chunk.OffsetBytes = 0;
            chunk.SizeBytes = 0;

            auto chunkIt = chunkToEntryIndices.find(chunkId);
            if (chunkIt != chunkToEntryIndices.end() && !chunkIt->second.empty()) {
                const auto& entryIndices = chunkIt->second;
                chunk.OffsetBytes = entries[entryIndices.front()].OffsetBytes;
                uint64_t hashSeed = 14695981039346656037ull;
                for (size_t entryIndex : entryIndices) {
                    const auto& entry = entries[entryIndex];
                    chunk.SizeBytes += entry.SizeBytes;
                    hashSeed ^= entry.AssetId;
                    hashSeed *= 1099511628211ull;
                    for (char c : entry.AddressKey) {
                        hashSeed ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
                        hashSeed *= 1099511628211ull;
                    }
                }
                chunk.IntegrityHash = ToHexString(hashSeed);
            }
            manifest.Chunks.push_back(std::move(chunk));
        }

        auto saveManifestResult = SaveBundleManifest(manifest, request.OutputManifestPath);
        if (!saveManifestResult.Ok) {
            return Result<BuildAssetBundleResult>::Failure(saveManifestResult.Error);
        }

        nlohmann::json payload;
        payload["bundleId"] = manifest.BundleId;
        payload["bundleVersion"] = manifest.BundleVersion;
        payload["platform"] = manifest.Platform;
        payload["compressionMode"] = ToCompressionModeString(manifest.CompressionMode);
        payload["sourceDigest"] = manifest.SourceDigest;
        payload["entryCount"] = manifest.Entries.size();
        payload["chunkCount"] = manifest.Chunks.size();
        payload["entries"] = nlohmann::json::array();
        for (const auto& entry : manifest.Entries) {
            payload["entries"].push_back({
                {"addressKey", entry.AddressKey},
                {"assetId", entry.AssetId},
                {"assetKind", static_cast<uint32_t>(entry.AssetKind)},
                {"cookedPath", entry.CookedPath},
                {"sizeBytes", entry.SizeBytes},
                {"chunkId", entry.ChunkId},
                {"offsetBytes", entry.OffsetBytes}
            });
        }

        std::error_code errorCode;
        const std::filesystem::path bundleParent = request.OutputBundlePath.parent_path();
        if (!bundleParent.empty()) {
            std::filesystem::create_directories(bundleParent, errorCode);
        }
        if (errorCode) {
            return Result<BuildAssetBundleResult>::Failure(
                "Failed to create bundle payload directory: " + bundleParent.string());
        }

        std::ofstream payloadFile(request.OutputBundlePath, std::ios::trunc);
        if (!payloadFile) {
            return Result<BuildAssetBundleResult>::Failure(
                "Failed to open bundle payload path: " + request.OutputBundlePath.string());
        }
        payloadFile << payload.dump(2);
        payloadFile << '\n';
        if (!payloadFile) {
            return Result<BuildAssetBundleResult>::Failure(
                "Failed to write bundle payload: " + request.OutputBundlePath.string());
        }

        BuildAssetBundleResult result;
        result.BundleId = manifest.BundleId;
        result.BundlePath = request.OutputBundlePath;
        result.ManifestPath = request.OutputManifestPath;
        result.EntryCount = static_cast<uint32_t>(manifest.Entries.size());
        result.ChunkCount = static_cast<uint32_t>(manifest.Chunks.size());
        result.Digest = manifest.SourceDigest;

        ENGINE_CORE_INFO("Built asset bundle '{}' with {} entries and {} chunks",
                         result.BundleId,
                         result.EntryCount,
                         result.ChunkCount);
        return Result<BuildAssetBundleResult>::Success(std::move(result));
    }

    Result<AssetBundleManifest> AssetBundleBuilderService::LoadBundleManifest(
        const std::filesystem::path& manifestPath) const {
        std::ifstream file(manifestPath);
        if (!file) {
            return Result<AssetBundleManifest>::Failure(
                "Failed to open bundle manifest: " + manifestPath.string());
        }

        nlohmann::json payload;
        try {
            file >> payload;
        } catch (const nlohmann::json::parse_error& parseError) {
            return Result<AssetBundleManifest>::Failure(
                "Failed to parse bundle manifest JSON: " + std::string(parseError.what()));
        }

        AssetBundleManifest manifest;
        manifest.SchemaVersion = payload.value("schemaVersion", 0u);
        manifest.BundleId = payload.value("bundleId", std::string{});
        manifest.BundleVersion = payload.value("bundleVersion", std::string{});
        manifest.Platform = payload.value("platform", std::string{"windows"});
        manifest.CompressionMode = ParseCompressionMode(payload.value("compressionMode", std::string{"balanced"}));
        manifest.GeneratedAtEpochSeconds = payload.value("generatedAtEpochSeconds", 0ull);
        manifest.SourceDigest = payload.value("sourceDigest", std::string{});

        if (manifest.SchemaVersion != ASSET_BUNDLE_SCHEMA_VERSION) {
            return Result<AssetBundleManifest>::Failure(
                "Unsupported bundle schema version: " + std::to_string(manifest.SchemaVersion));
        }

        if (manifest.BundleId.empty()) {
            return Result<AssetBundleManifest>::Failure("Bundle manifest missing bundleId");
        }

        if (payload.contains("chunks") && payload["chunks"].is_array()) {
            for (const auto& chunkPayload : payload["chunks"]) {
                BundleChunkRecord chunk;
                chunk.ChunkId = chunkPayload.value("chunkId", 0u);
                chunk.OffsetBytes = chunkPayload.value("offsetBytes", 0ull);
                chunk.SizeBytes = chunkPayload.value("sizeBytes", 0ull);
                chunk.IntegrityHash = chunkPayload.value("integrityHash", std::string{});
                manifest.Chunks.push_back(std::move(chunk));
            }
        }

        if (!payload.contains("entries") || !payload["entries"].is_array()) {
            return Result<AssetBundleManifest>::Failure("Bundle manifest missing entries array");
        }

        for (const auto& entryPayload : payload["entries"]) {
            BundleEntryRecord entry;
            entry.AddressKey = entryPayload.value("addressKey", std::string{});
            entry.AssetId = entryPayload.value("assetId", 0ull);
            entry.AssetKind = static_cast<AssetType>(entryPayload.value("assetKind", 0u));
            entry.CookedPath = entryPayload.value("cookedPath", std::string{});
            entry.SizeBytes = entryPayload.value("sizeBytes", 0ull);
            entry.ChunkId = entryPayload.value("chunkId", 0u);
            entry.OffsetBytes = entryPayload.value("offsetBytes", 0ull);

            if (entry.AddressKey.empty()) {
                return Result<AssetBundleManifest>::Failure("Bundle manifest contains entry with empty address key");
            }

            manifest.Entries.push_back(std::move(entry));
        }

        return Result<AssetBundleManifest>::Success(std::move(manifest));
    }

    Result<void> AssetBundleBuilderService::SaveBundleManifest(
        const AssetBundleManifest& manifest,
        const std::filesystem::path& manifestPath) const {
        std::error_code errorCode;
        const std::filesystem::path manifestParent = manifestPath.parent_path();
        if (!manifestParent.empty()) {
            std::filesystem::create_directories(manifestParent, errorCode);
        }
        if (errorCode) {
            return Result<void>::Failure(
                "Failed to create bundle manifest directory: " + manifestParent.string());
        }

        std::ofstream file(manifestPath, std::ios::trunc);
        if (!file) {
            return Result<void>::Failure(
                "Failed to open bundle manifest for writing: " + manifestPath.string());
        }

        const nlohmann::json payload = SerializeManifest(manifest);
        file << payload.dump(2);
        file << '\n';
        if (!file) {
            return Result<void>::Failure("Failed to write bundle manifest: " + manifestPath.string());
        }
        return Result<void>::Success();
    }

    Result<BuildAssetBundleResult> BuildAssetBundle(const BuildAssetBundleRequest& request) {
        AssetBundleBuilderService service;
        return service.BuildAssetBundle(request);
    }

} // namespace Bundles
} // namespace Asset
} // namespace Core

