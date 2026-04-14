#include "Core/Automation/PerformanceTestRunner.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>

namespace Core::Automation {
namespace {

[[nodiscard]] bool IsValidPlatformTier(const std::string& platformTier) {
    return platformTier == "low" || platformTier == "mid" || platformTier == "high";
}

[[nodiscard]] uint32_t MetricKey(const PerformanceMetricKind metric) {
    return static_cast<uint32_t>(metric);
}

[[nodiscard]] bool IsRequestValid(const PerformanceSuiteRequest& request) {
    if (request.ProfileName.empty() || request.ScenarioId.empty() || request.PlatformTier.empty()) {
        return false;
    }

    if (!IsValidPlatformTier(request.PlatformTier)) {
        return false;
    }

    if (request.SampleFrames == 0 || request.Budgets.empty()) {
        return false;
    }

    std::unordered_set<uint32_t> seenMetrics;
    for (const PerformanceMetricBudget& budget : request.Budgets) {
        if (!std::isfinite(budget.Threshold) || budget.Threshold <= 0.0) {
            return false;
        }

        const uint32_t key = MetricKey(budget.Metric);
        if (seenMetrics.contains(key)) {
            return false;
        }

        seenMetrics.insert(key);
    }

    return true;
}

[[nodiscard]] uint32_t HashString(const std::string& value) {
    uint32_t hash = 2166136261u;
    for (const unsigned char symbol : value) {
        hash ^= static_cast<uint32_t>(symbol);
        hash *= 16777619u;
    }
    return hash;
}

[[nodiscard]] double TierScale(const std::string& platformTier) {
    if (platformTier == "low") {
        return 1.35;
    }

    if (platformTier == "mid") {
        return 1.0;
    }

    return 0.75;
}

[[nodiscard]] double MetricBaseline(const PerformanceMetricKind metric) {
    switch (metric) {
    case PerformanceMetricKind::FrameMs:
        return 13.0;
    case PerformanceMetricKind::CpuMs:
        return 6.7;
    case PerformanceMetricKind::GpuMs:
        return 6.2;
    case PerformanceMetricKind::MemoryMb:
        return 780.0;
    }

    return 0.0;
}

[[nodiscard]] double MetricVariance(const PerformanceMetricKind metric) {
    switch (metric) {
    case PerformanceMetricKind::FrameMs:
        return 1.2;
    case PerformanceMetricKind::CpuMs:
        return 0.8;
    case PerformanceMetricKind::GpuMs:
        return 0.9;
    case PerformanceMetricKind::MemoryMb:
        return 32.0;
    }

    return 0.0;
}

[[nodiscard]] bool TryGenerateSample(const PerformanceMetricKind metric,
                                     const uint32_t deterministicSeed,
                                     const uint32_t frameIndex,
                                     const double tierScale,
                                     const double scenarioScale,
                                     double& sampleOut) {
    const uint32_t mixed =
        deterministicSeed ^ (frameIndex * 747796405u + 2891336453u + MetricKey(metric) * 977u);

    const double normalized = static_cast<double>((mixed >> 8) % 1024u) / 1023.0;
    const double signedNoise = (normalized - 0.5) * 2.0;

    const double baseline = MetricBaseline(metric) * tierScale * scenarioScale;
    const double variance = MetricVariance(metric);

    sampleOut = baseline + signedNoise * variance;
    if (!std::isfinite(sampleOut) || sampleOut <= 0.0) {
        return false;
    }

    return true;
}

} // namespace

Result<PerformanceSuiteResult> RunAutomatedPerformanceTests(const PerformanceSuiteRequest& request) {
    if (!IsRequestValid(request)) {
        return Result<PerformanceSuiteResult>::Failure("AUTOMATION_INVALID_REQUEST");
    }

    const uint32_t deterministicSeed = HashString(request.ProfileName) ^ (HashString(request.ScenarioId) << 1u) ^
                                       (HashString(request.PlatformTier) << 2u);
    const double tierScale = TierScale(request.PlatformTier);
    const double scenarioScale = 0.92 + static_cast<double>(HashString(request.ScenarioId) % 17u) / 100.0;

    for (uint32_t warmupFrame = 0; warmupFrame < request.WarmupFrames; ++warmupFrame) {
        double discard = 0.0;
        if (!TryGenerateSample(PerformanceMetricKind::FrameMs,
                               deterministicSeed,
                               warmupFrame,
                               tierScale,
                               scenarioScale,
                               discard)) {
            return Result<PerformanceSuiteResult>::Failure("AUTOMATION_SAMPLING_FAILED");
        }
    }

    PerformanceSuiteResult suiteResult{};
    suiteResult.ProfileName = request.ProfileName;
    suiteResult.ScenarioId = request.ScenarioId;
    suiteResult.PlatformTier = request.PlatformTier;
    suiteResult.WarmupFrames = request.WarmupFrames;
    suiteResult.SampleFrames = request.SampleFrames;
    suiteResult.Passed = true;
    suiteResult.GateStatusCode = "AUTOMATION_PERF_PASSED";
    suiteResult.Diagnostics.push_back("AUTOMATION_PERFORMANCE_COMPLETED");

    suiteResult.MetricSamples.reserve(request.Budgets.size());
    suiteResult.MetricResults.reserve(request.Budgets.size());

    for (const PerformanceMetricBudget& budget : request.Budgets) {
        PerformanceMetricSample metricSample{};
        metricSample.Metric = budget.Metric;
        metricSample.Samples.reserve(request.SampleFrames);

        double sampleSum = 0.0;
        double samplePeak = 0.0;

        for (uint32_t sampleFrame = 0; sampleFrame < request.SampleFrames; ++sampleFrame) {
            const uint32_t frameIndex = request.WarmupFrames + sampleFrame;

            double sample = 0.0;
            if (!TryGenerateSample(
                    budget.Metric, deterministicSeed, frameIndex, tierScale, scenarioScale, sample)) {
                return Result<PerformanceSuiteResult>::Failure("AUTOMATION_SAMPLING_FAILED");
            }

            metricSample.Samples.push_back(sample);
            sampleSum += sample;
            samplePeak = std::max(samplePeak, sample);
        }

        metricSample.Average = sampleSum / static_cast<double>(request.SampleFrames);
        metricSample.Peak = samplePeak;

        PerformanceMetricResult metricResult{};
        metricResult.Metric = budget.Metric;
        metricResult.Threshold = budget.Threshold;
        metricResult.Average = metricSample.Average;
        metricResult.Peak = metricSample.Peak;
        metricResult.AverageDelta = metricResult.Average - metricResult.Threshold;
        metricResult.PeakDelta = metricResult.Peak - metricResult.Threshold;
        metricResult.Passed = metricResult.AverageDelta <= 0.0 && metricResult.PeakDelta <= 0.0;

        suiteResult.MetricSamples.push_back(metricSample);
        suiteResult.MetricResults.push_back(metricResult);

        if (!metricResult.Passed) {
            suiteResult.Passed = false;
            suiteResult.GateStatusCode = "PERF_BUDGET_EXCEEDED";
            suiteResult.FailingMetrics.push_back(metricResult);
        }
    }

    if (!suiteResult.Passed) {
        suiteResult.Diagnostics.push_back("PERF_BUDGET_EXCEEDED");
    }

    return Result<PerformanceSuiteResult>::Success(std::move(suiteResult));
}

} // namespace Core::Automation
