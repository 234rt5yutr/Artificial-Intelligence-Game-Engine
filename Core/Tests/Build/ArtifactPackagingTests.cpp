#include "Core/Build/StoreSubmissionPackager.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace {

Core::Build::StoreSubmissionRequest BuildValidRequest(const std::filesystem::path& sourceRoot,
                                                      const std::filesystem::path& outputRoot) {
    Core::Build::StoreSubmissionRequest request{};
    request.Storefront = "steam";
    request.Channel = "certification";
    request.SubmissionId = "submission-2832";
    request.SourceArtifactsRoot = sourceRoot;
    request.OutputDirectory = outputRoot;

    request.Metadata.ProductId = "com.aigameengine.game";
    request.Metadata.ProductName = "AI Game";
    request.Metadata.Version = "1.0.0";
    request.Metadata.BuildId = "build-2832";

    Core::Build::StoreSubmissionManifestEntry mainEntry{};
    mainEntry.ArtifactId = "main";
    mainEntry.RelativePath = "Game.bin";

    Core::Build::StoreSubmissionManifestEntry symbolsEntry{};
    symbolsEntry.ArtifactId = "symbols";
    symbolsEntry.RelativePath = "Game.pdb";

    request.ManifestEntries = {symbolsEntry, mainEntry};
    return request;
}

void WriteArtifact(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output.is_open());
    output << contents;
    output.flush();
    assert(output.good());
}

nlohmann::json ReadJson(const std::filesystem::path& path) {
    std::ifstream input(path);
    assert(input.is_open());
    return nlohmann::json::parse(input);
}

} // namespace

int main() {
    using namespace Core::Build;

    std::error_code errorCode;
    const std::filesystem::path root = std::filesystem::path("build") / "artifact-packaging-tests";
    std::filesystem::remove_all(root, errorCode);

    const std::filesystem::path sourceRoot = root / "source";
    const std::filesystem::path outputRoot = root / "output";
    std::filesystem::create_directories(sourceRoot, errorCode);
    assert(!errorCode);

    WriteArtifact(sourceRoot / "Game.bin", "main-content");
    WriteArtifact(sourceRoot / "Game.pdb", "symbol-content");

    {
        StoreSubmissionRequest invalidRequest{};
        const Result<StoreSubmissionResult> result = PackageStoreSubmissionArtifacts(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "PACKAGE_REQUEST_INVALID");
    }

    {
        StoreSubmissionRequest invalidRequest = BuildValidRequest(sourceRoot, outputRoot);
        invalidRequest.Storefront = "unknown";
        const Result<StoreSubmissionResult> result = PackageStoreSubmissionArtifacts(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "PACKAGE_STOREFRONT_UNSUPPORTED");
    }

    {
        StoreSubmissionRequest invalidRequest = BuildValidRequest(sourceRoot, outputRoot);
        invalidRequest.Channel = "beta";
        const Result<StoreSubmissionResult> result = PackageStoreSubmissionArtifacts(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "PACKAGE_CHANNEL_INVALID");
    }

    {
        StoreSubmissionRequest invalidRequest = BuildValidRequest(sourceRoot, outputRoot);
        invalidRequest.Metadata.ProductName.clear();
        const Result<StoreSubmissionResult> result = PackageStoreSubmissionArtifacts(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "PACKAGE_METADATA_INVALID");
    }

    {
        StoreSubmissionRequest invalidRequest = BuildValidRequest(sourceRoot, outputRoot);
        invalidRequest.ManifestEntries[0].RelativePath.clear();
        const Result<StoreSubmissionResult> result = PackageStoreSubmissionArtifacts(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "PACKAGE_MANIFEST_INVALID");
    }

    {
        StoreSubmissionRequest invalidRequest = BuildValidRequest(sourceRoot, outputRoot);
        invalidRequest.ManifestEntries[0].RelativePath = "missing.file";
        const Result<StoreSubmissionResult> result = PackageStoreSubmissionArtifacts(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "PACKAGE_ARTIFACT_MISSING");
    }

    {
        StoreSubmissionRequest request = BuildValidRequest(sourceRoot, outputRoot);
        const Result<StoreSubmissionResult> first = PackageStoreSubmissionArtifacts(request);
        assert(first.Ok);
        assert(std::filesystem::exists(first.Value.BundleMetadataPath));
        assert(std::filesystem::exists(first.Value.ChecksumManifestPath));
        assert(std::filesystem::exists(first.Value.ManifestIndexPath));
        assert(first.Value.PackageEntries.size() == 2);
        assert(first.Value.PackageEntries[0].ArtifactId == "main");
        assert(first.Value.PackageEntries[1].ArtifactId == "symbols");

        const nlohmann::json metadataJson = ReadJson(first.Value.BundleMetadataPath);
        assert(metadataJson["submissionId"] == request.SubmissionId);
        assert(metadataJson["storefront"] == request.Storefront);
        assert(metadataJson["channel"] == request.Channel);
        assert(metadataJson["artifacts"].size() == 2);

        const nlohmann::json checksumManifest = ReadJson(first.Value.ChecksumManifestPath);
        assert(checksumManifest["schemaVersion"] == 1);
        assert(checksumManifest["entries"].size() == 2);
        assert(checksumManifest["entries"][0]["artifactId"] == "main");
        assert(checksumManifest["entries"][1]["artifactId"] == "symbols");

        const Result<StoreSubmissionResult> second = PackageStoreSubmissionArtifacts(request);
        assert(second.Ok);
        assert(first.Value.DeterministicDigest == second.Value.DeterministicDigest);
    }

    return 0;
}
