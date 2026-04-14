#include "Core/Build/BuildOrchestrator.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

[[noreturn]] void FailExpectation(const char* expression, int line) {
    std::fprintf(stderr, "Expectation failed at line %d: %s\n", line, expression);
    std::fflush(stderr);
    std::abort();
}

#define TEST_EXPECT(condition)                  \
    do {                                        \
        if (!(condition)) {                     \
            FailExpectation(#condition, __LINE__); \
        }                                       \
    } while (false)

Core::Build::PlatformBuildRequest BuildValidRequest() {
    Core::Build::PlatformBuildRequest request{};
    request.Platform = "windows-client";
    request.Configuration = "Release";
    request.BuildProfile = "windows-client-shipping";
    request.CookProfile = "full";
    request.IncludeSymbols = true;
    request.OutputDirectory = "build/output/windows-client";
    return request;
}

void ExpectStage(Core::Build::BuildStageStatus const& stage,
                 const std::string& expectedName) {
    TEST_EXPECT(stage.StageName == expectedName);
    TEST_EXPECT(stage.State == Core::Build::BuildStageState::Succeeded);
    TEST_EXPECT(stage.DurationMs > 0);
}

} // namespace

int main() {
    using namespace Core::Build;

    {
        PlatformBuildRequest invalidRequest{};
        const Result<PlatformBuildResult> result = BuildForPlatformTarget(invalidRequest);
        TEST_EXPECT(!result.Ok);
        TEST_EXPECT(result.Error == "BUILD_ARGUMENT_INVALID");
    }

    {
        PlatformBuildRequest invalidRequest = BuildValidRequest();
        invalidRequest.Platform = "linux-client";
        const Result<PlatformBuildResult> result = BuildForPlatformTarget(invalidRequest);
        TEST_EXPECT(!result.Ok);
        TEST_EXPECT(result.Error == "BUILD_PLATFORM_UNSUPPORTED");
    }

    {
        PlatformBuildRequest invalidRequest = BuildValidRequest();
        invalidRequest.Configuration = "Retail";
        const Result<PlatformBuildResult> result = BuildForPlatformTarget(invalidRequest);
        TEST_EXPECT(!result.Ok);
        TEST_EXPECT(result.Error == "BUILD_CONFIGURATION_INVALID");
    }

    {
        PlatformBuildRequest invalidRequest = BuildValidRequest();
        invalidRequest.BuildProfile = "unknown-profile";
        const Result<PlatformBuildResult> result = BuildForPlatformTarget(invalidRequest);
        TEST_EXPECT(!result.Ok);
        TEST_EXPECT(result.Error == "BUILD_PROFILE_INVALID");
    }

    {
        PlatformBuildRequest invalidRequest = BuildValidRequest();
        invalidRequest.CookProfile = "invalid-cook";
        const Result<PlatformBuildResult> result = BuildForPlatformTarget(invalidRequest);
        TEST_EXPECT(!result.Ok);
        TEST_EXPECT(result.Error == "BUILD_PROFILE_INVALID");
    }

    {
        const PlatformBuildRequest request = BuildValidRequest();
        const Result<PlatformBuildResult> result = BuildForPlatformTarget(request);
        TEST_EXPECT(result.Ok);
        TEST_EXPECT(result.Value.Succeeded);
        TEST_EXPECT(result.Value.Platform == request.Platform);
        TEST_EXPECT(result.Value.Configuration == request.Configuration);
        TEST_EXPECT(result.Value.BuildProfile == request.BuildProfile);
        TEST_EXPECT(result.Value.CookProfile == request.CookProfile);
        TEST_EXPECT(result.Value.OutputDirectory == request.OutputDirectory);
        TEST_EXPECT(!result.Value.BuildId.empty());
        TEST_EXPECT(!result.Value.ProfileHash.empty());
        TEST_EXPECT(result.Value.GitCommitHash == "unknown");
        TEST_EXPECT(result.Value.CookManifestHash == "pending");
        TEST_EXPECT(result.Value.Stages.size() == 4);

        ExpectStage(result.Value.Stages[0], "configure");
        ExpectStage(result.Value.Stages[1], "compile");
        ExpectStage(result.Value.Stages[2], "cook");
        ExpectStage(result.Value.Stages[3], "package");

        TEST_EXPECT(result.Value.Stages[2].Message.find("BUILD_STAGE_COOKED") != std::string::npos);
        TEST_EXPECT(result.Value.CookOptions.Platform == request.Platform);
        TEST_EXPECT(result.Value.CookOptions.OutputDirectory.find("Cooked") != std::string::npos);

        uint64_t accumulatedDuration = 0;
        for (const BuildStageStatus& stage : result.Value.Stages) {
            accumulatedDuration += stage.DurationMs;
        }
        TEST_EXPECT(result.Value.TotalDurationMs == accumulatedDuration);
    }

    {
        const PlatformBuildRequest request = BuildValidRequest();
        const Result<PlatformBuildResult> firstRun = BuildForPlatformTarget(request);
        const Result<PlatformBuildResult> secondRun = BuildForPlatformTarget(request);
        TEST_EXPECT(firstRun.Ok);
        TEST_EXPECT(secondRun.Ok);
        TEST_EXPECT(firstRun.Value.BuildId == secondRun.Value.BuildId);
        TEST_EXPECT(firstRun.Value.TotalDurationMs == secondRun.Value.TotalDurationMs);
        TEST_EXPECT(firstRun.Value.Stages.size() == secondRun.Value.Stages.size());
        for (std::size_t i = 0; i < firstRun.Value.Stages.size(); ++i) {
            TEST_EXPECT(firstRun.Value.Stages[i].StageName == secondRun.Value.Stages[i].StageName);
            TEST_EXPECT(firstRun.Value.Stages[i].DurationMs == secondRun.Value.Stages[i].DurationMs);
            TEST_EXPECT(firstRun.Value.Stages[i].State == secondRun.Value.Stages[i].State);
        }
    }

    return 0;
}
