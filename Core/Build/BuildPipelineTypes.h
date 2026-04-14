#pragma once

#include "Core/Asset/Addressables/AddressablesCatalogTypes.h"
#include "Core/Asset/AssetCooker.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Core::Build {

template <typename T>
using Result = Core::Asset::Addressables::Result<T>;

enum class BuildStageState : uint8_t {
    Pending = 0,
    Running = 1,
    Succeeded = 2,
    Failed = 3,
    Skipped = 4
};

struct BuildStageStatus {
    std::string StageName;
    BuildStageState State = BuildStageState::Pending;
    std::string Message;
    uint64_t DurationMs = 0;
};

struct PlatformBuildRequest {
    std::string Platform;
    std::string Configuration;
    std::string BuildProfile;
    std::string CookProfile;
    bool IncludeSymbols = false;
    std::filesystem::path OutputDirectory;
};

struct PlatformBuildResult {
    std::string Platform;
    std::string Configuration;
    std::string BuildProfile;
    std::string CookProfile;
    bool IncludeSymbols = false;
    std::filesystem::path OutputDirectory;

    std::string BuildId;
    std::string GitCommitHash;
    std::string ProfileHash;
    std::string CookManifestHash;
    std::string ToolchainSignature;

    Core::Asset::CookOptions CookOptions;
    std::vector<BuildStageStatus> Stages;
    uint64_t TotalDurationMs = 0;
    bool Succeeded = false;
};

struct StoreSubmissionMetadata {
    std::string ProductId;
    std::string ProductName;
    std::string Version;
    std::string BuildId;
};

struct StoreSubmissionManifestEntry {
    std::string ArtifactId;
    std::string RelativePath;
    std::string Checksum;
};

struct StoreSubmissionRequest {
    std::string Storefront;
    std::string Channel;
    std::string SubmissionId;
    std::filesystem::path SourceArtifactsRoot;
    std::filesystem::path OutputDirectory;
    StoreSubmissionMetadata Metadata;
    std::vector<StoreSubmissionManifestEntry> ManifestEntries;
};

struct StoreSubmissionPackageEntry {
    std::string ArtifactId;
    std::string RelativePath;
    uint64_t ByteSize = 0;
    std::string Checksum;
};

struct StoreSubmissionResult {
    std::string Storefront;
    std::string Channel;
    std::string SubmissionId;
    std::filesystem::path BundleMetadataPath;
    std::filesystem::path ChecksumManifestPath;
    std::filesystem::path ManifestIndexPath;
    std::string BundleMetadataChecksum;
    std::string ChecksumManifestChecksum;
    std::string DeterministicDigest;
    std::vector<StoreSubmissionPackageEntry> PackageEntries;
};

struct DedicatedServerBuildRequest {
    std::string Platform;
    std::string Configuration;
    std::string BuildProfile;
    bool IncludeSymbols = false;
    std::filesystem::path OutputDirectory;
};

struct DedicatedServerArtifactMetadata {
    std::filesystem::path BinaryPath;
    std::filesystem::path ConfigTemplatePath;
    std::filesystem::path SymbolsPath;
    std::filesystem::path DeploymentManifestPath;
    std::string BinaryChecksum;
    std::string ConfigTemplateChecksum;
    std::string SymbolsChecksum;
    std::string DeploymentManifestChecksum;
};

struct DedicatedServerBuildResult {
    std::string Platform;
    std::string Configuration;
    std::string BuildProfile;
    bool IncludeSymbols = false;
    bool ExcludesClientPayload = true;
    bool ExcludesUiPayload = true;
    std::string SymbolHandlingState;
    std::filesystem::path OutputDirectory;
    std::filesystem::path BinaryPath;
    std::filesystem::path ConfigTemplatePath;
    std::filesystem::path SymbolsPath;
    std::filesystem::path DeploymentManifestPath;
    std::string DeterministicDigest;
    DedicatedServerArtifactMetadata Artifacts;
    bool Succeeded = false;
};

} // namespace Core::Build
