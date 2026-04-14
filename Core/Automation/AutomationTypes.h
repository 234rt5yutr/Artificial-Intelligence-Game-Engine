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

enum class PerformanceMetricKind : uint8_t {
    FrameMs = 0,
    CpuMs = 1,
    GpuMs = 2,
    MemoryMb = 3
};

struct PerformanceMetricBudget {
    PerformanceMetricKind Metric = PerformanceMetricKind::FrameMs;
    double Threshold = 0.0;
};

struct PerformanceMetricSample {
    PerformanceMetricKind Metric = PerformanceMetricKind::FrameMs;
    std::vector<double> Samples;
    double Average = 0.0;
    double Peak = 0.0;
};

struct PerformanceMetricResult {
    PerformanceMetricKind Metric = PerformanceMetricKind::FrameMs;
    double Threshold = 0.0;
    double Average = 0.0;
    double Peak = 0.0;
    double AverageDelta = 0.0;
    double PeakDelta = 0.0;
    bool Passed = false;
};

struct PerformanceSuiteRequest {
    std::string ProfileName;
    std::string ScenarioId;
    std::string PlatformTier;
    uint32_t WarmupFrames = 0;
    uint32_t SampleFrames = 0;
    std::vector<PerformanceMetricBudget> Budgets;
};

struct PerformanceSuiteResult {
    std::string ProfileName;
    std::string ScenarioId;
    std::string PlatformTier;
    uint32_t WarmupFrames = 0;
    uint32_t SampleFrames = 0;
    bool Passed = false;
    std::string GateStatusCode;
    std::vector<PerformanceMetricSample> MetricSamples;
    std::vector<PerformanceMetricResult> MetricResults;
    std::vector<PerformanceMetricResult> FailingMetrics;
    std::vector<std::string> Diagnostics;
};

} // namespace Core::Automation
