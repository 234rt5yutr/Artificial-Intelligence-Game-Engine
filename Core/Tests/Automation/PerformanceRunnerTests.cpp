#include "Core/Automation/PerformanceTestRunner.h"

#include <cstdlib>
#include <cstdio>

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

Core::Automation::PerformanceSuiteRequest BuildValidRequest() {
    using namespace Core::Automation;

    PerformanceSuiteRequest request{};
    request.ProfileName = "windows-client";
    request.ScenarioId = "baseline-scene";
    request.PlatformTier = "high";
    request.WarmupFrames = 8;
    request.SampleFrames = 16;
    request.Budgets = {
        {PerformanceMetricKind::FrameMs, 16.5},
        {PerformanceMetricKind::CpuMs, 8.0},
        {PerformanceMetricKind::GpuMs, 7.5},
        {PerformanceMetricKind::MemoryMb, 850.0}
    };
    return request;
}

} // namespace

int main() {
    using namespace Core::Automation;

    {
        PerformanceSuiteRequest invalidRequest{};
        const Result<PerformanceSuiteResult> result = RunAutomatedPerformanceTests(invalidRequest);
        TEST_EXPECT(!result.Ok);
        TEST_EXPECT(result.Error == "AUTOMATION_INVALID_REQUEST");
    }

    {
        PerformanceSuiteRequest passRequest = BuildValidRequest();
        const Result<PerformanceSuiteResult> result = RunAutomatedPerformanceTests(passRequest);
        TEST_EXPECT(result.Ok);
        TEST_EXPECT(result.Value.Passed);
        TEST_EXPECT(result.Value.GateStatusCode == "AUTOMATION_PERF_PASSED");
        TEST_EXPECT(result.Value.FailingMetrics.empty());
        TEST_EXPECT(result.Value.MetricResults.size() == passRequest.Budgets.size());
        TEST_EXPECT(result.Value.MetricSamples.size() == passRequest.Budgets.size());
    }

    {
        PerformanceSuiteRequest failRequest = BuildValidRequest();
        failRequest.Budgets = {
            {PerformanceMetricKind::FrameMs, 4.0}
        };

        const Result<PerformanceSuiteResult> result = RunAutomatedPerformanceTests(failRequest);
        TEST_EXPECT(result.Ok);
        TEST_EXPECT(!result.Value.Passed);
        TEST_EXPECT(result.Value.GateStatusCode == "PERF_BUDGET_EXCEEDED");
        TEST_EXPECT(result.Value.FailingMetrics.size() == 1);
        TEST_EXPECT(result.Value.FailingMetrics[0].Metric == PerformanceMetricKind::FrameMs);
        TEST_EXPECT(result.Value.FailingMetrics[0].AverageDelta > 0.0);
    }

    return 0;
}
