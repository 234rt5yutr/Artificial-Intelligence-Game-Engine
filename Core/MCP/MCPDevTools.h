#pragma once

// MCP Development Control Tools
//
// The scene/audio/particle tool families let an agent author *content*. This family
// lets an agent drive the *development loop* itself: freeze and single-step the
// simulation, read engine logs, run the automated play-mode and performance suites,
// and capture/export profiler traces.
//
// Everything here runs on the engine main thread (MCPServer marshals tools/call), so
// it is safe to touch the scene and its system pipeline directly.

#include "MCPTool.h"
#include "Core/ECS/Scene.h"
#include "Core/ECS/SystemPipeline.h"
#include "Core/Automation/PlayModeTestRunner.h"
#include "Core/Automation/PerformanceTestRunner.h"
#include "Core/Diagnostics/CPUProfilerCapture.h"
#include "Core/Diagnostics/GPUProfilerCapture.h"
#include "Core/Diagnostics/TraceExporter.h"
#include "Core/Log.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace Core {
namespace MCP {

    namespace DevToolsDetail {

        inline ECS::SystemPipeline* PipelineFor(ECS::Scene* scene) {
            return scene ? scene->GetSystemPipeline() : nullptr;
        }

        inline Json MetricKindToJson(Automation::PerformanceMetricKind kind) {
            switch (kind) {
                case Automation::PerformanceMetricKind::FrameMs:  return "frameMs";
                case Automation::PerformanceMetricKind::CpuMs:    return "cpuMs";
                case Automation::PerformanceMetricKind::GpuMs:    return "gpuMs";
                case Automation::PerformanceMetricKind::MemoryMb: return "memoryMb";
            }
            return "frameMs";
        }

        inline bool MetricKindFromString(const std::string& text, Automation::PerformanceMetricKind& out) {
            if (text == "frameMs")  { out = Automation::PerformanceMetricKind::FrameMs;  return true; }
            if (text == "cpuMs")    { out = Automation::PerformanceMetricKind::CpuMs;    return true; }
            if (text == "gpuMs")    { out = Automation::PerformanceMetricKind::GpuMs;    return true; }
            if (text == "memoryMb") { out = Automation::PerformanceMetricKind::MemoryMb; return true; }
            return false;
        }

        inline bool ChannelFromString(const std::string& text, Diagnostics::ProfilerMarkerChannel& out) {
            if (text == "render")  { out = Diagnostics::ProfilerMarkerChannel::Render;  return true; }
            if (text == "physics") { out = Diagnostics::ProfilerMarkerChannel::Physics; return true; }
            if (text == "network") { out = Diagnostics::ProfilerMarkerChannel::Network; return true; }
            if (text == "ui")      { out = Diagnostics::ProfilerMarkerChannel::Ui;      return true; }
            if (text == "script")  { out = Diagnostics::ProfilerMarkerChannel::Script;  return true; }
            if (text == "custom")  { out = Diagnostics::ProfilerMarkerChannel::Custom;  return true; }
            return false;
        }

    } // namespace DevToolsDetail

    // ========================================================================
    // GetEngineStatus - what is the engine actually doing right now
    // ========================================================================
    class GetEngineStatusTool : public MCPTool {
    public:
        GetEngineStatusTool()
            : MCPTool("GetEngineStatus",
                      "Report live engine runtime state: which simulation systems are active, "
                      "frame count and last frame cost, pause/time-scale state, entity count, "
                      "and whether physics/input/animation are available. Use this first to "
                      "understand the running engine before changing anything.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Properties = Json::object();
            return schema;
        }

        ToolResult Execute(const Json&, ECS::Scene* scene) override {
            if (!scene) {
                return ToolResult::Error("No active scene bound to the MCP server");
            }

            Json status;
            status["sceneName"] = scene->GetName();
            status["entityCount"] = scene->GetEntityCount();

            auto* pipeline = DevToolsDetail::PipelineFor(scene);
            if (!pipeline) {
                status["systemPipeline"] = nullptr;
                status["note"] = "Simulation systems are not initialized for this scene";
                return ToolResult::SuccessJson(status);
            }

            Json pipelineJson;
            pipelineJson["initialized"] = pipeline->IsInitialized();
            pipelineJson["frameCount"] = pipeline->GetFrameCount();
            pipelineJson["lastUpdateMs"] = pipeline->GetLastUpdateSeconds() * 1000.0f;
            pipelineJson["paused"] = pipeline->IsPaused();
            pipelineJson["pendingStepFrames"] = pipeline->GetPendingStepFrames();
            pipelineJson["timeScale"] = pipeline->GetTimeScale();
            pipelineJson["fixedFrameDelta"] = pipeline->GetFixedFrameDelta();

            Json systems;
            systems["physics"] = pipeline->GetPhysicsWorld() != nullptr;
            systems["input"] = pipeline->GetInputMapper() != nullptr;
            systems["animation"] = pipeline->GetAnimatorSystem() != nullptr;
            systems["cameras"] = pipeline->GetCameraSystem() != nullptr;
            systems["lighting"] = pipeline->GetLightSystem() != nullptr;
            systems["renderCollection"] = pipeline->GetRenderSystem() != nullptr;
            pipelineJson["systems"] = systems;

            const auto& config = pipeline->GetConfig();
            Json configJson;
            configJson["physicsFixedTimestep"] = config.PhysicsFixedTimestep;
            configJson["physicsCollisionSteps"] = config.PhysicsCollisionSteps;
            configJson["maxFrameDeltaSeconds"] = config.MaxFrameDeltaSeconds;
            configJson["screenWidth"] = config.ScreenWidth;
            configJson["screenHeight"] = config.ScreenHeight;
            pipelineJson["config"] = configJson;

            status["systemPipeline"] = pipelineJson;
            return ToolResult::SuccessJson(status);
        }

        ToolAnnotations GetAnnotations() const override {
            ToolAnnotations a;
            a.Title = "Get engine status";
            a.ReadOnlyHint = true;
            a.IdempotentHint = true;
            return a;
        }

        std::vector<std::string> RequiredCapabilities() const override { return {"engine.read"}; }
    };

    // ========================================================================
    // ControlSimulation - freeze / step / slow down the running world
    // ========================================================================
    class ControlSimulationTool : public MCPTool {
    public:
        ControlSimulationTool()
            : MCPTool("ControlSimulation",
                      "Control the running simulation: pause, resume, advance an exact number of "
                      "frames while paused, set a time scale for slow motion, or pin a fixed frame "
                      "delta for reproducible runs. Pausing then stepping is the way to inspect a "
                      "specific frame; rendering and UI keep running so the frozen frame stays visible.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Properties = {
                {"action", {
                    {"type", "string"},
                    {"enum", Json::array({"pause", "resume", "step", "status"})},
                    {"description", "pause freezes simulation; resume unfreezes; step advances "
                                    "'frames' frames while staying paused; status only reports."}
                }},
                {"frames", {
                    {"type", "integer"},
                    {"minimum", 1},
                    {"maximum", 10000},
                    {"description", "Frames to advance when action is 'step' (default 1)"}
                }},
                {"timeScale", {
                    {"type", "number"},
                    {"minimum", 0.0},
                    {"maximum", 100.0},
                    {"description", "Optional simulation speed multiplier. 1.0 is realtime, 0.1 is slow motion."}
                }},
                {"fixedFrameDelta", {
                    {"type", "number"},
                    {"minimum", 0.0},
                    {"maximum", 1.0},
                    {"description", "Optional constant delta in seconds for deterministic runs. 0 restores wall-clock timing."}
                }}
            };
            schema.Required = {"action"};
            return schema;
        }

        bool ValidateArguments(const Json& args, std::string& errorMessage) const override {
            if (!args.contains("action") || !args["action"].is_string()) {
                errorMessage = "'action' is required and must be a string";
                return false;
            }
            const std::string action = args["action"].get<std::string>();
            if (action != "pause" && action != "resume" && action != "step" && action != "status") {
                errorMessage = "'action' must be one of: pause, resume, step, status";
                return false;
            }
            return true;
        }

        ToolResult Execute(const Json& args, ECS::Scene* scene) override {
            auto* pipeline = DevToolsDetail::PipelineFor(scene);
            if (!pipeline) {
                return ToolResult::Error("Scene has no system pipeline; simulation control unavailable");
            }

            const std::string action = args["action"].get<std::string>();

            if (args.contains("timeScale") && args["timeScale"].is_number()) {
                pipeline->SetTimeScale(args["timeScale"].get<float>());
            }
            if (args.contains("fixedFrameDelta") && args["fixedFrameDelta"].is_number()) {
                pipeline->SetFixedFrameDelta(args["fixedFrameDelta"].get<float>());
            }

            if (action == "pause") {
                pipeline->SetPaused(true);
            } else if (action == "resume") {
                pipeline->SetPaused(false);
            } else if (action == "step") {
                const uint32_t frames = args.contains("frames") && args["frames"].is_number_integer()
                    ? static_cast<uint32_t>(std::clamp<int64_t>(args["frames"].get<int64_t>(), 1, 10000))
                    : 1u;
                // Stepping implies paused: otherwise the world keeps running and the
                // requested frames are meaningless.
                pipeline->SetPaused(true);
                pipeline->RequestStepFrames(frames);
            }

            Json result;
            result["action"] = action;
            result["paused"] = pipeline->IsPaused();
            result["pendingStepFrames"] = pipeline->GetPendingStepFrames();
            result["timeScale"] = pipeline->GetTimeScale();
            result["fixedFrameDelta"] = pipeline->GetFixedFrameDelta();
            result["frameCount"] = pipeline->GetFrameCount();
            return ToolResult::SuccessJson(result);
        }

        ToolAnnotations GetAnnotations() const override {
            ToolAnnotations a;
            a.Title = "Control simulation";
            // Changes how the world advances, but destroys no authored data.
            a.ReadOnlyHint = false;
            a.DestructiveHint = false;
            return a;
        }

        std::vector<std::string> RequiredCapabilities() const override { return {"engine.control"}; }
    };

    // ========================================================================
    // GetEngineLog - read recent engine output
    // ========================================================================
    class GetEngineLogTool : public MCPTool {
    public:
        GetEngineLogTool()
            : MCPTool("GetEngineLog",
                      "Read the most recent engine log lines from the in-memory ring buffer. "
                      "Use this to diagnose what the engine reported during a step, a failed "
                      "tool call, or a test run, without access to the process stdout.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Properties = {
                {"lines", {
                    {"type", "integer"},
                    {"minimum", 1},
                    {"maximum", 2048},
                    {"description", "How many of the most recent lines to return (default 100)"}
                }},
                {"contains", {
                    {"type", "string"},
                    {"description", "Optional case-sensitive substring filter applied to each line"}
                }}
            };
            return schema;
        }

        bool RequiresScene() const override { return false; }

        ToolResult Execute(const Json& args, ECS::Scene*) override {
            size_t lineCount = 100;
            if (args.contains("lines") && args["lines"].is_number_integer()) {
                lineCount = static_cast<size_t>(
                    std::clamp<int64_t>(args["lines"].get<int64_t>(), 1, 2048));
            }

            auto messages = Engine::Log::GetRecentMessages(lineCount);

            if (args.contains("contains") && args["contains"].is_string()) {
                const std::string needle = args["contains"].get<std::string>();
                if (!needle.empty()) {
                    messages.erase(
                        std::remove_if(messages.begin(), messages.end(),
                                       [&needle](const std::string& line) {
                                           return line.find(needle) == std::string::npos;
                                       }),
                        messages.end());
                }
            }

            Json result;
            result["lineCount"] = messages.size();
            result["bufferCapacity"] = Engine::Log::GetRecentMessageCapacity();
            result["lines"] = messages;
            return ToolResult::SuccessJson(result);
        }

        ToolAnnotations GetAnnotations() const override {
            ToolAnnotations a;
            a.Title = "Read engine log";
            a.ReadOnlyHint = true;
            a.IdempotentHint = true;
            return a;
        }

        std::vector<std::string> RequiredCapabilities() const override { return {"engine.read"}; }
    };

    // ========================================================================
    // RunPlayModeTests - deterministic automated play-mode suite
    // ========================================================================
    class RunPlayModeTestsTool : public MCPTool {
    public:
        RunPlayModeTestsTool()
            : MCPTool("RunPlayModeTests",
                      "Run the automated play-mode test suite with a deterministic seed and fixed "
                      "delta, and return per-case and per-assertion results. This is the engine's "
                      "regression gate: run it after changing gameplay, physics, or scene content.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Properties = {
                {"suiteName", {{"type", "string"}, {"description", "Name of the suite to run"}}},
                {"scenePath", {{"type", "string"}, {"description", "Scene the suite runs against"}}},
                {"deterministicSeed", {{"type", "integer"}, {"minimum", 0},
                    {"description", "Seed for reproducible runs (default 1)"}}},
                {"fixedDeltaTime", {{"type", "number"}, {"minimum", 0.0},
                    {"description", "Fixed simulation delta in seconds (default 0.016667)"}}},
                {"maxFrames", {{"type", "integer"}, {"minimum", 1},
                    {"description", "Frame budget for the run (default 120)"}}},
                {"assertions", {{"type", "array"}, {"items", {{"type", "string"}}},
                    {"description", "Assertion identifiers the suite should evaluate"}}}
            };
            schema.Required = {"suiteName", "scenePath"};
            return schema;
        }

        bool RequiresScene() const override { return false; }

        bool ValidateArguments(const Json& args, std::string& errorMessage) const override {
            if (!args.contains("suiteName") || !args["suiteName"].is_string() ||
                args["suiteName"].get<std::string>().empty()) {
                errorMessage = "'suiteName' is required and must be a non-empty string";
                return false;
            }
            if (!args.contains("scenePath") || !args["scenePath"].is_string() ||
                args["scenePath"].get<std::string>().empty()) {
                errorMessage = "'scenePath' is required and must be a non-empty string";
                return false;
            }
            return true;
        }

        ToolResult Execute(const Json& args, ECS::Scene*) override {
            Automation::PlayModeSuiteRequest request;
            request.SuiteName = args["suiteName"].get<std::string>();
            request.ScenePath = args["scenePath"].get<std::string>();
            request.DeterministicSeed = JsonUtils::GetOr<uint32_t>(args, "deterministicSeed", 1u);
            request.FixedDeltaTime = JsonUtils::GetOr<float>(args, "fixedDeltaTime", 1.0f / 60.0f);
            request.MaxFrames = JsonUtils::GetOr<uint32_t>(args, "maxFrames", 120u);

            if (args.contains("assertions") && args["assertions"].is_array()) {
                for (const auto& assertion : args["assertions"]) {
                    if (assertion.is_string()) {
                        request.Assertions.push_back(assertion.get<std::string>());
                    }
                }
            }

            auto outcome = Automation::RunAutomatedPlayModeTests(request);
            if (!outcome.Ok) {
                return ToolResult::Error("Play-mode suite failed to run: " + outcome.Error);
            }

            const auto& suite = outcome.Value;
            Json result;
            result["suiteName"] = suite.SuiteName;
            result["scenePath"] = suite.ScenePath;
            result["passed"] = suite.Passed;
            result["deterministicSeed"] = suite.DeterministicSeed;
            result["fixedDeltaTime"] = suite.FixedDeltaTime;
            result["maxFrames"] = suite.MaxFrames;
            result["simulatedFrames"] = suite.SimulatedFrames;
            result["diagnostics"] = suite.Diagnostics;

            Json cases = Json::array();
            for (const auto& caseResult : suite.CaseResults) {
                Json caseJson;
                caseJson["caseName"] = caseResult.CaseName;
                caseJson["passed"] = caseResult.Passed;
                caseJson["simulatedFrames"] = caseResult.SimulatedFrames;
                caseJson["diagnostics"] = caseResult.Diagnostics;

                Json assertions = Json::array();
                for (const auto& assertion : caseResult.AssertionResults) {
                    assertions.push_back({
                        {"assertionName", assertion.AssertionName},
                        {"passed", assertion.Passed},
                        {"diagnostic", assertion.Diagnostic}
                    });
                }
                caseJson["assertions"] = assertions;
                cases.push_back(caseJson);
            }
            result["cases"] = cases;

            ToolResult toolResult = ToolResult::SuccessJson(result);
            // Surface a failing suite as an error result so the agent does not read
            // "the call worked" as "the tests passed".
            toolResult.IsError = !suite.Passed;
            return toolResult;
        }

        ToolAnnotations GetAnnotations() const override {
            ToolAnnotations a;
            a.Title = "Run play-mode tests";
            // Simulates a scene; does not mutate authored content.
            a.ReadOnlyHint = true;
            return a;
        }

        std::vector<std::string> RequiredCapabilities() const override { return {"engine.test"}; }
    };

    // ========================================================================
    // RunPerformanceTests - budget gate
    // ========================================================================
    class RunPerformanceTestsTool : public MCPTool {
    public:
        RunPerformanceTestsTool()
            : MCPTool("RunPerformanceTests",
                      "Run the automated performance suite against frame/CPU/GPU/memory budgets "
                      "and report which metrics passed or regressed. Run this after rendering or "
                      "simulation changes to catch performance regressions before they ship.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Properties = {
                {"profileName", {{"type", "string"}, {"description", "Performance profile name"}}},
                {"scenarioId", {{"type", "string"}, {"description", "Scenario identifier to measure"}}},
                {"platformTier", {{"type", "string"},
                    {"description", "Target hardware tier, e.g. 'low', 'mid', 'high'"}}},
                {"warmupFrames", {{"type", "integer"}, {"minimum", 0},
                    {"description", "Frames to discard before sampling (default 30)"}}},
                {"sampleFrames", {{"type", "integer"}, {"minimum", 1},
                    {"description", "Frames to sample (default 120)"}}},
                {"budgets", {
                    {"type", "array"},
                    {"description", "Budget per metric; a metric exceeding its threshold fails the gate"},
                    {"items", {
                        {"type", "object"},
                        {"properties", {
                            {"metric", {{"type", "string"},
                                {"enum", Json::array({"frameMs", "cpuMs", "gpuMs", "memoryMb"})}}},
                            {"threshold", {{"type", "number"}}}
                        }},
                        {"required", Json::array({"metric", "threshold"})}
                    }}
                }}
            };
            schema.Required = {"profileName", "scenarioId"};
            return schema;
        }

        bool RequiresScene() const override { return false; }

        bool ValidateArguments(const Json& args, std::string& errorMessage) const override {
            if (!args.contains("profileName") || !args["profileName"].is_string() ||
                args["profileName"].get<std::string>().empty()) {
                errorMessage = "'profileName' is required and must be a non-empty string";
                return false;
            }
            if (!args.contains("scenarioId") || !args["scenarioId"].is_string() ||
                args["scenarioId"].get<std::string>().empty()) {
                errorMessage = "'scenarioId' is required and must be a non-empty string";
                return false;
            }
            if (args.contains("budgets")) {
                if (!args["budgets"].is_array()) {
                    errorMessage = "'budgets' must be an array";
                    return false;
                }
                for (const auto& budget : args["budgets"]) {
                    Automation::PerformanceMetricKind kind{};
                    if (!budget.is_object() || !budget.contains("metric") || !budget["metric"].is_string() ||
                        !DevToolsDetail::MetricKindFromString(budget["metric"].get<std::string>(), kind)) {
                        errorMessage = "each budget needs a 'metric' of frameMs|cpuMs|gpuMs|memoryMb";
                        return false;
                    }
                    if (!budget.contains("threshold") || !budget["threshold"].is_number()) {
                        errorMessage = "each budget needs a numeric 'threshold'";
                        return false;
                    }
                }
            }
            return true;
        }

        ToolResult Execute(const Json& args, ECS::Scene*) override {
            Automation::PerformanceSuiteRequest request;
            request.ProfileName = args["profileName"].get<std::string>();
            request.ScenarioId = args["scenarioId"].get<std::string>();
            request.PlatformTier = JsonUtils::GetOr<std::string>(args, "platformTier", "mid");
            request.WarmupFrames = JsonUtils::GetOr<uint32_t>(args, "warmupFrames", 30u);
            request.SampleFrames = JsonUtils::GetOr<uint32_t>(args, "sampleFrames", 120u);

            if (args.contains("budgets") && args["budgets"].is_array()) {
                for (const auto& budget : args["budgets"]) {
                    Automation::PerformanceMetricBudget entry;
                    DevToolsDetail::MetricKindFromString(budget["metric"].get<std::string>(), entry.Metric);
                    entry.Threshold = budget["threshold"].get<double>();
                    request.Budgets.push_back(entry);
                }
            }

            auto outcome = Automation::RunAutomatedPerformanceTests(request);
            if (!outcome.Ok) {
                return ToolResult::Error("Performance suite failed to run: " + outcome.Error);
            }

            const auto& suite = outcome.Value;
            Json result;
            result["profileName"] = suite.ProfileName;
            result["scenarioId"] = suite.ScenarioId;
            result["platformTier"] = suite.PlatformTier;
            result["passed"] = suite.Passed;
            result["gateStatusCode"] = suite.GateStatusCode;
            result["warmupFrames"] = suite.WarmupFrames;
            result["sampleFrames"] = suite.SampleFrames;
            result["diagnostics"] = suite.Diagnostics;

            auto metricsToJson = [](const std::vector<Automation::PerformanceMetricResult>& metrics) {
                Json array = Json::array();
                for (const auto& metric : metrics) {
                    array.push_back({
                        {"metric", DevToolsDetail::MetricKindToJson(metric.Metric)},
                        {"threshold", metric.Threshold},
                        {"average", metric.Average},
                        {"peak", metric.Peak},
                        {"averageDelta", metric.AverageDelta},
                        {"peakDelta", metric.PeakDelta},
                        {"passed", metric.Passed}
                    });
                }
                return array;
            };

            result["metrics"] = metricsToJson(suite.MetricResults);
            result["failingMetrics"] = metricsToJson(suite.FailingMetrics);

            ToolResult toolResult = ToolResult::SuccessJson(result);
            toolResult.IsError = !suite.Passed;
            return toolResult;
        }

        ToolAnnotations GetAnnotations() const override {
            ToolAnnotations a;
            a.Title = "Run performance tests";
            a.ReadOnlyHint = true;
            return a;
        }

        std::vector<std::string> RequiredCapabilities() const override { return {"engine.test"}; }
    };

    // ========================================================================
    // CaptureProfilerTrace - capture and export in one step
    // ========================================================================
    class CaptureProfilerTraceTool : public MCPTool {
    public:
        CaptureProfilerTraceTool()
            : MCPTool("CaptureProfilerTrace",
                      "Capture a CPU (and optionally GPU) profiler session and export it to a trace "
                      "file on disk, returning the artifact paths and checksum. Use 'chrome' format "
                      "to open the result in a trace viewer when investigating a frame-time spike.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Properties = {
                {"profileName", {{"type", "string"},
                    {"description", "Label for the capture session (default 'mcp-capture')"}}},
                {"durationMs", {{"type", "integer"}, {"minimum", 1}, {"maximum", 60000},
                    {"description", "Capture window in milliseconds (default 1000)"}}},
                {"channels", {
                    {"type", "array"},
                    {"items", {{"type", "string"},
                        {"enum", Json::array({"render", "physics", "network", "ui", "script", "custom"})}}},
                    {"description", "Marker channels to record; defaults to all channels"}
                }},
                {"includeGpu", {{"type", "boolean"},
                    {"description", "Also capture GPU pass timings (requires an active Vulkan context)"}}},
                {"format", {{"type", "string"}, {"enum", Json::array({"json", "chrome"})},
                    {"description", "Export format (default 'json')"}}},
                {"outputPath", {{"type", "string"},
                    {"description", "Directory for the exported artifacts (default 'build/diagnostics')"}}}
            };
            return schema;
        }

        bool RequiresScene() const override { return false; }

        ToolResult Execute(const Json& args, ECS::Scene*) override {
            Diagnostics::ProfilerCaptureRequest request;
            request.ProfileName = JsonUtils::GetOr<std::string>(args, "profileName", "mcp-capture");
            request.DurationMs = JsonUtils::GetOr<uint32_t>(args, "durationMs", 1000u);
            request.IncludeCpu = true;
            request.IncludeGpu = JsonUtils::GetOr<bool>(args, "includeGpu", false);

            if (args.contains("channels") && args["channels"].is_array() && !args["channels"].empty()) {
                for (const auto& channel : args["channels"]) {
                    Diagnostics::ProfilerMarkerChannel parsed{};
                    if (channel.is_string() &&
                        DevToolsDetail::ChannelFromString(channel.get<std::string>(), parsed)) {
                        request.Channels.push_back(parsed);
                    }
                }
            }
            if (request.Channels.empty()) {
                for (auto channel : Diagnostics::GetAllProfilerMarkerChannels()) {
                    request.Channels.push_back(channel);
                }
            }

            auto capture = request.IncludeGpu
                ? Diagnostics::StartGPUProfilerCapture(request)
                : Diagnostics::StartCPUProfilerCapture(request);
            if (!capture.Ok) {
                return ToolResult::Error("Profiler capture failed: " + capture.Error);
            }

            Diagnostics::TraceExportRequest exportRequest;
            exportRequest.SessionId = capture.Value.SessionId;
            exportRequest.Format =
                JsonUtils::GetOr<std::string>(args, "format", "json") == "chrome"
                    ? Diagnostics::TraceExportFormat::ChromeTrace
                    : Diagnostics::TraceExportFormat::Json;
            exportRequest.OutputPath = std::filesystem::path(
                JsonUtils::GetOr<std::string>(args, "outputPath", "build/diagnostics"));

            auto exported = Diagnostics::ExportProfilerTrace(exportRequest);
            if (!exported.Ok) {
                return ToolResult::Error("Trace export failed: " + exported.Error);
            }

            Json result;
            result["sessionId"] = capture.Value.SessionId;
            result["profileName"] = capture.Value.ProfileName;
            result["captureType"] = capture.Value.CaptureType;
            result["durationMs"] = capture.Value.DurationMs;
            result["completed"] = capture.Value.Completed;
            result["traceArtifactPath"] = exported.Value.TraceArtifactPath.string();
            result["manifestArtifactPath"] = exported.Value.ManifestArtifactPath.string();
            result["checksum"] = exported.Value.Checksum;
            result["traceByteSize"] = exported.Value.TraceByteSize;
            return ToolResult::SuccessJson(result);
        }

        ToolAnnotations GetAnnotations() const override {
            ToolAnnotations a;
            a.Title = "Capture profiler trace";
            // Writes trace artifacts to disk, so not read-only.
            a.OpenWorldHint = true;
            return a;
        }

        std::vector<std::string> RequiredCapabilities() const override { return {"engine.profile"}; }
    };

    // ========================================================================
    // Factory
    // ========================================================================
    inline std::vector<MCPToolPtr> CreateDevTools() {
        std::vector<MCPToolPtr> tools;
        tools.push_back(std::make_shared<GetEngineStatusTool>());
        tools.push_back(std::make_shared<ControlSimulationTool>());
        tools.push_back(std::make_shared<GetEngineLogTool>());
        tools.push_back(std::make_shared<RunPlayModeTestsTool>());
        tools.push_back(std::make_shared<RunPerformanceTestsTool>());
        tools.push_back(std::make_shared<CaptureProfilerTraceTool>());
        return tools;
    }

} // namespace MCP
} // namespace Core
