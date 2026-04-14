#include "Core/Build/DedicatedServerBuildService.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>

namespace Core::Build {
namespace {

struct DedicatedServerProfileSpec {
    std::string Name;
    std::string Platform;
};

[[nodiscard]] const std::unordered_map<std::string, DedicatedServerProfileSpec>& GetDedicatedServerProfiles() {
    static const std::unordered_map<std::string, DedicatedServerProfileSpec> kProfiles = {
        {"windows-dedicated-dev", {"windows-dedicated-dev", "windows-server"}},
        {"windows-dedicated-shipping", {"windows-dedicated-shipping", "windows-server"}}
    };
    return kProfiles;
}

[[nodiscard]] bool IsConfigurationSupported(const std::string& configuration) {
    return configuration == "Debug" || configuration == "Release";
}

[[nodiscard]] uint64_t HashString(std::string_view value) {
    constexpr uint64_t kFnvOffset = 14695981039346656037ull;
    constexpr uint64_t kFnvPrime = 1099511628211ull;

    uint64_t hash = kFnvOffset;
    for (const unsigned char symbol : value) {
        hash ^= static_cast<uint64_t>(symbol);
        hash *= kFnvPrime;
    }
    return hash;
}

[[nodiscard]] std::string HashToHex(const uint64_t value) {
    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << value;
    return stream.str();
}

[[nodiscard]] std::string ComputeChecksum(const std::string& payload) {
    return HashToHex(HashString(payload));
}

[[nodiscard]] bool WriteFile(const std::filesystem::path& filePath, const std::string& content) {
    std::ofstream output(filePath, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }

    output << content;
    output.flush();
    return output.good();
}

} // namespace

Result<DedicatedServerBuildResult> GenerateDedicatedServerBuildArtifacts(const DedicatedServerBuildRequest& request) {
    if (request.Platform.empty() || request.Configuration.empty() || request.BuildProfile.empty() ||
        request.OutputDirectory.empty()) {
        return Result<DedicatedServerBuildResult>::Failure("BUILD_ARGUMENT_INVALID");
    }

    if (request.Platform != "windows-server") {
        return Result<DedicatedServerBuildResult>::Failure("BUILD_PLATFORM_UNSUPPORTED");
    }

    if (!IsConfigurationSupported(request.Configuration)) {
        return Result<DedicatedServerBuildResult>::Failure("BUILD_CONFIGURATION_INVALID");
    }

    const auto& profiles = GetDedicatedServerProfiles();
    const auto profileIt = profiles.find(request.BuildProfile);
    if (profileIt == profiles.end() || profileIt->second.Platform != request.Platform) {
        return Result<DedicatedServerBuildResult>::Failure("BUILD_PROFILE_INVALID");
    }

    std::error_code errorCode;
    const bool outputExists = std::filesystem::exists(request.OutputDirectory, errorCode);
    if (errorCode) {
        return Result<DedicatedServerBuildResult>::Failure("DEDICATED_SERVER_ARTIFACT_FAILED");
    }

    if (outputExists) {
        const bool isDirectory = std::filesystem::is_directory(request.OutputDirectory, errorCode);
        if (errorCode || !isDirectory) {
            return Result<DedicatedServerBuildResult>::Failure("DEDICATED_SERVER_ARTIFACT_FAILED");
        }
    } else {
        std::filesystem::create_directories(request.OutputDirectory, errorCode);
        if (errorCode) {
            return Result<DedicatedServerBuildResult>::Failure("DEDICATED_SERVER_ARTIFACT_FAILED");
        }
    }

    const std::filesystem::path binaryPath = request.OutputDirectory / "Bin" / "AIGameEngineServer.exe";
    const std::filesystem::path configTemplatePath = request.OutputDirectory / "Config" / "server.config.template.json";
    const std::filesystem::path symbolsPath = request.OutputDirectory / "Symbols" / "AIGameEngineServer.pdb";
    const std::filesystem::path deploymentManifestPath = request.OutputDirectory / "deploy.manifest.json";

    std::filesystem::create_directories(configTemplatePath.parent_path(), errorCode);
    if (errorCode) {
        return Result<DedicatedServerBuildResult>::Failure("DEDICATED_SERVER_ARTIFACT_FAILED");
    }

    std::filesystem::create_directories(deploymentManifestPath.parent_path(), errorCode);
    if (errorCode) {
        return Result<DedicatedServerBuildResult>::Failure("DEDICATED_SERVER_ARTIFACT_FAILED");
    }

    const nlohmann::json configTemplate = {
        {"schemaVersion", 1},
        {"platform", request.Platform},
        {"configuration", request.Configuration},
        {"buildProfile", request.BuildProfile},
        {"headlessMode", true},
        {"clientPayloadEnabled", false},
        {"uiPayloadEnabled", false},
        {"network", {{"bindAddress", "0.0.0.0"}, {"port", 7777}}}
    };
    const std::string configTemplatePayload = configTemplate.dump(2);

    const std::string binaryChecksum =
        ComputeChecksum(binaryPath.string() + request.Platform + request.Configuration + request.BuildProfile);
    const std::string configTemplateChecksum = ComputeChecksum(configTemplatePayload);
    const std::string symbolsChecksum =
        request.IncludeSymbols ? ComputeChecksum(symbolsPath.string() + request.BuildProfile) : "symbols-excluded";

    const nlohmann::json deploymentManifest = {
        {"schemaVersion", 1},
        {"platform", request.Platform},
        {"configuration", request.Configuration},
        {"profile", request.BuildProfile},
        {"headless", true},
        {"excludeClientPayload", true},
        {"excludeUiPayload", true},
        {"symbolHandling", request.IncludeSymbols ? "SYMBOLS_INCLUDED" : "SYMBOLS_EXCLUDED"},
        {"artifacts",
         {{"binaryPath", binaryPath.string()},
          {"configTemplatePath", configTemplatePath.string()},
          {"symbolsPath", request.IncludeSymbols ? symbolsPath.string() : ""},
          {"deploymentManifestPath", deploymentManifestPath.string()},
          {"binaryChecksum", binaryChecksum},
          {"configTemplateChecksum", configTemplateChecksum},
          {"symbolsChecksum", symbolsChecksum}}}
    };

    const std::string manifestPayloadWithoutDigest = deploymentManifest.dump(2);
    const std::string deterministicDigest = ComputeChecksum(manifestPayloadWithoutDigest);
    nlohmann::json manifestWithDigest = deploymentManifest;
    manifestWithDigest["deterministicDigest"] = deterministicDigest;
    const std::string manifestPayload = manifestWithDigest.dump(2);
    const std::string deploymentManifestChecksum = ComputeChecksum(manifestPayload);

    if (!WriteFile(configTemplatePath, configTemplatePayload) || !WriteFile(deploymentManifestPath, manifestPayload)) {
        return Result<DedicatedServerBuildResult>::Failure("DEDICATED_SERVER_ARTIFACT_FAILED");
    }

    DedicatedServerBuildResult result{};
    result.Platform = request.Platform;
    result.Configuration = request.Configuration;
    result.BuildProfile = request.BuildProfile;
    result.IncludeSymbols = request.IncludeSymbols;
    result.OutputDirectory = request.OutputDirectory;
    result.ExcludesClientPayload = true;
    result.ExcludesUiPayload = true;
    result.SymbolHandlingState = request.IncludeSymbols ? "SYMBOLS_INCLUDED" : "SYMBOLS_EXCLUDED";
    result.BinaryPath = binaryPath;
    result.ConfigTemplatePath = configTemplatePath;
    result.SymbolsPath = request.IncludeSymbols ? symbolsPath : std::filesystem::path{};
    result.DeploymentManifestPath = deploymentManifestPath;
    result.DeterministicDigest = deterministicDigest;
    result.Artifacts.BinaryPath = binaryPath;
    result.Artifacts.ConfigTemplatePath = configTemplatePath;
    result.Artifacts.SymbolsPath = request.IncludeSymbols ? symbolsPath : std::filesystem::path{};
    result.Artifacts.DeploymentManifestPath = deploymentManifestPath;
    result.Artifacts.BinaryChecksum = binaryChecksum;
    result.Artifacts.ConfigTemplateChecksum = configTemplateChecksum;
    result.Artifacts.SymbolsChecksum = symbolsChecksum;
    result.Artifacts.DeploymentManifestChecksum = deploymentManifestChecksum;
    result.Succeeded = true;

    return Result<DedicatedServerBuildResult>::Success(std::move(result));
}

} // namespace Core::Build
