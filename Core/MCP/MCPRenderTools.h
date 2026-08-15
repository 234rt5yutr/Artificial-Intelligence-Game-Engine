#pragma once

// MCP Rendering & Material Tools
//
// The existing families let an agent author content and drive the development
// loop. None of them could see or touch the renderer, which meant the whole
// GPU-driven / GI / upscaling stack was invisible to the tooling that is
// supposed to be this engine's differentiator.
//
// Everything here runs on the simulation thread: MCPServer marshals tools/call
// into the frame's idle window, after the render thread has been drained, so it
// is safe to mutate renderer state directly.

#include "MCPTool.h"
#include "MCPAssetTools.h"
#include "Core/Application.h"
#include "Core/ECS/Scene.h"
#include "Core/ECS/SystemPipeline.h"
#include "Core/Editor/EditorModule.h"
#include "Core/Log.h"
#include "Core/RHI/Vulkan/VulkanContext.h"
#include "Core/Renderer/Material/MaterialGraph.h"
#include "Core/Renderer/RenderThread.h"
#include "Core/Renderer/SceneRenderer.h"
#include "Core/UI/UIManager.h"

#include <algorithm>
#include <string>
#include <vector>

namespace Core {
namespace MCP {

    namespace RenderToolsDetail {

        inline Renderer::SceneRenderer* SceneRendererOrNull() {
            Application* application = Application::TryGet();
            if (!application) {
                return nullptr;
            }
            RHI::VulkanContext* context = application->GetVulkanContext();
            return context ? context->GetSceneRenderer() : nullptr;
        }

        inline Editor::ViewportPanel* ViewportPanelOrNull() {
            auto* editorModule = UI::UIManager::Get().GetEditorModule();
            return editorModule ? &editorModule->GetViewportPanel() : nullptr;
        }

        inline Json Vec3ToJson(const Math::Vec3& v) {
            return Json::array({v.x, v.y, v.z});
        }

        inline bool ReadVec3(const Json& arguments, const char* key, Math::Vec3& out) {
            if (!arguments.contains(key) || !arguments[key].is_array() || arguments[key].size() != 3) {
                return false;
            }
            out = Math::Vec3(arguments[key][0].get<float>(),
                             arguments[key][1].get<float>(),
                             arguments[key][2].get<float>());
            return true;
        }

        inline Json SchemaProperty(const char* type, const char* description) {
            return Json{{"type", type}, {"description", description}};
        }

        inline Json NumberProperty(const char* description, double minimum, double maximum) {
            return Json{{"type", "number"}, {"description", description},
                        {"minimum", minimum}, {"maximum", maximum}};
        }

    } // namespace RenderToolsDetail

    // ========================================================================
    // GetRenderStats - what the GPU actually did last frame
    // ========================================================================
    class GetRenderStatsTool : public MCPTool {
    public:
        GetRenderStatsTool()
            : MCPTool("GetRenderStats",
                      "Report the live rendering pipeline: render vs display resolution, "
                      "GPU-driven cluster culling counters (frustum, backface cone, HZB "
                      "occlusion, two-phase), global illumination state, FSR upscaling mode, "
                      "material pipeline count, and render-thread timings. Call this before "
                      "changing any rendering setting.") {}

        ToolAnnotations GetAnnotations() const override {
            ToolAnnotations annotations;
            annotations.Title = "Get Render Stats";
            annotations.ReadOnlyHint = true;
            annotations.IdempotentHint = true;
            return annotations;
        }

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Properties = Json::object();
            return schema;
        }

        bool RequiresScene() const override { return false; }

        ToolResult Execute(const Json&, ECS::Scene*) override {
            auto* renderer = RenderToolsDetail::SceneRendererOrNull();
            if (!renderer) {
                return ToolResult::Error(
                    "No scene renderer is active. The engine is running headless, or the "
                    "Vulkan context failed to initialize.");
            }

            const auto& stats = renderer->GetStats();
            const auto& cull = renderer->GetCuller().GetStats();
            const auto& gi = renderer->GetGlobalIllumination().GetStats();
            const auto& giSettings = renderer->GetGlobalIllumination().GetSettings();
            const auto& fsr = renderer->GetUpscaler().GetStats();
            const auto& fsrSettings = renderer->GetUpscaler().GetSettings();
            const auto& scene = renderer->GetGPUScene().GetStats();

            Json report;
            Json resolution;
            resolution["renderWidth"] = stats.RenderWidth;
            resolution["renderHeight"] = stats.RenderHeight;
            resolution["displayWidth"] = stats.DisplayWidth;
            resolution["displayHeight"] = stats.DisplayHeight;
            report["resolution"] = resolution;

            Json draws;
            draws["indirectBatches"] = stats.IndirectDraws;
            draws["directDraws"] = stats.DirectDraws;
            draws["skippedDraws"] = stats.SkippedDraws;
            draws["gpuDrivenActive"] = stats.GPUDrivenActive;
            draws["materialPipelines"] = stats.MaterialPipelines;
            draws["postProcessActive"] = stats.PostProcessActive;
            report["draws"] = draws;

            Json skinning;
            skinning["instances"] = stats.SkinnedInstances;
            skinning["vertices"] = stats.SkinnedVertices;
            skinning["dropped"] = stats.SkinnedDropped;
            report["skinning"] = skinning;

            Json temporal;
            temporal["taaEnabled"] = stats.TAAEnabled;
            temporal["taaActive"] = stats.TAAActive;
            temporal["taaFeedback"] = stats.TAAFeedback;
            report["temporal"] = temporal;

            Json culling;
            culling["clusterSlots"] = cull.ClusterSlots;
            culling["visibleEarly"] = cull.VisibleEarly;
            culling["visibleLate"] = cull.VisibleLate;
            culling["frustumCulled"] = cull.FrustumCulled;
            culling["coneCulled"] = cull.ConeCulled;
            culling["occlusionCulled"] = cull.OcclusionCulled;
            culling["hzbMipCount"] = cull.HZBMipCount;
            culling["occlusionEnabled"] = cull.OcclusionEnabled;
            culling["coneCullingEnabled"] = cull.ConeCullingEnabled;
            culling["twoPhaseEnabled"] = cull.TwoPhaseEnabled;
            report["gpuCulling"] = culling;

            Json gpuScene;
            gpuScene["residentMeshes"] = scene.ResidentMeshes;
            gpuScene["residentClusters"] = scene.ResidentClusters;
            gpuScene["residentTriangles"] = scene.ResidentTriangles;
            gpuScene["frameInstances"] = scene.FrameInstances;
            gpuScene["materialBatches"] = scene.FrameMaterialBatches;
            gpuScene["rejectedMeshes"] = scene.RejectedMeshes;
            gpuScene["skinnedInstances"] = scene.SkinnedInstances;
            gpuScene["skinnedVerticesUsed"] = scene.SkinnedVerticesUsed;
            gpuScene["skinnedVerticesCapacity"] = scene.SkinnedVerticesCapacity;
            gpuScene["vertexBytesUsed"] = scene.VertexBytesUsed;
            gpuScene["vertexBytesCapacity"] = scene.VertexBytesCapacity;
            report["gpuScene"] = gpuScene;

            Json illumination;
            illumination["enabled"] = giSettings.Enabled;
            illumination["ready"] = gi.Ready;
            illumination["width"] = gi.Width;
            illumination["height"] = gi.Height;
            illumination["probeCount"] = gi.ProbeCount;
            illumination["framesAccumulated"] = gi.FramesAccumulated;
            illumination["raysPerPixel"] = giSettings.RaysPerPixel;
            illumination["stepsPerRay"] = giSettings.StepsPerRay;
            illumination["intensity"] = giSettings.Intensity;
            illumination["temporalAlpha"] = giSettings.TemporalAlpha;
            illumination["probeCacheEnabled"] = giSettings.ProbeCacheEnabled;
            report["globalIllumination"] = illumination;

            Json upscaling;
            upscaling["enabled"] = fsrSettings.Enabled;
            upscaling["quality"] = Renderer::FSRQualityModeName(fsrSettings.Quality);
            upscaling["sharpness"] = fsrSettings.Sharpness;
            upscaling["jitterEnabled"] = fsrSettings.JitterEnabled;
            upscaling["active"] = fsr.Active;
            upscaling["renderScale"] = fsr.RenderScale;
            report["upscaling"] = upscaling;

            Json lights;
            lights["directional"] = stats.DirectionalLights;
            lights["point"] = stats.PointLights;
            lights["spot"] = stats.SpotLights;
            lights["punctualUploaded"] = stats.PunctualLights;
            report["lights"] = lights;

            const auto& grid = renderer->GetLightCuller().GetStats();
            Json clustered;
            clustered["active"] = grid.Active;
            clustered["gridX"] = grid.GridX;
            clustered["gridY"] = grid.GridY;
            clustered["gridZ"] = grid.GridZ;
            clustered["clusterCount"] = grid.ClusterCount;
            clustered["lightCount"] = grid.LightCount;
            // Total light-to-froxel assignments last frame, and the worst single
            // froxel. A max at the 256 ceiling means lights are being dropped.
            clustered["visibleAssignments"] = grid.VisibleAssignments;
            clustered["maxLightsInCluster"] = grid.MaxLightsInCluster;
            clustered["overflowedClusters"] = grid.OverflowedClusters;
            report["clusteredLighting"] = clustered;

            const auto& shadows = renderer->GetShadowRenderer().GetStats();
            const auto& shadowSettings = renderer->GetShadowRenderer().GetSettings();
            Json shadowJson;
            shadowJson["enabled"] = shadowSettings.Enabled;
            shadowJson["active"] = shadows.Active;
            // -1 means no directional light in the scene is casting, which is
            // the usual reason a scene renders unshadowed.
            shadowJson["shadowLightIndex"] = shadows.ShadowLightIndex;
            shadowJson["cascadeCount"] = shadows.CascadeCount;
            shadowJson["resolution"] = shadows.Resolution;
            shadowJson["clusterSlots"] = shadows.ClusterSlots;
            // Spot lights with an atlas tile this frame. Lights past the tile
            // cap are still lit, just unshadowed.
            shadowJson["spotShadowCount"] = shadows.SpotShadowCount;
            shadowJson["spotShadowsEnabled"] = shadowSettings.SpotShadowsEnabled;
            // Point lights take six tiles each; a light that cannot fit a whole
            // cube is skipped rather than partially allocated.
            shadowJson["pointShadowCount"] = shadows.PointShadowCount;
            shadowJson["pointShadowsEnabled"] = shadowSettings.PointShadowsEnabled;
            shadowJson["atlasTilesUsed"] = shadows.AtlasTilesUsed;
            shadowJson["atlasTilesTotal"] = shadows.AtlasTilesTotal;
            // Cascade page cache. dirtyPages 0 with cascadesSkipped equal to the
            // cascade count means a fully static frame cost nothing.
            Json cache;
            cache["enabled"] = shadowSettings.CacheCascades;
            cache["pagesPerSide"] = shadows.PagesPerSide;
            cache["dirtyPages"] = shadows.DirtyPages;
            cache["totalPages"] = shadows.TotalPages;
            cache["cascadesRedrawn"] = shadows.CascadesRedrawn;
            cache["cascadesSkipped"] = shadows.CascadesSkipped;
            cache["dirtyRects"] = shadows.DirtyRects;
            cache["movedOccluders"] = shadows.MovedInstances;
            // Monotonic totals; take two readings and diff them to see whether
            // the cache is actually holding.
            cache["totalCascadeRedraws"] = shadows.TotalCascadeRedraws;
            cache["totalCascadeSkips"] = shadows.TotalCascadeSkips;
            cache["totalOccluderChanges"] = shadows.TotalOccluderChanges;
            shadowJson["pageCache"] = cache;
            shadowJson["atlasResolution"] = shadows.AtlasResolution;
            shadowJson["atlasTileSize"] = shadows.AtlasTileSize;
            shadowJson["atlasTilesPerRow"] = shadowSettings.AtlasTilesPerRow;
            shadowJson["maxShadowDistance"] = shadowSettings.MaxShadowDistance;
            shadowJson["splitLambda"] = shadowSettings.CascadeSplitLambda;
            shadowJson["depthBias"] = shadowSettings.DepthBias;
            shadowJson["slopeBias"] = shadowSettings.SlopeBias;
            shadowJson["normalBias"] = shadowSettings.NormalBias;
            shadowJson["pcfRadius"] = shadowSettings.PcfRadius;
            shadowJson["stabilizeCascades"] = shadowSettings.StabilizeCascades;
            Json splits = Json::array();
            for (uint32_t i = 0; i < shadows.CascadeCount && i < Renderer::kMaxShadowCascades; ++i) {
                splits.push_back(shadows.CascadeSplits[i]);
            }
            shadowJson["cascadeSplits"] = splits;
            report["shadows"] = shadowJson;

            if (auto* application = Application::TryGet()) {
                if (auto* renderThread = application->GetRenderThread()) {
                    const auto threadStats = renderThread->GetStats();
                    Json thread;
                    thread["running"] = threadStats.Running;
                    thread["framesSubmitted"] = threadStats.FramesSubmitted;
                    thread["framesRendered"] = threadStats.FramesRendered;
                    thread["lastRenderMs"] = threadStats.LastRenderMs;
                    // How long the simulation had to wait for the renderer. Near
                    // zero means the two threads are balanced.
                    thread["lastSimulationWaitMs"] = threadStats.LastWaitMs;
                    report["renderThread"] = thread;
                }
            }

            return ToolResult::SuccessJson(report);
        }
    };

    // ========================================================================
    // SetGPUCulling - control the Nanite-class cluster culling pipeline
    // ========================================================================
    class SetGPUCullingTool : public MCPTool {
    public:
        SetGPUCullingTool()
            : MCPTool("SetGPUCulling",
                      "Enable or disable parts of the GPU-driven cluster culling pipeline: "
                      "the whole indirect path, hierarchical-Z occlusion culling, backface "
                      "cone culling, and the two-phase re-test. Use it to isolate what a "
                      "visual artefact or a performance change is coming from.") {}

        ToolAnnotations GetAnnotations() const override {
            ToolAnnotations annotations;
            annotations.Title = "Set GPU Culling";
            annotations.IdempotentHint = true;
            return annotations;
        }

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Properties = Json::object();
            schema.Properties["gpuDriven"] = RenderToolsDetail::SchemaProperty(
                "boolean", "Use the GPU indirect draw path at all. Off falls back to per-mesh direct draws.");
            schema.Properties["occlusion"] = RenderToolsDetail::SchemaProperty(
                "boolean", "Hierarchical-Z occlusion culling.");
            schema.Properties["coneCulling"] = RenderToolsDetail::SchemaProperty(
                "boolean", "Backface cluster cone culling.");
            schema.Properties["twoPhase"] = RenderToolsDetail::SchemaProperty(
                "boolean", "Second culling pass against the HZB built from this frame's depth.");
            return schema;
        }

        bool RequiresScene() const override { return false; }

        ToolResult Execute(const Json& arguments, ECS::Scene*) override {
            auto* renderer = RenderToolsDetail::SceneRendererOrNull();
            if (!renderer) {
                return ToolResult::Error("No scene renderer is active");
            }

            auto& culler = renderer->GetCuller();
            if (arguments.contains("gpuDriven") && arguments["gpuDriven"].is_boolean()) {
                renderer->SetGPUDrivenEnabled(arguments["gpuDriven"].get<bool>());
            }
            if (arguments.contains("occlusion") && arguments["occlusion"].is_boolean()) {
                culler.SetOcclusionEnabled(arguments["occlusion"].get<bool>());
            }
            if (arguments.contains("coneCulling") && arguments["coneCulling"].is_boolean()) {
                culler.SetConeCullingEnabled(arguments["coneCulling"].get<bool>());
            }
            if (arguments.contains("twoPhase") && arguments["twoPhase"].is_boolean()) {
                culler.SetTwoPhaseEnabled(arguments["twoPhase"].get<bool>());
            }

            Json state;
            state["gpuDriven"] = renderer->IsGPUDrivenEnabled();
            state["occlusion"] = culler.IsOcclusionEnabled();
            state["coneCulling"] = culler.IsConeCullingEnabled();
            state["twoPhase"] = culler.IsTwoPhaseEnabled();
            return ToolResult::Success("GPU culling updated", state);
        }
    };

    // ========================================================================
    // SetGlobalIllumination - control the dynamic GI pipeline
    // ========================================================================
    class SetGlobalIlluminationTool : public MCPTool {
    public:
        SetGlobalIlluminationTool()
            : MCPTool("SetGlobalIllumination",
                      "Tune the dynamic global illumination pass: enable it, set screen-space "
                      "rays per pixel and steps per ray, trace distance, indirect intensity, "
                      "temporal blend rate, the world radiance-cache probe spacing, and the sky "
                      "colour rays fall back to when they miss.") {}

        ToolAnnotations GetAnnotations() const override {
            ToolAnnotations annotations;
            annotations.Title = "Set Global Illumination";
            annotations.IdempotentHint = true;
            return annotations;
        }

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Properties = Json::object();
            schema.Properties["enabled"] = RenderToolsDetail::SchemaProperty("boolean", "Run the GI pass.");
            schema.Properties["raysPerPixel"] = RenderToolsDetail::NumberProperty(
                "Screen-space rays per half-resolution pixel per frame.", 1, 32);
            schema.Properties["stepsPerRay"] = RenderToolsDetail::NumberProperty(
                "March steps per ray.", 2, 64);
            schema.Properties["maxTraceDistance"] = RenderToolsDetail::NumberProperty(
                "World-space ray length in metres.", 0.5, 200.0);
            schema.Properties["intensity"] = RenderToolsDetail::NumberProperty(
                "Indirect light multiplier.", 0.0, 8.0);
            schema.Properties["temporalAlpha"] = RenderToolsDetail::NumberProperty(
                "New-frame weight in the temporal filter. Lower is smoother and laggier.", 0.01, 1.0);
            schema.Properties["probeSpacing"] = RenderToolsDetail::NumberProperty(
                "World radiance-cache probe spacing in metres.", 0.25, 20.0);
            schema.Properties["probeCacheEnabled"] = RenderToolsDetail::SchemaProperty(
                "boolean", "Use the world probe cache for rays that miss on screen.");
            schema.Properties["skyColor"] = Json{
                {"type", "array"},
                {"description", "Linear RGB fallback radiance for rays that escape."},
                {"items", Json{{"type", "number"}}},
                {"minItems", 3}, {"maxItems", 3}};
            schema.Properties["skyIntensity"] = RenderToolsDetail::NumberProperty(
                "Sky radiance multiplier.", 0.0, 16.0);
            return schema;
        }

        bool RequiresScene() const override { return false; }

        ToolResult Execute(const Json& arguments, ECS::Scene*) override {
            auto* renderer = RenderToolsDetail::SceneRendererOrNull();
            if (!renderer) {
                return ToolResult::Error("No scene renderer is active");
            }

            auto& settings = renderer->GetGlobalIllumination().GetSettings();
            if (arguments.contains("enabled") && arguments["enabled"].is_boolean()) {
                settings.Enabled = arguments["enabled"].get<bool>();
            }
            if (arguments.contains("raysPerPixel") && arguments["raysPerPixel"].is_number()) {
                settings.RaysPerPixel = std::clamp(arguments["raysPerPixel"].get<uint32_t>(), 1u, 32u);
            }
            if (arguments.contains("stepsPerRay") && arguments["stepsPerRay"].is_number()) {
                settings.StepsPerRay = std::clamp(arguments["stepsPerRay"].get<uint32_t>(), 2u, 64u);
            }
            if (arguments.contains("maxTraceDistance") && arguments["maxTraceDistance"].is_number()) {
                settings.MaxTraceDistance = std::clamp(arguments["maxTraceDistance"].get<float>(), 0.5f, 200.0f);
            }
            if (arguments.contains("intensity") && arguments["intensity"].is_number()) {
                settings.Intensity = std::clamp(arguments["intensity"].get<float>(), 0.0f, 8.0f);
            }
            if (arguments.contains("temporalAlpha") && arguments["temporalAlpha"].is_number()) {
                settings.TemporalAlpha = std::clamp(arguments["temporalAlpha"].get<float>(), 0.01f, 1.0f);
            }
            if (arguments.contains("probeSpacing") && arguments["probeSpacing"].is_number()) {
                settings.ProbeSpacing = std::clamp(arguments["probeSpacing"].get<float>(), 0.25f, 20.0f);
            }
            if (arguments.contains("probeCacheEnabled") && arguments["probeCacheEnabled"].is_boolean()) {
                settings.ProbeCacheEnabled = arguments["probeCacheEnabled"].get<bool>();
            }
            Math::Vec3 skyColor;
            if (RenderToolsDetail::ReadVec3(arguments, "skyColor", skyColor)) {
                settings.SkyColor = skyColor;
            }
            if (arguments.contains("skyIntensity") && arguments["skyIntensity"].is_number()) {
                settings.SkyIntensity = std::clamp(arguments["skyIntensity"].get<float>(), 0.0f, 16.0f);
            }

            Json state;
            state["enabled"] = settings.Enabled;
            state["raysPerPixel"] = settings.RaysPerPixel;
            state["stepsPerRay"] = settings.StepsPerRay;
            state["maxTraceDistance"] = settings.MaxTraceDistance;
            state["intensity"] = settings.Intensity;
            state["temporalAlpha"] = settings.TemporalAlpha;
            state["probeSpacing"] = settings.ProbeSpacing;
            state["probeCacheEnabled"] = settings.ProbeCacheEnabled;
            state["skyColor"] = RenderToolsDetail::Vec3ToJson(settings.SkyColor);
            state["skyIntensity"] = settings.SkyIntensity;
            return ToolResult::Success("Global illumination updated", state);
        }
    };

    // ========================================================================
    // SetUpscaler - FSR quality mode and sharpening
    // ========================================================================
    class SetUpscalerTool : public MCPTool {
    public:
        SetUpscalerTool()
            : MCPTool("SetUpscaler",
                      "Set the FSR upscaling mode. Changing quality changes the render "
                      "resolution and rebuilds every offscreen target, so the whole frame "
                      "gets cheaper or sharper. Modes: off, ultraQuality, quality, balanced, "
                      "performance, ultraPerformance.") {}

        ToolAnnotations GetAnnotations() const override {
            ToolAnnotations annotations;
            annotations.Title = "Set Upscaler";
            annotations.IdempotentHint = true;
            return annotations;
        }

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Properties = Json::object();
            schema.Properties["quality"] = Json{
                {"type", "string"},
                {"description", "FSR quality preset."},
                {"enum", Json::array({"off", "ultraQuality", "quality", "balanced",
                                      "performance", "ultraPerformance"})}};
            schema.Properties["enabled"] = RenderToolsDetail::SchemaProperty(
                "boolean", "Run the upscaler at all.");
            schema.Properties["sharpness"] = RenderToolsDetail::NumberProperty(
                "RCAS sharpening strength; 0 skips the sharpening pass.", 0.0, 1.0);
            schema.Properties["jitter"] = RenderToolsDetail::SchemaProperty(
                "boolean", "Apply the Halton sub-pixel jitter to the projection.");
            return schema;
        }

        bool RequiresScene() const override { return false; }

        ToolResult Execute(const Json& arguments, ECS::Scene*) override {
            auto* renderer = RenderToolsDetail::SceneRendererOrNull();
            if (!renderer) {
                return ToolResult::Error("No scene renderer is active");
            }

            auto& settings = renderer->GetUpscaler().GetSettings();
            bool resolutionChanged = false;

            if (arguments.contains("quality") && arguments["quality"].is_string()) {
                Renderer::FSRQualityMode mode;
                const std::string text = arguments["quality"].get<std::string>();
                if (!Renderer::FSRQualityModeFromString(text, mode)) {
                    return ToolResult::Error("Unknown FSR quality mode '" + text + "'");
                }
                resolutionChanged = mode != settings.Quality;
                settings.Quality = mode;
            }
            if (arguments.contains("enabled") && arguments["enabled"].is_boolean()) {
                const bool enabled = arguments["enabled"].get<bool>();
                resolutionChanged = resolutionChanged || enabled != settings.Enabled;
                settings.Enabled = enabled;
            }
            if (arguments.contains("sharpness") && arguments["sharpness"].is_number()) {
                settings.Sharpness = std::clamp(arguments["sharpness"].get<float>(), 0.0f, 1.0f);
            }
            if (arguments.contains("jitter") && arguments["jitter"].is_boolean()) {
                settings.JitterEnabled = arguments["jitter"].get<bool>();
            }

            // Only a quality or enable change moves the render resolution, and
            // that rebuild stalls the device - so do not pay for it otherwise.
            if (resolutionChanged && !renderer->ApplyUpscalerQuality()) {
                return ToolResult::Error("Failed to rebuild render targets for the new upscaler mode");
            }

            const auto& stats = renderer->GetStats();
            Json state;
            state["quality"] = Renderer::FSRQualityModeName(settings.Quality);
            state["enabled"] = settings.Enabled;
            state["sharpness"] = settings.Sharpness;
            state["jitter"] = settings.JitterEnabled;
            state["renderWidth"] = stats.RenderWidth;
            state["renderHeight"] = stats.RenderHeight;
            state["displayWidth"] = stats.DisplayWidth;
            state["displayHeight"] = stats.DisplayHeight;
            return ToolResult::Success("Upscaler updated", state);
        }
    };

    // ========================================================================
    // SetShadows - control the cascaded shadow maps
    // ========================================================================
    class SetShadowsTool : public MCPTool {
    public:
        SetShadowsTool()
            : MCPTool("SetShadows",
                      "Tune the directional cascaded shadow maps: enable them, set the cascade "
                      "count and resolution, the distance they cover, the split distribution, "
                      "depth/slope/normal bias, PCF kernel radius, and cascade stabilisation. "
                      "Changing the cascade count or resolution rebuilds the shadow array. "
                      "Only a directional light with castShadows set produces shadows; check "
                      "shadows.shadowLightIndex in GetRenderStats to see whether one was found.") {}

        ToolAnnotations GetAnnotations() const override {
            ToolAnnotations annotations;
            annotations.Title = "Set Shadows";
            annotations.IdempotentHint = true;
            return annotations;
        }

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Properties = Json::object();
            schema.Properties["enabled"] = RenderToolsDetail::SchemaProperty(
                "boolean", "Render the shadow cascades at all.");
            schema.Properties["cascadeCount"] = RenderToolsDetail::NumberProperty(
                "Number of cascades (1-4). Each is a full depth pass.", 1, 4);
            schema.Properties["resolution"] = RenderToolsDetail::NumberProperty(
                "Per-cascade shadow map resolution in pixels.", 256, 8192);
            schema.Properties["maxShadowDistance"] = RenderToolsDetail::NumberProperty(
                "How far the directional light casts, in metres.", 5.0, 5000.0);
            schema.Properties["splitLambda"] = RenderToolsDetail::NumberProperty(
                "0 = uniform cascade splits, 1 = fully logarithmic.", 0.0, 1.0);
            schema.Properties["depthBias"] = RenderToolsDetail::NumberProperty(
                "Constant depth bias applied during the shadow pass.", 0.0, 32.0);
            schema.Properties["slopeBias"] = RenderToolsDetail::NumberProperty(
                "Slope-scaled depth bias.", 0.0, 32.0);
            schema.Properties["normalBias"] = RenderToolsDetail::NumberProperty(
                "World-space offset along the surface normal before the shadow lookup.", 0.0, 1.0);
            schema.Properties["pcfRadius"] = RenderToolsDetail::NumberProperty(
                "PCF kernel half-width in texels. Higher is softer and slower.", 0, 8);
            schema.Properties["stabilizeCascades"] = RenderToolsDetail::SchemaProperty(
                "boolean", "Snap cascades to a texel grid so shadow edges stop crawling as the camera moves.");
            schema.Properties["spotShadows"] = RenderToolsDetail::SchemaProperty(
                "boolean", "Render shadow tiles for spot lights.");
            schema.Properties["pointShadows"] = RenderToolsDetail::SchemaProperty(
                "boolean", "Render cube shadows for point lights. Each takes six atlas tiles.");
            schema.Properties["cacheCascades"] = RenderToolsDetail::SchemaProperty(
                "boolean", "Redraw only the cascade pages whose contents changed.");
            schema.Properties["invalidateCache"] = RenderToolsDetail::SchemaProperty(
                "boolean", "Force every cascade page dirty for one frame. Use this to tell a "
                           "stale cached page apart from a genuine shading problem.");
            schema.Properties["atlasResolution"] = RenderToolsDetail::NumberProperty(
                "Spot shadow atlas resolution in pixels.", 512, 8192);
            schema.Properties["atlasTilesPerRow"] = RenderToolsDetail::NumberProperty(
                "Atlas tiles per row; the atlas holds this squared, capped at 8 lights.", 1, 8);
            return schema;
        }

        bool RequiresScene() const override { return false; }

        ToolResult Execute(const Json& arguments, ECS::Scene*) override {
            auto* renderer = RenderToolsDetail::SceneRendererOrNull();
            if (!renderer) {
                return ToolResult::Error("No scene renderer is active");
            }
            auto& shadows = renderer->GetShadowRenderer();
            if (!shadows.IsInitialized()) {
                return ToolResult::Error("The shadow renderer failed to initialize on this device");
            }

            auto& settings = shadows.GetSettings();
            bool rebuild = false;

            if (arguments.contains("enabled") && arguments["enabled"].is_boolean()) {
                settings.Enabled = arguments["enabled"].get<bool>();
            }
            if (arguments.contains("cascadeCount") && arguments["cascadeCount"].is_number()) {
                const uint32_t value = std::clamp(arguments["cascadeCount"].get<uint32_t>(),
                                                  1u, Renderer::kMaxShadowCascades);
                rebuild = rebuild || value != settings.CascadeCount;
                settings.CascadeCount = value;
            }
            if (arguments.contains("resolution") && arguments["resolution"].is_number()) {
                const uint32_t value = std::clamp(arguments["resolution"].get<uint32_t>(), 256u, 8192u);
                rebuild = rebuild || value != settings.CascadeResolution;
                settings.CascadeResolution = value;
            }
            if (arguments.contains("maxShadowDistance") && arguments["maxShadowDistance"].is_number()) {
                settings.MaxShadowDistance = std::clamp(arguments["maxShadowDistance"].get<float>(), 5.0f, 5000.0f);
            }
            if (arguments.contains("splitLambda") && arguments["splitLambda"].is_number()) {
                settings.CascadeSplitLambda = std::clamp(arguments["splitLambda"].get<float>(), 0.0f, 1.0f);
            }
            if (arguments.contains("depthBias") && arguments["depthBias"].is_number()) {
                settings.DepthBias = std::clamp(arguments["depthBias"].get<float>(), 0.0f, 32.0f);
            }
            if (arguments.contains("slopeBias") && arguments["slopeBias"].is_number()) {
                settings.SlopeBias = std::clamp(arguments["slopeBias"].get<float>(), 0.0f, 32.0f);
            }
            if (arguments.contains("normalBias") && arguments["normalBias"].is_number()) {
                settings.NormalBias = std::clamp(arguments["normalBias"].get<float>(), 0.0f, 1.0f);
            }
            if (arguments.contains("pcfRadius") && arguments["pcfRadius"].is_number()) {
                settings.PcfRadius = std::clamp(arguments["pcfRadius"].get<uint32_t>(), 0u, 8u);
            }
            if (arguments.contains("stabilizeCascades") && arguments["stabilizeCascades"].is_boolean()) {
                settings.StabilizeCascades = arguments["stabilizeCascades"].get<bool>();
            }
            if (arguments.contains("spotShadows") && arguments["spotShadows"].is_boolean()) {
                settings.SpotShadowsEnabled = arguments["spotShadows"].get<bool>();
            }
            if (arguments.contains("pointShadows") && arguments["pointShadows"].is_boolean()) {
                settings.PointShadowsEnabled = arguments["pointShadows"].get<bool>();
            }
            if (arguments.contains("cacheCascades") && arguments["cacheCascades"].is_boolean()) {
                settings.CacheCascades = arguments["cacheCascades"].get<bool>();
                shadows.InvalidateCache();
            }
            if (arguments.value("invalidateCache", false)) {
                shadows.InvalidateCache();
            }
            if (arguments.contains("atlasResolution") && arguments["atlasResolution"].is_number()) {
                const uint32_t value = std::clamp(arguments["atlasResolution"].get<uint32_t>(), 512u, 8192u);
                rebuild = rebuild || value != settings.AtlasResolution;
                settings.AtlasResolution = value;
            }
            if (arguments.contains("atlasTilesPerRow") && arguments["atlasTilesPerRow"].is_number()) {
                const uint32_t value = std::clamp(arguments["atlasTilesPerRow"].get<uint32_t>(), 1u, 8u);
                rebuild = rebuild || value != settings.AtlasTilesPerRow;
                settings.AtlasTilesPerRow = value;
            }

            // Only a count or resolution change reallocates; that stalls the
            // device, so it is not paid for a bias tweak.
            if (rebuild && !shadows.ApplySettings()) {
                return ToolResult::Error("Failed to rebuild the shadow cascade array");
            }

            const auto& stats = shadows.GetStats();
            Json state;
            state["enabled"] = settings.Enabled;
            state["cascadeCount"] = settings.CascadeCount;
            state["resolution"] = stats.Resolution;
            state["maxShadowDistance"] = settings.MaxShadowDistance;
            state["splitLambda"] = settings.CascadeSplitLambda;
            state["depthBias"] = settings.DepthBias;
            state["slopeBias"] = settings.SlopeBias;
            state["normalBias"] = settings.NormalBias;
            state["pcfRadius"] = settings.PcfRadius;
            state["stabilizeCascades"] = settings.StabilizeCascades;
            state["shadowLightIndex"] = stats.ShadowLightIndex;
            state["spotShadows"] = settings.SpotShadowsEnabled;
            state["atlasResolution"] = stats.AtlasResolution;
            state["atlasTileSize"] = stats.AtlasTileSize;
            state["spotShadowCount"] = stats.SpotShadowCount;
            state["pointShadows"] = settings.PointShadowsEnabled;
            state["pointShadowCount"] = stats.PointShadowCount;
            state["atlasTilesUsed"] = stats.AtlasTilesUsed;
            state["atlasTilesTotal"] = stats.AtlasTilesTotal;
            state["cacheCascades"] = settings.CacheCascades;
            state["dirtyPages"] = stats.DirtyPages;
            state["totalPages"] = stats.TotalPages;
            return ToolResult::Success("Shadow settings updated", state);
        }
    };

    // ========================================================================
    // SetPostProcess - bloom
    // ========================================================================
    class SetPostProcessTool : public MCPTool {
    public:
        SetPostProcessTool()
            : MCPTool("SetPostProcess",
                      "Tune post-processing. Bloom runs as compute passes on the HDR scene "
                      "before upscaling: a soft-knee threshold, then a progressive blur down "
                      "and back up a mip chain. The older PostProcessManager passes (SSAO, "
                      "depth of field, motion blur, colour grading) are registered but cannot "
                      "run - they never write their descriptor sets, and PostProcessPass has no "
                      "way to receive a depth buffer - so bloom, SSAO, and colour grading are "
                      "reimplemented here over the G-buffer and respond to these settings. "
                      "Temporal antialiasing resolves the sub-pixel jitter the frame is already "
                      "rendered with; with it off the jitter is pure cost. Depth of field and "
                      "motion blur do not exist yet.") {}

        ToolAnnotations GetAnnotations() const override {
            ToolAnnotations annotations;
            annotations.Title = "Set Post Process";
            annotations.IdempotentHint = true;
            return annotations;
        }

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Properties = Json::object();
            schema.Properties["bloom"] = RenderToolsDetail::SchemaProperty(
                "boolean", "Run the bloom chain.");
            schema.Properties["bloomThreshold"] = RenderToolsDetail::NumberProperty(
                "Luminance above which a pixel blooms.", 0.0, 16.0);
            schema.Properties["bloomIntensity"] = RenderToolsDetail::NumberProperty(
                "How strongly the glow is added back over the scene.", 0.0, 8.0);
            schema.Properties["bloomSoftKnee"] = RenderToolsDetail::NumberProperty(
                "Softness of the threshold. 0 is a hard cutoff, which flickers in motion.", 0.0, 1.0);
            schema.Properties["bloomScatter"] = RenderToolsDetail::NumberProperty(
                "How far the glow spreads during upsampling.", 0.1, 4.0);
            schema.Properties["ssao"] = RenderToolsDetail::SchemaProperty(
                "boolean", "Run screen-space ambient occlusion over the G-buffer.");
            schema.Properties["ssaoRadius"] = RenderToolsDetail::NumberProperty(
                "World-space sampling radius in metres.", 0.05, 10.0);
            schema.Properties["ssaoIntensity"] = RenderToolsDetail::NumberProperty(
                "How strongly occlusion darkens ambient and indirect light.", 0.0, 4.0);
            schema.Properties["ssaoBias"] = RenderToolsDetail::NumberProperty(
                "Depth bias that stops a surface occluding itself.", 0.0, 1.0);
            schema.Properties["ssaoKernelSize"] = RenderToolsDetail::NumberProperty(
                "Samples per pixel.", 4, 64);
            schema.Properties["ssaoBlurPasses"] = RenderToolsDetail::NumberProperty(
                "Depth-aware blur passes over the occlusion buffer.", 0, 4);
            schema.Properties["taa"] = RenderToolsDetail::SchemaProperty(
                "boolean", "Resolve the jittered frames into a temporally antialiased image.");
            schema.Properties["taaFeedback"] = RenderToolsDetail::NumberProperty(
                "Fraction of the history kept each frame. Higher is steadier and slower to "
                "react; past about 0.98 a moving edge never catches up.", 0.0, 0.98);
            schema.Properties["exposure"] = RenderToolsDetail::NumberProperty(
                "Linear exposure applied before the tonemap.", 0.0, 16.0);
            schema.Properties["contrast"] = RenderToolsDetail::NumberProperty(
                "Contrast applied after the tonemap.", 0.0, 4.0);
            schema.Properties["saturation"] = RenderToolsDetail::NumberProperty(
                "Saturation applied after the tonemap. 0 is greyscale.", 0.0, 4.0);
            schema.Properties["vignette"] = RenderToolsDetail::NumberProperty(
                "Corner darkening strength. 0 disables it.", 0.0, 4.0);
            schema.Properties["colorFilter"] = Json{
                {"type", "array"},
                {"description", "Linear RGB tint applied before the tonemap."},
                {"items", Json{{"type", "number"}}},
                {"minItems", 3}, {"maxItems", 3}};
            return schema;
        }

        bool RequiresScene() const override { return false; }

        ToolResult Execute(const Json& arguments, ECS::Scene*) override {
            auto* renderer = RenderToolsDetail::SceneRendererOrNull();
            if (!renderer) {
                return ToolResult::Error("No scene renderer is active");
            }
            auto& settings = renderer->GetPostProcessSettings();

            if (arguments.contains("bloom") && arguments["bloom"].is_boolean()) {
                settings.bloomEnabled = arguments["bloom"].get<bool>();
            }
            if (arguments.contains("bloomThreshold") && arguments["bloomThreshold"].is_number()) {
                settings.bloomThreshold = std::clamp(arguments["bloomThreshold"].get<float>(), 0.0f, 16.0f);
            }
            if (arguments.contains("bloomIntensity") && arguments["bloomIntensity"].is_number()) {
                settings.bloomIntensity = std::clamp(arguments["bloomIntensity"].get<float>(), 0.0f, 8.0f);
            }
            if (arguments.contains("bloomSoftKnee") && arguments["bloomSoftKnee"].is_number()) {
                settings.bloomSoftKnee = std::clamp(arguments["bloomSoftKnee"].get<float>(), 0.0f, 1.0f);
            }
            if (arguments.contains("bloomScatter") && arguments["bloomScatter"].is_number()) {
                settings.bloomScatter = std::clamp(arguments["bloomScatter"].get<float>(), 0.1f, 4.0f);
            }
            if (arguments.contains("ssao") && arguments["ssao"].is_boolean()) {
                settings.ssaoEnabled = arguments["ssao"].get<bool>();
            }
            if (arguments.contains("ssaoRadius") && arguments["ssaoRadius"].is_number()) {
                settings.ssaoRadius = std::clamp(arguments["ssaoRadius"].get<float>(), 0.05f, 10.0f);
            }
            if (arguments.contains("ssaoIntensity") && arguments["ssaoIntensity"].is_number()) {
                settings.ssaoIntensity = std::clamp(arguments["ssaoIntensity"].get<float>(), 0.0f, 4.0f);
            }
            if (arguments.contains("ssaoBias") && arguments["ssaoBias"].is_number()) {
                settings.ssaoBias = std::clamp(arguments["ssaoBias"].get<float>(), 0.0f, 1.0f);
            }
            if (arguments.contains("ssaoKernelSize") && arguments["ssaoKernelSize"].is_number()) {
                settings.ssaoKernelSize = std::clamp(arguments["ssaoKernelSize"].get<int>(), 4, 64);
            }
            if (arguments.contains("ssaoBlurPasses") && arguments["ssaoBlurPasses"].is_number()) {
                settings.ssaoBlurPasses = std::clamp(arguments["ssaoBlurPasses"].get<int>(), 0, 4);
            }
            if (arguments.contains("taa") && arguments["taa"].is_boolean()) {
                renderer->GetTAA().SetEnabled(arguments["taa"].get<bool>());
            }
            if (arguments.contains("taaFeedback") && arguments["taaFeedback"].is_number()) {
                renderer->GetTAA().SetFeedback(arguments["taaFeedback"].get<float>());
            }
            if (arguments.contains("exposure") && arguments["exposure"].is_number()) {
                settings.exposure = std::clamp(arguments["exposure"].get<float>(), 0.0f, 16.0f);
            }
            if (arguments.contains("contrast") && arguments["contrast"].is_number()) {
                settings.contrast = std::clamp(arguments["contrast"].get<float>(), 0.0f, 4.0f);
            }
            if (arguments.contains("saturation") && arguments["saturation"].is_number()) {
                settings.saturation = std::clamp(arguments["saturation"].get<float>(), 0.0f, 4.0f);
            }
            if (arguments.contains("vignette") && arguments["vignette"].is_number()) {
                settings.vignetteIntensity = std::clamp(arguments["vignette"].get<float>(), 0.0f, 4.0f);
            }
            Math::Vec3 filter;
            if (RenderToolsDetail::ReadVec3(arguments, "colorFilter", filter)) {
                settings.colorFilter = filter;
            }

            const auto& stats = renderer->GetBloom().GetStats();
            Json state;
            state["taa"] = renderer->GetTAA().IsEnabled();
            state["taaFeedback"] = renderer->GetTAA().GetFeedback();
            state["bloom"] = settings.bloomEnabled;
            state["bloomThreshold"] = settings.bloomThreshold;
            state["bloomIntensity"] = settings.bloomIntensity;
            state["bloomSoftKnee"] = settings.bloomSoftKnee;
            state["bloomScatter"] = settings.bloomScatter;
            state["mipCount"] = stats.MipCount;
            state["active"] = stats.Active;
            const auto& ssao = renderer->GetSSAO().GetStats();
            state["ssao"] = settings.ssaoEnabled;
            state["ssaoActive"] = ssao.Active;
            state["ssaoRadius"] = settings.ssaoRadius;
            state["ssaoIntensity"] = settings.ssaoIntensity;
            state["ssaoKernelSize"] = ssao.KernelSize;
            state["ssaoBlurPasses"] = ssao.BlurPasses;
            state["ssaoWidth"] = ssao.Width;
            state["ssaoHeight"] = ssao.Height;
            state["exposure"] = settings.exposure;
            state["contrast"] = settings.contrast;
            state["saturation"] = settings.saturation;
            state["vignette"] = settings.vignetteIntensity;
            state["colorFilter"] = RenderToolsDetail::Vec3ToJson(settings.colorFilter);
            return ToolResult::Success("Post-process settings updated", state);
        }
    };

    // ========================================================================
    // ListMaterials
    // ========================================================================
    class ListMaterialsTool : public MCPTool {
    public:
        ListMaterialsTool()
            : MCPTool("ListMaterials",
                      "List every material in the library with its index, node/link counts, "
                      "and whether its graph compiled. The index is what a mesh's "
                      "MaterialIndex refers to.") {}

        ToolAnnotations GetAnnotations() const override {
            ToolAnnotations annotations;
            annotations.Title = "List Materials";
            annotations.ReadOnlyHint = true;
            annotations.IdempotentHint = true;
            return annotations;
        }

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Properties = Json::object();
            return schema;
        }

        bool RequiresScene() const override { return false; }

        ToolResult Execute(const Json&, ECS::Scene*) override {
            auto& library = Renderer::MaterialLibrary::Get();
            library.GetOrCreateDefault();

            Json materials = Json::array();
            for (uint32_t index = 0; index < library.GetMaterialCount(); ++index) {
                const auto* material = library.GetMaterial(index);
                if (!material) {
                    continue;
                }
                Json entry;
                entry["index"] = index;
                entry["name"] = material->Name;
                entry["nodeCount"] = material->Graph.GetNodes().size();
                entry["linkCount"] = material->Graph.GetLinks().size();
                entry["compiled"] = material->Compiled.Succeeded;
                entry["doubleSided"] = material->DoubleSided;
                if (!material->Compiled.Succeeded) {
                    entry["error"] = material->Compiled.Error;
                }
                entry["textureSlots"] = material->Compiled.TextureSlots;
                materials.push_back(entry);
            }

            Json report;
            report["revision"] = library.GetRevision();
            report["materials"] = materials;
            return ToolResult::SuccessJson(report);
        }
    };

    // ========================================================================
    // SetMaterialGraph - author a material as a node graph
    // ========================================================================
    class SetMaterialGraphTool : public MCPTool {
    public:
        SetMaterialGraphTool()
            : MCPTool("SetMaterialGraph",
                      "Create or replace a material's node graph from JSON, compile it to "
                      "GLSL, and rebuild its pipeline on the next frame. The document is "
                      "{name, nodes:[{id,type,name,value:[r,g,b,a],texture}], links:[{from,to,slot}]}. "
                      "Node types include ConstantColor, ConstantScalar, TextureSample, UV, "
                      "WorldPosition, WorldNormal, Time, CameraVector, Multiply, Add, Subtract, "
                      "Lerp, DotProduct, OneMinus, Saturate, Power, Fresnel, Panner, Normalize, "
                      "Split and Output. Output slots are 0 BaseColor, 1 Metallic, 2 Roughness, "
                      "3 Emissive, 4 Normal, 5 Opacity. Rejects cycles and unknown node types "
                      "without touching the existing material.") {}

        ToolAnnotations GetAnnotations() const override {
            ToolAnnotations annotations;
            annotations.Title = "Set Material Graph";
            annotations.DestructiveHint = true;
            return annotations;
        }

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Properties = Json::object();
            schema.Properties["name"] = RenderToolsDetail::SchemaProperty(
                "string", "Material name. Created if it does not exist.");
            schema.Properties["graph"] = Json{
                {"type", "object"},
                {"description", "The graph document. Accepts an object or a JSON string."}};
            schema.Properties["doubleSided"] = RenderToolsDetail::SchemaProperty(
                "boolean", "Disable backface culling for this material.");
            schema.Required = {"name", "graph"};
            return schema;
        }

        bool RequiresScene() const override { return false; }

        ToolResult Execute(const Json& arguments, ECS::Scene*) override {
            if (!arguments.contains("name") || !arguments["name"].is_string()) {
                return ToolResult::Error("'name' is required and must be a string");
            }
            if (!arguments.contains("graph")) {
                return ToolResult::Error("'graph' is required");
            }

            const std::string name = arguments["name"].get<std::string>();
            const std::string document = arguments["graph"].is_string()
                                             ? arguments["graph"].get<std::string>()
                                             : arguments["graph"].dump();

            Renderer::MaterialGraph graph;
            std::string error;
            if (!Renderer::MaterialGraph::FromJson(document, graph, &error)) {
                return ToolResult::Error("Material graph rejected: " + error);
            }
            graph.SetName(name);

            auto& library = Renderer::MaterialLibrary::Get();
            library.GetOrCreateDefault();
            const uint32_t index = library.CreateMaterial(name);
            auto* material = library.GetMaterial(index);
            if (!material) {
                return ToolResult::Error("Failed to allocate a material slot");
            }

            material->Graph = std::move(graph);
            if (arguments.contains("doubleSided") && arguments["doubleSided"].is_boolean()) {
                material->DoubleSided = arguments["doubleSided"].get<bool>();
            }
            library.MarkDirty(index);
            library.CompileDirty();

            Json state;
            state["index"] = index;
            state["name"] = material->Name;
            state["compiled"] = material->Compiled.Succeeded;
            state["textureSlots"] = material->Compiled.TextureSlots;
            if (!material->Compiled.Succeeded) {
                state["error"] = material->Compiled.Error;
                return ToolResult::Success("Material stored but its graph does not compile; "
                                           "the fallback surface will be used", state);
            }
            return ToolResult::Success("Material graph compiled; the pipeline rebuilds next frame",
                                       state);
        }
    };

    // ========================================================================
    // GetMaterialGraph
    // ========================================================================
    class GetMaterialGraphTool : public MCPTool {
    public:
        GetMaterialGraphTool()
            : MCPTool("GetMaterialGraph",
                      "Return a material's node graph as JSON, plus the GLSL body it compiles "
                      "to. Use this to read a material before editing it, or to see why one "
                      "renders the way it does.") {}

        ToolAnnotations GetAnnotations() const override {
            ToolAnnotations annotations;
            annotations.Title = "Get Material Graph";
            annotations.ReadOnlyHint = true;
            annotations.IdempotentHint = true;
            return annotations;
        }

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Properties = Json::object();
            schema.Properties["name"] = RenderToolsDetail::SchemaProperty("string", "Material name.");
            schema.Properties["index"] = RenderToolsDetail::SchemaProperty(
                "integer", "Material index. Used when 'name' is absent.");
            schema.Properties["includeGeneratedCode"] = RenderToolsDetail::SchemaProperty(
                "boolean", "Include the generated GLSL fragment body.");
            return schema;
        }

        bool RequiresScene() const override { return false; }

        ToolResult Execute(const Json& arguments, ECS::Scene*) override {
            auto& library = Renderer::MaterialLibrary::Get();
            library.GetOrCreateDefault();

            uint32_t index = UINT32_MAX;
            if (arguments.contains("name") && arguments["name"].is_string()) {
                index = library.FindMaterial(arguments["name"].get<std::string>());
                if (index == UINT32_MAX) {
                    return ToolResult::Error("No material named '" +
                                             arguments["name"].get<std::string>() + "'");
                }
            } else if (arguments.contains("index") && arguments["index"].is_number_integer()) {
                index = arguments["index"].get<uint32_t>();
            } else {
                return ToolResult::Error("Provide either 'name' or 'index'");
            }

            const auto* material = library.GetMaterial(index);
            if (!material) {
                return ToolResult::Error("Material index " + std::to_string(index) + " is out of range");
            }

            Json report;
            report["index"] = index;
            report["name"] = material->Name;
            report["compiled"] = material->Compiled.Succeeded;
            report["doubleSided"] = material->DoubleSided;
            report["graph"] = Json::parse(material->Graph.ToJson(), nullptr, false);
            report["textureSlots"] = material->Compiled.TextureSlots;
            if (!material->Compiled.Succeeded) {
                report["error"] = material->Compiled.Error;
            }
            if (arguments.value("includeGeneratedCode", false)) {
                report["generatedGlsl"] = material->Compiled.FragmentBody;
            }
            return ToolResult::SuccessJson(report);
        }
    };

    // ========================================================================
    // SetEditorViewport - drive the editor camera, gizmo, and play state
    // ========================================================================
    class SetEditorViewportTool : public MCPTool {
    public:
        SetEditorViewportTool()
            : MCPTool("SetEditorViewport",
                      "Control the editor scene viewport: switch between the editor fly camera "
                      "and the scene camera, choose the gizmo mode (none/translate/rotate/scale), "
                      "frame the current selection, and pause, resume, or single-step the "
                      "simulation while the frame stays inspectable.") {}

        ToolAnnotations GetAnnotations() const override {
            ToolAnnotations annotations;
            annotations.Title = "Set Editor Viewport";
            return annotations;
        }

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Properties = Json::object();
            schema.Properties["open"] = RenderToolsDetail::SchemaProperty(
                "boolean", "Show or hide the viewport panel.");
            schema.Properties["editorCamera"] = RenderToolsDetail::SchemaProperty(
                "boolean", "Drive the frame from the editor fly camera instead of the scene camera.");
            schema.Properties["gizmo"] = Json{
                {"type", "string"},
                {"description", "Transform gizmo mode."},
                {"enum", Json::array({"none", "translate", "rotate", "scale"})}};
            schema.Properties["focusSelection"] = RenderToolsDetail::SchemaProperty(
                "boolean", "Move the editor camera to frame the current selection.");
            schema.Properties["paused"] = RenderToolsDetail::SchemaProperty(
                "boolean", "Freeze or resume the simulation.");
            schema.Properties["stepFrames"] = RenderToolsDetail::SchemaProperty(
                "integer", "Advance this many frames while paused, then re-freeze.");
            schema.Properties["cameraSpeed"] = RenderToolsDetail::NumberProperty(
                "Editor fly camera speed in metres per second.", 0.1, 200.0);
            return schema;
        }

        ToolResult Execute(const Json& arguments, ECS::Scene* scene) override {
            auto* editorModule = UI::UIManager::Get().GetEditorModule();
            if (!editorModule) {
                return ToolResult::Error(
                    "The editor module is not available. The engine is running headless or "
                    "with UI disabled.");
            }
            auto& viewport = editorModule->GetViewportPanel();
            auto& state = viewport.GetState();

            if (arguments.contains("open") && arguments["open"].is_boolean()) {
                state.Open = arguments["open"].get<bool>();
            }
            if (arguments.contains("editorCamera") && arguments["editorCamera"].is_boolean()) {
                state.UseEditorCamera = arguments["editorCamera"].get<bool>();
            }
            if (arguments.contains("cameraSpeed") && arguments["cameraSpeed"].is_number()) {
                state.CameraSpeed = std::clamp(arguments["cameraSpeed"].get<float>(), 0.1f, 200.0f);
            }
            if (arguments.contains("gizmo") && arguments["gizmo"].is_string()) {
                const std::string mode = arguments["gizmo"].get<std::string>();
                if (mode == "none")            state.Gizmo = Editor::GizmoMode::None;
                else if (mode == "translate")  state.Gizmo = Editor::GizmoMode::Translate;
                else if (mode == "rotate")     state.Gizmo = Editor::GizmoMode::Rotate;
                else if (mode == "scale")      state.Gizmo = Editor::GizmoMode::Scale;
                else return ToolResult::Error("Unknown gizmo mode '" + mode + "'");
            }
            if (arguments.value("focusSelection", false)) {
                viewport.FocusOnSelection(editorModule->GetContext());
            }

            auto* pipeline = scene ? scene->GetSystemPipeline() : nullptr;
            if (pipeline) {
                if (arguments.contains("paused") && arguments["paused"].is_boolean()) {
                    pipeline->SetPaused(arguments["paused"].get<bool>());
                }
                if (arguments.contains("stepFrames") && arguments["stepFrames"].is_number_integer()) {
                    const int frames = arguments["stepFrames"].get<int>();
                    if (frames > 0) {
                        pipeline->SetPaused(true);
                        pipeline->RequestStepFrames(static_cast<uint32_t>(frames));
                    }
                }
            }

            Json report;
            report["open"] = state.Open;
            report["editorCamera"] = state.UseEditorCamera;
            report["cameraSpeed"] = state.CameraSpeed;
            switch (state.Gizmo) {
                case Editor::GizmoMode::None:      report["gizmo"] = "none"; break;
                case Editor::GizmoMode::Translate: report["gizmo"] = "translate"; break;
                case Editor::GizmoMode::Rotate:    report["gizmo"] = "rotate"; break;
                case Editor::GizmoMode::Scale:     report["gizmo"] = "scale"; break;
            }
            report["paused"] = pipeline ? pipeline->IsPaused() : false;
            report["pendingStepFrames"] = pipeline ? pipeline->GetPendingStepFrames() : 0u;
            return ToolResult::Success("Editor viewport updated", report);
        }
    };


    // =========================================================================
    // CaptureFrame - write the rendered image to a PNG
    // =========================================================================

    class CaptureFrameTool : public MCPTool {
    public:
        CaptureFrameTool()
            : MCPTool("CaptureFrame",
                      "Write the last rendered frame to a PNG under the project root. This is "
                      "the only way anything outside the window can see what the renderer "
                      "produced, which is what makes a visual change checkable at all - "
                      "GetRenderStats reports that a pass ran, not what it drew.") {}

        ToolAnnotations GetAnnotations() const override {
            ToolAnnotations annotations;
            annotations.Title = "Capture Frame";
            annotations.ReadOnlyHint = false;
            return annotations;
        }

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Properties = Json::object();
            schema.Properties["path"] = RenderToolsDetail::SchemaProperty(
                "string", "Destination PNG, relative to the project root.");
            schema.Required = {"path"};
            return schema;
        }

        bool RequiresScene() const override { return false; }

        ToolResult Execute(const Json& arguments, ECS::Scene*) override {
            auto* renderer = RenderToolsDetail::SceneRendererOrNull();
            if (!renderer) {
                return ToolResult::Error("No scene renderer is active");
            }
            if (!arguments.contains("path") || !arguments["path"].is_string()) {
                return ToolResult::Error("path is required");
            }

            std::filesystem::path resolved;
            std::string error;
            if (!AssetToolsDetail::ResolveProjectPath(arguments["path"].get<std::string>(),
                                                      resolved, error)) {
                return ToolResult::Error("Capture path rejected: " + error);
            }
            if (!renderer->CaptureToFile(resolved.string(), error)) {
                return ToolResult::Error("Capture failed: " + error);
            }

            const auto& stats = renderer->GetStats();
            Json state;
            state["path"] = arguments["path"];
            state["width"] = stats.DisplayWidth;
            state["height"] = stats.DisplayHeight;
            state["renderWidth"] = stats.RenderWidth;
            state["renderHeight"] = stats.RenderHeight;
            state["upscaled"] = stats.DisplayWidth != stats.RenderWidth;
            state["taaActive"] = stats.TAAActive;
            return ToolResult::SuccessJson(state);
        }
    };

    // ========================================================================
    // Factory
    // ========================================================================
    inline std::vector<MCPToolPtr> CreateRenderTools() {
        std::vector<MCPToolPtr> tools;
        tools.push_back(std::make_shared<GetRenderStatsTool>());
        tools.push_back(std::make_shared<SetGPUCullingTool>());
        tools.push_back(std::make_shared<SetGlobalIlluminationTool>());
        tools.push_back(std::make_shared<SetUpscalerTool>());
        tools.push_back(std::make_shared<SetShadowsTool>());
        tools.push_back(std::make_shared<SetPostProcessTool>());
        tools.push_back(std::make_shared<ListMaterialsTool>());
        tools.push_back(std::make_shared<SetMaterialGraphTool>());
        tools.push_back(std::make_shared<GetMaterialGraphTool>());
        tools.push_back(std::make_shared<SetEditorViewportTool>());
        tools.push_back(std::make_shared<CaptureFrameTool>());
        return tools;
    }

} // namespace MCP
} // namespace Core
