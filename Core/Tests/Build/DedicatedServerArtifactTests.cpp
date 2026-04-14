#include "Core/Build/DedicatedServerBuildService.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace {

Core::Build::DedicatedServerBuildRequest BuildValidRequest(const std::filesystem::path& outputRoot) {
    Core::Build::DedicatedServerBuildRequest request{};
    request.Platform = "windows-server";
    request.Configuration = "Release";
    request.BuildProfile = "windows-dedicated-shipping";
    request.IncludeSymbols = true;
    request.OutputDirectory = outputRoot;
    return request;
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
    const std::filesystem::path root = std::filesystem::path("build") / "dedicated-server-artifact-tests";
    std::filesystem::remove_all(root, errorCode);

    {
        DedicatedServerBuildRequest invalidRequest{};
        const Result<DedicatedServerBuildResult> result = GenerateDedicatedServerBuildArtifacts(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "BUILD_ARGUMENT_INVALID");
    }

    {
        DedicatedServerBuildRequest invalidRequest = BuildValidRequest(root / "invalid-platform");
        invalidRequest.Platform = "linux-server";
        const Result<DedicatedServerBuildResult> result = GenerateDedicatedServerBuildArtifacts(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "BUILD_PLATFORM_UNSUPPORTED");
    }

    {
        DedicatedServerBuildRequest invalidRequest = BuildValidRequest(root / "invalid-config");
        invalidRequest.Configuration = "Profile";
        const Result<DedicatedServerBuildResult> result = GenerateDedicatedServerBuildArtifacts(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "BUILD_CONFIGURATION_INVALID");
    }

    {
        DedicatedServerBuildRequest invalidRequest = BuildValidRequest(root / "invalid-profile");
        invalidRequest.BuildProfile = "not-a-profile";
        const Result<DedicatedServerBuildResult> result = GenerateDedicatedServerBuildArtifacts(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "BUILD_PROFILE_INVALID");
    }

    {
        DedicatedServerBuildRequest request = BuildValidRequest(root / "release-output");
        const Result<DedicatedServerBuildResult> first = GenerateDedicatedServerBuildArtifacts(request);
        assert(first.Ok);
        assert(first.Value.Succeeded);
        assert(first.Value.ExcludesClientPayload);
        assert(first.Value.ExcludesUiPayload);
        assert(first.Value.SymbolHandlingState == "SYMBOLS_INCLUDED");
        assert(!first.Value.BinaryPath.empty());
        assert(!first.Value.ConfigTemplatePath.empty());
        assert(!first.Value.SymbolsPath.empty());
        assert(!first.Value.DeploymentManifestPath.empty());
        assert(!first.Value.DeterministicDigest.empty());
        assert(std::filesystem::exists(first.Value.ConfigTemplatePath));
        assert(std::filesystem::exists(first.Value.DeploymentManifestPath));

        const nlohmann::json deploymentManifest = ReadJson(first.Value.DeploymentManifestPath);
        assert(deploymentManifest["schemaVersion"] == 1);
        assert(deploymentManifest["platform"] == request.Platform);
        assert(deploymentManifest["profile"] == request.BuildProfile);
        assert(deploymentManifest["deterministicDigest"] == first.Value.DeterministicDigest);
        assert(deploymentManifest["artifacts"]["binaryPath"] == first.Value.BinaryPath.string());

        const Result<DedicatedServerBuildResult> second = GenerateDedicatedServerBuildArtifacts(request);
        assert(second.Ok);
        assert(second.Value.DeterministicDigest == first.Value.DeterministicDigest);
    }

    {
        DedicatedServerBuildRequest request = BuildValidRequest(root / "symbols-excluded");
        request.IncludeSymbols = false;
        const Result<DedicatedServerBuildResult> result = GenerateDedicatedServerBuildArtifacts(request);
        assert(result.Ok);
        assert(result.Value.SymbolHandlingState == "SYMBOLS_EXCLUDED");
        assert(result.Value.SymbolsPath.empty());

        const nlohmann::json deploymentManifest = ReadJson(result.Value.DeploymentManifestPath);
        assert(deploymentManifest["symbolHandling"] == "SYMBOLS_EXCLUDED");
        assert(deploymentManifest["artifacts"]["symbolsPath"] == "");
    }

    {
        const std::filesystem::path blockedPath = root / "write-blocked";
        std::ofstream blockedFile(blockedPath, std::ios::trunc);
        assert(blockedFile.is_open());
        blockedFile << "not-a-directory";
        blockedFile.flush();
        blockedFile.close();

        DedicatedServerBuildRequest invalidRequest = BuildValidRequest(blockedPath);
        const Result<DedicatedServerBuildResult> result = GenerateDedicatedServerBuildArtifacts(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "DEDICATED_SERVER_ARTIFACT_FAILED");
    }

    return 0;
}
