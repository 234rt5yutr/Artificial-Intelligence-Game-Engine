#include "Core/Automation/PlayModeTestRunner.h"

#include "Core/ECS/Scene.h"
#include "Core/Log.h"
#include "Core/State/SceneLoader.h"

#include <random>
#include <string>
#include <utility>

namespace Core::Automation {
namespace {

[[nodiscard]] bool IsRequestValid(const PlayModeSuiteRequest& request) {
    if (request.SuiteName.empty() || request.ScenePath.empty()) {
        return false;
    }

    if (request.FixedDeltaTime <= 0.0f || request.MaxFrames == 0) {
        return false;
    }

    return !request.Assertions.empty();
}

[[nodiscard]] PlayModeAssertionResult EvaluateAssertion(const std::string& assertionName,
                                                        const Core::ECS::Scene& scene,
                                                        const uint32_t simulatedFrames,
                                                        const PlayModeSuiteRequest& request) {
    PlayModeAssertionResult result{};
    result.AssertionName = assertionName;

    if (assertionName == "SCENE_LOADED") {
        result.Passed = true;
        result.Diagnostic = "AUTOMATION_ASSERTION_PASSED";
        return result;
    }

    if (assertionName == "ENTITY_COUNT_NON_ZERO") {
        const std::size_t entityCount = scene.GetEntityCount();
        result.Passed = entityCount > 0;
        result.Diagnostic = "ENTITY_COUNT=" + std::to_string(entityCount);
        return result;
    }

    if (assertionName == "FRAME_COUNT_MATCH") {
        result.Passed = simulatedFrames == request.MaxFrames;
        result.Diagnostic = "SIMULATED_FRAMES=" + std::to_string(simulatedFrames);
        return result;
    }

    result.Passed = false;
    result.Diagnostic = "AUTOMATION_ASSERTION_UNSUPPORTED";
    return result;
}

} // namespace

Result<PlayModeSuiteResult> RunAutomatedPlayModeTests(const PlayModeSuiteRequest& request) {
    if (!IsRequestValid(request)) {
        return Result<PlayModeSuiteResult>::Failure("AUTOMATION_INVALID_REQUEST");
    }

    if (!Engine::Log::GetCoreLogger()) {
        Engine::Log::Init();
    }

    Core::State::SceneLoader& sceneLoader = Core::State::SceneLoader::Get();
    sceneLoader.Initialize();

    std::unique_ptr<Core::ECS::Scene> scene = sceneLoader.LoadScene(request.ScenePath);
    if (!scene) {
        return Result<PlayModeSuiteResult>::Failure("AUTOMATION_SCENE_LOAD_FAILED");
    }

    std::mt19937 deterministicRng(request.DeterministicSeed);
    uint32_t simulatedFrames = 0;
    uint32_t deterministicState = 0;

    for (; simulatedFrames < request.MaxFrames; ++simulatedFrames) {
        scene->OnUpdate(request.FixedDeltaTime);
        deterministicState ^= deterministicRng();
    }

    PlayModeCaseResult caseResult{};
    caseResult.CaseName = request.SuiteName + "::default";
    caseResult.SimulatedFrames = simulatedFrames;
    caseResult.Passed = true;

    caseResult.AssertionResults.reserve(request.Assertions.size());
    for (const std::string& assertionName : request.Assertions) {
        PlayModeAssertionResult assertionResult =
            EvaluateAssertion(assertionName, *scene, simulatedFrames, request);
        caseResult.Passed = caseResult.Passed && assertionResult.Passed;
        caseResult.AssertionResults.push_back(std::move(assertionResult));
    }

    caseResult.Diagnostics.push_back("DETERMINISTIC_STATE=" + std::to_string(deterministicState));
    caseResult.Diagnostics.push_back("SIMULATED_FRAMES=" + std::to_string(simulatedFrames));

    PlayModeSuiteResult suiteResult{};
    suiteResult.SuiteName = request.SuiteName;
    suiteResult.ScenePath = request.ScenePath;
    suiteResult.DeterministicSeed = request.DeterministicSeed;
    suiteResult.FixedDeltaTime = request.FixedDeltaTime;
    suiteResult.MaxFrames = request.MaxFrames;
    suiteResult.SimulatedFrames = simulatedFrames;
    suiteResult.CaseResults.push_back(std::move(caseResult));
    suiteResult.Passed = suiteResult.CaseResults[0].Passed;
    suiteResult.Diagnostics.push_back("AUTOMATION_PLAY_MODE_COMPLETED");

    return Result<PlayModeSuiteResult>::Success(std::move(suiteResult));
}

} // namespace Core::Automation
