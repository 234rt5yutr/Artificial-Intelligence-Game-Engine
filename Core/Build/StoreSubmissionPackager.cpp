#include "Core/Build/StoreSubmissionPackager.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Core::Build {
namespace {

struct PreparedArtifact {
    std::string ArtifactId;
    std::string RelativePath;
    uint64_t ByteSize = 0;
    std::string Checksum;
};

[[nodiscard]] bool IsSupportedStorefront(const std::string& storefront) {
    static const std::array<std::string, 4> kSupportedStorefronts = {"steam", "epic", "gog", "microsoft-store"};
    return std::find(kSupportedStorefronts.begin(), kSupportedStorefronts.end(), storefront) !=
           kSupportedStorefronts.end();
}

[[nodiscard]] bool IsSupportedChannel(const std::string& channel) {
    static const std::array<std::string, 3> kSupportedChannels = {"certification", "production", "sandbox"};
    return std::find(kSupportedChannels.begin(), kSupportedChannels.end(), channel) != kSupportedChannels.end();
}

[[nodiscard]] std::string ComputeChecksum(const std::string& payload) {
    constexpr uint64_t kFnvOffset = 14695981039346656037ULL;
    constexpr uint64_t kFnvPrime = 1099511628211ULL;

    uint64_t hash = kFnvOffset;
    for (const unsigned char byte : payload) {
        hash ^= static_cast<uint64_t>(byte);
        hash *= kFnvPrime;
    }

    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << hash;
    return stream.str();
}

[[nodiscard]] Result<std::string> ComputeFileChecksum(const std::filesystem::path& filePath) {
    std::ifstream input(filePath, std::ios::binary);
    if (!input.is_open()) {
        return Result<std::string>::Failure("PACKAGE_ARTIFACT_READ_FAILED");
    }

    std::ostringstream stream;
    stream << input.rdbuf();
    if (!input.good() && !input.eof()) {
        return Result<std::string>::Failure("PACKAGE_ARTIFACT_READ_FAILED");
    }

    return Result<std::string>::Success(ComputeChecksum(stream.str()));
}

[[nodiscard]] bool WriteFile(const std::filesystem::path& filePath, const std::string& content) {
    std::ofstream output(filePath, std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }

    output << content;
    output.flush();
    return output.good();
}

[[nodiscard]] bool IsMetadataValid(const StoreSubmissionMetadata& metadata) {
    return !metadata.ProductId.empty() && !metadata.ProductName.empty() && !metadata.Version.empty() &&
           !metadata.BuildId.empty();
}

} // namespace

Result<StoreSubmissionResult> PackageStoreSubmissionArtifacts(const StoreSubmissionRequest& request) {
    if (request.Storefront.empty() || request.Channel.empty() || request.SubmissionId.empty() ||
        request.SourceArtifactsRoot.empty() || request.OutputDirectory.empty() || request.ManifestEntries.empty()) {
        return Result<StoreSubmissionResult>::Failure("PACKAGE_REQUEST_INVALID");
    }

    if (!IsSupportedStorefront(request.Storefront)) {
        return Result<StoreSubmissionResult>::Failure("PACKAGE_STOREFRONT_UNSUPPORTED");
    }

    if (!IsSupportedChannel(request.Channel)) {
        return Result<StoreSubmissionResult>::Failure("PACKAGE_CHANNEL_INVALID");
    }

    if (!IsMetadataValid(request.Metadata)) {
        return Result<StoreSubmissionResult>::Failure("PACKAGE_METADATA_INVALID");
    }

    std::error_code errorCode;
    const bool outputPathExists = std::filesystem::exists(request.OutputDirectory, errorCode);
    if (errorCode) {
        return Result<StoreSubmissionResult>::Failure("PACKAGE_WRITE_FAILED");
    }

    if (outputPathExists && !std::filesystem::is_directory(request.OutputDirectory, errorCode)) {
        return Result<StoreSubmissionResult>::Failure("PACKAGE_REQUEST_INVALID");
    }
    if (errorCode) {
        return Result<StoreSubmissionResult>::Failure("PACKAGE_WRITE_FAILED");
    }

    if (!outputPathExists) {
        std::filesystem::create_directories(request.OutputDirectory, errorCode);
        if (errorCode) {
            return Result<StoreSubmissionResult>::Failure("PACKAGE_WRITE_FAILED");
        }
    }

    std::unordered_set<std::string> artifactIds;
    std::vector<PreparedArtifact> preparedArtifacts;
    preparedArtifacts.reserve(request.ManifestEntries.size());

    for (const StoreSubmissionManifestEntry& manifestEntry : request.ManifestEntries) {
        if (manifestEntry.ArtifactId.empty() || manifestEntry.RelativePath.empty()) {
            return Result<StoreSubmissionResult>::Failure("PACKAGE_MANIFEST_INVALID");
        }

        const bool inserted = artifactIds.insert(manifestEntry.ArtifactId).second;
        if (!inserted) {
            return Result<StoreSubmissionResult>::Failure("PACKAGE_MANIFEST_INVALID");
        }

        const std::filesystem::path relativePath = std::filesystem::path(manifestEntry.RelativePath);
        if (relativePath.is_absolute()) {
            return Result<StoreSubmissionResult>::Failure("PACKAGE_MANIFEST_INVALID");
        }

        const std::filesystem::path artifactPath = request.SourceArtifactsRoot / relativePath;
        const bool artifactExists = std::filesystem::exists(artifactPath, errorCode);
        if (errorCode) {
            return Result<StoreSubmissionResult>::Failure("PACKAGE_WRITE_FAILED");
        }
        if (!artifactExists || !std::filesystem::is_regular_file(artifactPath, errorCode) || errorCode) {
            return Result<StoreSubmissionResult>::Failure("PACKAGE_ARTIFACT_MISSING");
        }

        const uint64_t artifactSize = std::filesystem::file_size(artifactPath, errorCode);
        if (errorCode) {
            return Result<StoreSubmissionResult>::Failure("PACKAGE_WRITE_FAILED");
        }

        std::string checksum = manifestEntry.Checksum;
        if (checksum.empty()) {
            const Result<std::string> checksumResult = ComputeFileChecksum(artifactPath);
            if (!checksumResult.Ok) {
                return Result<StoreSubmissionResult>::Failure(checksumResult.Error);
            }
            checksum = checksumResult.Value;
        }

        PreparedArtifact prepared{};
        prepared.ArtifactId = manifestEntry.ArtifactId;
        prepared.RelativePath = manifestEntry.RelativePath;
        prepared.ByteSize = artifactSize;
        prepared.Checksum = std::move(checksum);
        preparedArtifacts.push_back(std::move(prepared));
    }

    std::sort(preparedArtifacts.begin(), preparedArtifacts.end(), [](const PreparedArtifact& left,
                                                                     const PreparedArtifact& right) {
        if (left.ArtifactId == right.ArtifactId) {
            return left.RelativePath < right.RelativePath;
        }
        return left.ArtifactId < right.ArtifactId;
    });

    nlohmann::json bundleMetadata;
    bundleMetadata["schemaVersion"] = 1;
    bundleMetadata["submissionId"] = request.SubmissionId;
    bundleMetadata["storefront"] = request.Storefront;
    bundleMetadata["channel"] = request.Channel;
    bundleMetadata["metadata"] = {{"productId", request.Metadata.ProductId},
                                  {"productName", request.Metadata.ProductName},
                                  {"version", request.Metadata.Version},
                                  {"buildId", request.Metadata.BuildId}};
    bundleMetadata["artifacts"] = nlohmann::json::array();
    for (const PreparedArtifact& artifact : preparedArtifacts) {
        bundleMetadata["artifacts"].push_back({{"artifactId", artifact.ArtifactId},
                                               {"relativePath", artifact.RelativePath},
                                               {"byteSize", artifact.ByteSize},
                                               {"checksum", artifact.Checksum}});
    }

    const std::string bundleMetadataPayload = bundleMetadata.dump(2);
    const std::string bundleChecksum = ComputeChecksum(bundleMetadataPayload);

    nlohmann::json checksumManifest;
    checksumManifest["schemaVersion"] = 1;
    checksumManifest["submissionId"] = request.SubmissionId;
    checksumManifest["bundleChecksum"] = bundleChecksum;
    checksumManifest["entries"] = nlohmann::json::array();
    for (const PreparedArtifact& artifact : preparedArtifacts) {
        checksumManifest["entries"].push_back({{"artifactId", artifact.ArtifactId},
                                               {"relativePath", artifact.RelativePath},
                                               {"byteSize", artifact.ByteSize},
                                               {"checksum", artifact.Checksum}});
    }

    const std::string checksumManifestPayload = checksumManifest.dump(2);
    const std::string checksumManifestDigest = ComputeChecksum(checksumManifestPayload);
    const std::string deterministicDigest = ComputeChecksum(bundleMetadataPayload + checksumManifestPayload);

    nlohmann::json manifestIndex;
    manifestIndex["schemaVersion"] = 1;
    manifestIndex["submissionId"] = request.SubmissionId;
    manifestIndex["storefront"] = request.Storefront;
    manifestIndex["channel"] = request.Channel;
    manifestIndex["artifactCount"] = static_cast<uint64_t>(preparedArtifacts.size());
    manifestIndex["bundleMetadataFile"] = "store_submission_bundle_metadata.json";
    manifestIndex["checksumManifestFile"] = "store_submission_checksum_manifest.json";
    manifestIndex["bundleMetadataChecksum"] = bundleChecksum;
    manifestIndex["checksumManifestChecksum"] = checksumManifestDigest;
    manifestIndex["deterministicDigest"] = deterministicDigest;

    const std::string manifestIndexPayload = manifestIndex.dump(2);

    const std::filesystem::path bundleMetadataPath = request.OutputDirectory / "store_submission_bundle_metadata.json";
    const std::filesystem::path checksumManifestPath = request.OutputDirectory / "store_submission_checksum_manifest.json";
    const std::filesystem::path manifestIndexPath = request.OutputDirectory / "store_submission_index.json";

    if (!WriteFile(bundleMetadataPath, bundleMetadataPayload) || !WriteFile(checksumManifestPath, checksumManifestPayload) ||
        !WriteFile(manifestIndexPath, manifestIndexPayload)) {
        return Result<StoreSubmissionResult>::Failure("PACKAGE_WRITE_FAILED");
    }

    StoreSubmissionResult result{};
    result.Storefront = request.Storefront;
    result.Channel = request.Channel;
    result.SubmissionId = request.SubmissionId;
    result.BundleMetadataPath = bundleMetadataPath;
    result.ChecksumManifestPath = checksumManifestPath;
    result.ManifestIndexPath = manifestIndexPath;
    result.BundleMetadataChecksum = bundleChecksum;
    result.ChecksumManifestChecksum = checksumManifestDigest;
    result.DeterministicDigest = deterministicDigest;
    result.PackageEntries.reserve(preparedArtifacts.size());
    for (const PreparedArtifact& artifact : preparedArtifacts) {
        StoreSubmissionPackageEntry entry{};
        entry.ArtifactId = artifact.ArtifactId;
        entry.RelativePath = artifact.RelativePath;
        entry.ByteSize = artifact.ByteSize;
        entry.Checksum = artifact.Checksum;
        result.PackageEntries.push_back(std::move(entry));
    }

    return Result<StoreSubmissionResult>::Success(std::move(result));
}

} // namespace Core::Build
