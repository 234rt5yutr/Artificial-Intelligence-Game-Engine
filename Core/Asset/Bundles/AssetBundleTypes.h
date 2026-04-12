#pragma once

#include "Core/Asset/Addressables/AddressablesCatalogTypes.h"
#include "Core/Asset/AssetTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Core {
namespace Asset {
namespace Bundles {

    template <typename T>
    using Result = Core::Asset::Addressables::Result<T>;

    enum class BundleCompressionMode : uint8_t {
        None = 0,
        Fast,
        Balanced,
        Max
    };

    enum class BundleMountTier : uint8_t {
        Base = 0,
        Dlc,
        Mod,
        Hotfix
    };

    enum class MountConflictPolicy : uint8_t {
        Fail = 0,
        OverrideByPriority,
        KeepExisting
    };

    struct BundleChunkRecord {
        uint32_t ChunkId = 0;
        uint64_t OffsetBytes = 0;
        uint64_t SizeBytes = 0;
        std::string IntegrityHash;
    };

    struct BundleEntryRecord {
        std::string AddressKey;
        uint64_t AssetId = 0;
        AssetType AssetKind = AssetType::Unknown;
        std::string CookedPath;
        uint64_t SizeBytes = 0;
        uint32_t ChunkId = 0;
        uint64_t OffsetBytes = 0;
    };

    struct AssetBundleManifest {
        uint32_t SchemaVersion = ASSET_BUNDLE_SCHEMA_VERSION;
        std::string BundleId;
        std::string BundleVersion;
        std::string Platform;
        BundleCompressionMode CompressionMode = BundleCompressionMode::Balanced;
        uint64_t GeneratedAtEpochSeconds = 0;
        std::string SourceDigest;
        std::vector<BundleChunkRecord> Chunks;
        std::vector<BundleEntryRecord> Entries;
    };

    struct BuildAssetBundleRequest {
        std::string BundleName;
        std::string BundleVersion = "1.0.0";
        std::string Platform = "windows";
        BundleCompressionMode CompressionMode = BundleCompressionMode::Balanced;
        uint32_t ChunkSizeBytes = 4u * 1024u * 1024u;
        std::filesystem::path OutputBundlePath;
        std::filesystem::path OutputManifestPath;
        std::vector<BundleEntryRecord> Entries;
    };

    struct BuildAssetBundleResult {
        std::string BundleId;
        std::filesystem::path BundlePath;
        std::filesystem::path ManifestPath;
        uint32_t EntryCount = 0;
        uint32_t ChunkCount = 0;
        std::string Digest;
    };

    struct MountAssetBundleRequest {
        std::filesystem::path BundleManifestPath;
        std::filesystem::path BundlePayloadPath;
        std::filesystem::path MountPoint;
        int32_t MountPriority = 0;
        BundleMountTier MountTier = BundleMountTier::Dlc;
        MountConflictPolicy ConflictPolicy = MountConflictPolicy::OverrideByPriority;
        bool VerifyIntegrity = true;
    };

    struct MountAssetBundleResult {
        std::string BundleId;
        std::string BundleVersion;
        uint32_t MountedEntryCount = 0;
        int32_t AppliedPriority = 0;
    };

    struct PatchAssetBundleDeltaRequest {
        std::string TargetBundleId;
        std::filesystem::path TargetManifestPath;
        std::filesystem::path DeltaManifestPath;
        std::filesystem::path DeltaPayloadPath;
        std::filesystem::path StagingDirectory;
        bool RequireBackup = true;
    };

    struct PatchAssetBundleDeltaResult {
        std::string TargetBundleId;
        std::string PreviousVersion;
        std::string UpdatedVersion;
        uint32_t ChangedEntryCount = 0;
        bool RolledBack = false;
        bool Success = false;
    };

    struct MountTableEntry {
        std::string AddressKey;
        std::string BundleId;
        std::string BundleVersion;
        std::string ResolvedCookedPath;
        int32_t MountPriority = 0;
        BundleMountTier Tier = BundleMountTier::Base;
    };

} // namespace Bundles
} // namespace Asset
} // namespace Core

