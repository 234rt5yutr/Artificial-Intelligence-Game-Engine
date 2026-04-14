#pragma once

#include "Core/Asset/Addressables/AddressablesCatalogTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Core::Automation {

template <typename T>
using Result = Core::Asset::Addressables::Result<T>;

struct PlayModeSuiteRequest {
    std::string SuiteName;
    std::string ScenePath;
    uint32_t DeterministicSeed = 0;
    float FixedDeltaTime = 0.0f;
    uint32_t MaxFrames = 0;
    std::vector<std::string> Assertions;
};

struct PlayModeAssertionResult {
    std::string AssertionName;
    bool Passed = false;
    std::string Diagnostic;
};

struct PlayModeCaseResult {
    std::string CaseName;
    bool Passed = false;
    uint32_t SimulatedFrames = 0;
    std::vector<PlayModeAssertionResult> AssertionResults;
    std::vector<std::string> Diagnostics;
};

struct PlayModeSuiteResult {
    std::string SuiteName;
    std::string ScenePath;
    uint32_t DeterministicSeed = 0;
    float FixedDeltaTime = 0.0f;
    uint32_t MaxFrames = 0;
    uint32_t SimulatedFrames = 0;
    bool Passed = false;
    std::vector<PlayModeCaseResult> CaseResults;
    std::vector<std::string> Diagnostics;
};

} // namespace Core::Automation
