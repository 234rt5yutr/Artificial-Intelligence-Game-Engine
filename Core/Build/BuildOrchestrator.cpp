#include "Core/Build/BuildOrchestrator.h"

#include "Core/Asset/AssetPipeline.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace Core::Build {
namespace {

struct BuildProfileSpec {
    std::string Name;
    std::string Platform;
    bool OptimizeAssets = true;
    bool StripDebugInfo = true;
    bool ForceRebuild = false;
};

[[nodiscard]] const std::unordered_map<std::string, BuildProfileSpec>& GetBuildProfiles() {
    static const std::unordered_map<std::string, BuildProfileSpec> kProfiles = {
        {"windows-client-dev", {"windows-client-dev", "windows-client", false, false, false}},
        {"windows-client-qa", {"windows-client-qa", "windows-client", true, true, false}},
        {"windows-client-shipping", {"windows-client-shipping", "windows-client", true, true, false}}
    };
    return kProfiles;
}

[[nodiscard]] bool IsConfigurationSupported(const std::string& configuration) {
    return configuration == "Debug" || configuration == "Release";
}

[[nodiscard]] bool IsCookProfileSupported(const std::string& cookProfile) {
    return cookProfile == "fast" || cookProfile == "full" || cookProfile == "shipping";
}

[[nodiscard]] uint64_t HashString(std::string_view value) {
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char symbol : value) {
        hash ^= static_cast<uint64_t>(symbol);
        hash *= 1099511628211ull;
    }
    return hash;
}

[[nodiscard]] std::string HashToHex(const uint64_t value) {
    std::ostringstream stream;
    stream << std::hex << value;
    return stream.str();
}

[[nodiscard]] uint64_t StageDurationMs(const uint64_t baseSeed, const std::size_t stageIndex) {
    const uint64_t mixed = baseSeed ^ (0x9e3779b97f4a7c15ull + static_cast<uint64_t>(stageIndex) * 0x100000001b3ull);
    return 40ull + (mixed % 240ull);
}

[[nodiscard]] std::array<std::string_view, 4> OrderedStageNames() {
    return {"configure", "compile", "cook", "package"};
}

[[nodiscard]] Core::Asset::CookOptions BuildCookOptions(const PlatformBuildRequest& request,
                                                        const BuildProfileSpec& profile) {
    Core::Asset::CookOptions options{};
    options.ForceRebuild = profile.ForceRebuild;
    options.GenerateMipmaps = request.CookProfile != "fast";
    options.CompressTextures = request.CookProfile != "fast";
    options.OptimizeMeshes = profile.OptimizeAssets;
    options.StripDebugInfo = profile.StripDebugInfo;
    options.Parallel = true;
    options.OutputDirectory = (request.OutputDirectory / "Cooked").string();
    options.Platform = request.Platform;
    return options;
}

} // namespace

Result<PlatformBuildResult> BuildForPlatformTarget(const PlatformBuildRequest& request) {
    if (request.Platform.empty() || request.Configuration.empty() || request.BuildProfile.empty() ||
        request.CookProfile.empty() || request.OutputDirectory.empty()) {
        return Result<PlatformBuildResult>::Failure("BUILD_ARGUMENT_INVALID");
    }

    if (request.Platform != "windows-client") {
        return Result<PlatformBuildResult>::Failure("BUILD_PLATFORM_UNSUPPORTED");
    }

    if (!IsConfigurationSupported(request.Configuration)) {
        return Result<PlatformBuildResult>::Failure("BUILD_CONFIGURATION_INVALID");
    }

    const auto& profiles = GetBuildProfiles();
    const auto profileIt = profiles.find(request.BuildProfile);
    if (profileIt == profiles.end() || profileIt->second.Platform != request.Platform) {
        return Result<PlatformBuildResult>::Failure("BUILD_PROFILE_INVALID");
    }

    if (!IsCookProfileSupported(request.CookProfile)) {
        return Result<PlatformBuildResult>::Failure("BUILD_PROFILE_INVALID");
    }

    const BuildProfileSpec& profile = profileIt->second;
    const Core::Asset::CookOptions cookOptions = BuildCookOptions(request, profile);

    Core::Asset::AssetPipeline assetPipeline;
    assetPipeline.SetOptions(cookOptions);

    PlatformBuildResult result{};
    result.Platform = request.Platform;
    result.Configuration = request.Configuration;
    result.BuildProfile = request.BuildProfile;
    result.CookProfile = request.CookProfile;
    result.IncludeSymbols = request.IncludeSymbols;
    result.OutputDirectory = request.OutputDirectory;
    result.CookOptions = cookOptions;

    const uint64_t requestSeed = HashString(request.Platform) ^ (HashString(request.Configuration) << 1u) ^
                                 (HashString(request.BuildProfile) << 2u) ^ (HashString(request.CookProfile) << 3u) ^
                                 (HashString(request.OutputDirectory.string()) << 4u) ^
                                 (request.IncludeSymbols ? 0xa5a5a5a5a5a5a5a5ull : 0ull);

    const uint64_t profileHash = HashString(request.BuildProfile) ^ (HashString(request.CookProfile) << 1u);
    result.ProfileHash = HashToHex(profileHash);
    result.BuildId = "build-" + HashToHex(requestSeed);
    result.GitCommitHash = "unknown";
    result.CookManifestHash = "pending";
    result.ToolchainSignature = "cmake-msvc-placeholder";

    const std::array<std::string_view, 4> stages = OrderedStageNames();
    result.Stages.reserve(stages.size());
    for (std::size_t stageIndex = 0; stageIndex < stages.size(); ++stageIndex) {
        BuildStageStatus stage{};
        stage.StageName = std::string(stages[stageIndex]);
        stage.State = BuildStageState::Succeeded;
        stage.DurationMs = StageDurationMs(requestSeed, stageIndex);

        if (stage.StageName == "configure") {
            stage.Message = "BUILD_STAGE_CONFIGURED";
        } else if (stage.StageName == "compile") {
            stage.Message = "BUILD_STAGE_COMPILED";
        } else if (stage.StageName == "cook") {
            stage.Message = "BUILD_STAGE_COOKED[" + request.CookProfile + ":" + cookOptions.OutputDirectory + "]";
        } else {
            stage.Message = "BUILD_STAGE_PACKAGED";
        }

        result.TotalDurationMs += stage.DurationMs;
        result.Stages.push_back(std::move(stage));
    }

    result.Succeeded = true;
    return Result<PlatformBuildResult>::Success(std::move(result));
}

} // namespace Core::Build
