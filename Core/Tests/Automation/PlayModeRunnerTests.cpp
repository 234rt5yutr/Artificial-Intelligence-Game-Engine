#include "Core/Automation/PlayModeTestRunner.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

[[noreturn]] void FailExpectation(const char* expression, int line) {
    std::fprintf(stderr, "Expectation failed at line %d: %s\n", line, expression);
    std::fflush(stderr);
    std::abort();
}

#define TEST_EXPECT(condition)            \
    do {                                  \
        if (!(condition)) {               \
            FailExpectation(#condition, __LINE__); \
        }                                 \
    } while (false)

std::filesystem::path BuildScenePath(const std::string& name) {
    return std::filesystem::path("build") / "automation" / "play-mode-tests" / (name + ".json");
}

void WriteSceneFile(const std::filesystem::path& scenePath) {
    std::error_code errorCode;
    std::filesystem::create_directories(scenePath.parent_path(), errorCode);
    TEST_EXPECT(!errorCode);

    std::ofstream output(scenePath, std::ios::trunc);
    TEST_EXPECT(output.is_open());

    output << R"({
  "name": "automation-scene",
  "entities": [
    {
      "components": {
        "transform": {
          "position": [0.0, 0.0, 0.0],
          "rotation": [0.0, 0.0, 0.0],
          "scale": [1.0, 1.0, 1.0]
        }
      }
    }
  ]
})";
}

Core::Automation::PlayModeSuiteRequest BuildValidRequest(const std::filesystem::path& scenePath) {
    Core::Automation::PlayModeSuiteRequest request{};
    request.SuiteName = "deterministic-pack";
    request.ScenePath = scenePath.string();
    request.DeterministicSeed = 1337;
    request.FixedDeltaTime = 1.0f / 60.0f;
    request.MaxFrames = 10;
    request.Assertions = {
        "SCENE_LOADED",
        "ENTITY_COUNT_NON_ZERO",
        "FRAME_COUNT_MATCH"
    };
    return request;
}

} // namespace

int main() {
    using namespace Core::Automation;

    {
        const PlayModeSuiteRequest invalidRequest{};
        const Result<PlayModeSuiteResult> invalidResult = RunAutomatedPlayModeTests(invalidRequest);
        TEST_EXPECT(!invalidResult.Ok);
        TEST_EXPECT(invalidResult.Error == "AUTOMATION_INVALID_REQUEST");
    }

    {
        PlayModeSuiteRequest missingSceneRequest{};
        missingSceneRequest.SuiteName = "missing-scene-pack";
        missingSceneRequest.ScenePath = "build/automation/play-mode-tests/missing-scene.json";
        missingSceneRequest.DeterministicSeed = 7;
        missingSceneRequest.FixedDeltaTime = 1.0f / 60.0f;
        missingSceneRequest.MaxFrames = 4;
        missingSceneRequest.Assertions = {"SCENE_LOADED"};

        const Result<PlayModeSuiteResult> missingSceneResult = RunAutomatedPlayModeTests(missingSceneRequest);
        TEST_EXPECT(!missingSceneResult.Ok);
        TEST_EXPECT(missingSceneResult.Error == "AUTOMATION_SCENE_LOAD_FAILED");
    }

    {
        const std::filesystem::path scenePath = BuildScenePath("deterministic");
        WriteSceneFile(scenePath);

        const PlayModeSuiteRequest request = BuildValidRequest(scenePath);
        const Result<PlayModeSuiteResult> firstRun = RunAutomatedPlayModeTests(request);
        const Result<PlayModeSuiteResult> secondRun = RunAutomatedPlayModeTests(request);

        TEST_EXPECT(firstRun.Ok);
        TEST_EXPECT(secondRun.Ok);
        TEST_EXPECT(firstRun.Value.Passed);
        TEST_EXPECT(secondRun.Value.Passed);
        TEST_EXPECT(firstRun.Value.SimulatedFrames == request.MaxFrames);
        TEST_EXPECT(secondRun.Value.SimulatedFrames == request.MaxFrames);
        TEST_EXPECT(firstRun.Value.CaseResults.size() == 1);
        TEST_EXPECT(secondRun.Value.CaseResults.size() == 1);
        TEST_EXPECT(firstRun.Value.CaseResults[0].AssertionResults.size() == request.Assertions.size());
        TEST_EXPECT(secondRun.Value.CaseResults[0].AssertionResults.size() == request.Assertions.size());

        for (size_t index = 0; index < request.Assertions.size(); ++index) {
            TEST_EXPECT(firstRun.Value.CaseResults[0].AssertionResults[index].AssertionName ==
                        secondRun.Value.CaseResults[0].AssertionResults[index].AssertionName);
            TEST_EXPECT(firstRun.Value.CaseResults[0].AssertionResults[index].Passed ==
                        secondRun.Value.CaseResults[0].AssertionResults[index].Passed);
            TEST_EXPECT(firstRun.Value.CaseResults[0].AssertionResults[index].Diagnostic ==
                        secondRun.Value.CaseResults[0].AssertionResults[index].Diagnostic);
        }
    }

    return 0;
}
