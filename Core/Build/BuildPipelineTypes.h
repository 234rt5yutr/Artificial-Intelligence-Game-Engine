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

} // namespace Core::Build
