#include "EditorModule.h"

#include "Core/Editor/EditorAPI.h"
#include "Core/Log.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace Core {
namespace Editor {
namespace {

    void DrawVisualScriptingEditor(EditorContext& ctx) {
        if (!ctx.Panels.VisualScripting.Open) {
            return;
        }

        if (!ImGui::Begin("Visual Scripting", &ctx.Panels.VisualScripting.Open)) {
            ImGui::End();
            return;
        }

        constexpr std::size_t kGuidLength = 255;
        std::array<char, kGuidLength + 1> guidBuffer{};
        const std::size_t copyLength = std::min(ctx.Panels.VisualScripting.ActiveGraphGuid.size(), kGuidLength);
        std::memcpy(guidBuffer.data(), ctx.Panels.VisualScripting.ActiveGraphGuid.data(), copyLength);
        guidBuffer[copyLength] = '\0';

        if (ImGui::InputText("Graph GUID", guidBuffer.data(), guidBuffer.size())) {
            ctx.Panels.VisualScripting.ActiveGraphGuid = std::string(guidBuffer.data());
        }

        if (ctx.Panels.VisualScripting.ActiveGraphGuid.empty()) {
            ctx.Panels.VisualScripting.ActiveGraphGuid = "default-graph";
        }

        if (ImGui::Button("Compile")) {
            auto compileResult = CompileVisualScriptingGraph(ctx, ctx.Panels.VisualScripting.ActiveGraphGuid);
            if (!compileResult.Ok) {
                ctx.Diagnostics.push_back("Graph compile failed: " + compileResult.Error);
            } else {
                ctx.Diagnostics.push_back("Graph compiled: " + compileResult.Value.GraphGuid);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Execute")) {
            auto executeResult = ExecuteVisualScriptingGraph(ctx, ctx.Panels.VisualScripting.ActiveGraphGuid, ExecutionMode::Manual);
            if (!executeResult.Ok) {
                ctx.Diagnostics.push_back("Graph execution failed: " + executeResult.Error);
            }
        }

        ImGui::Separator();
        ImGui::Text("Nodes: %zu", ctx.GraphRuntime.Graphs[ctx.Panels.VisualScripting.ActiveGraphGuid].Nodes.size());
        ImGui::Text("Edges: %zu", ctx.GraphRuntime.Graphs[ctx.Panels.VisualScripting.ActiveGraphGuid].Edges.size());

        if (!ctx.Diagnostics.empty()) {
            ImGui::Separator();
            ImGui::TextUnformatted("Diagnostics:");
            const std::size_t maxDiagnostics = 8;
            const std::size_t start = ctx.Diagnostics.size() > maxDiagnostics ? ctx.Diagnostics.size() - maxDiagnostics : 0;
            for (std::size_t i = start; i < ctx.Diagnostics.size(); ++i) {
                ImGui::BulletText("%s", ctx.Diagnostics[i].c_str());
            }
        }

        ImGui::End();
    }

    void DrawSequencerEditor(EditorContext& ctx) {
        if (!ctx.Panels.Sequencer.Open) {
            return;
        }

        if (!ImGui::Begin("Cinematic Sequencer", &ctx.Panels.Sequencer.Open)) {
            ImGui::End();
            return;
        }

        constexpr std::size_t kGuidLength = 255;
        std::array<char, kGuidLength + 1> guidBuffer{};
        const std::size_t copyLength = std::min(ctx.Panels.Sequencer.ActiveTimelineGuid.size(), kGuidLength);
        std::memcpy(guidBuffer.data(), ctx.Panels.Sequencer.ActiveTimelineGuid.data(), copyLength);
        guidBuffer[copyLength] = '\0';

        if (ImGui::InputText("Timeline GUID", guidBuffer.data(), guidBuffer.size())) {
            ctx.Panels.Sequencer.ActiveTimelineGuid = std::string(guidBuffer.data());
        }
        if (ctx.Panels.Sequencer.ActiveTimelineGuid.empty()) {
            ctx.Panels.Sequencer.ActiveTimelineGuid = "default-timeline";
        }

        auto timelineIt = ctx.Sequencer.Timelines.find(ctx.Panels.Sequencer.ActiveTimelineGuid);
        if (timelineIt == ctx.Sequencer.Timelines.end()) {
            OpenCinematicSequencerEditor(ctx, ctx.Panels.Sequencer.ActiveTimelineGuid);
            timelineIt = ctx.Sequencer.Timelines.find(ctx.Panels.Sequencer.ActiveTimelineGuid);
        }
        if (timelineIt == ctx.Sequencer.Timelines.end()) {
            ImGui::TextUnformatted("Unable to create timeline.");
            ImGui::End();
            return;
        }

        TimelineAsset& timeline = timelineIt->second;

        if (ImGui::Button(ctx.Panels.Sequencer.IsPlaying ? "Pause" : "Play")) {
            ctx.Panels.Sequencer.IsPlaying = !ctx.Panels.Sequencer.IsPlaying;
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            ctx.Panels.Sequencer.IsPlaying = false;
            ctx.Panels.Sequencer.PlayheadSeconds = 0.0f;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Loop", &ctx.Panels.Sequencer.LoopPlayback);

        ImGui::SliderFloat("Playhead", &ctx.Panels.Sequencer.PlayheadSeconds, 0.0f, std::max(0.01f, timeline.DurationSeconds));
        if (ImGui::Button("Evaluate")) {
            auto evalResult = EvaluateTimelineAtTime(
                ctx,
                ctx.Panels.Sequencer.ActiveTimelineGuid,
                ctx.Panels.Sequencer.PlayheadSeconds);
            if (!evalResult.Ok) {
                ctx.Diagnostics.push_back("Timeline evaluate failed: " + evalResult.Error);
            } else {
                ctx.Diagnostics.push_back("Timeline evaluate active clips: " + std::to_string(evalResult.Value.ActiveClipIds.size()));
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Add Camera Track")) {
            AddTimelineTrack(ctx, timeline.Guid, TrackType::Camera, "Camera");
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Animation Track")) {
            AddTimelineTrack(ctx, timeline.Guid, TrackType::Animation, "Animation");
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Audio Track")) {
            AddTimelineTrack(ctx, timeline.Guid, TrackType::Audio, "Audio");
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Event Track")) {
            AddTimelineTrack(ctx, timeline.Guid, TrackType::Event, "Event");
        }

        if (!timeline.Tracks.empty()) {
            if (ImGui::Button("Add Clip To First Track")) {
                TimelineClipCreateInfo clipInfo;
                clipInfo.StartTime = ctx.Panels.Sequencer.PlayheadSeconds;
                clipInfo.EndTime = clipInfo.StartTime + 1.0f;
                clipInfo.ClipType = "event";
                clipInfo.Payload = EditorJson{{"label", "EditorClip"}};
                clipInfo.Easing = "linear";
                AddTimelineClip(ctx, timeline.Tracks.front().TrackId, clipInfo);
            }
            ImGui::SameLine();
            if (ImGui::Button("Record Take") && ctx.Selection.Primary != entt::null) {
                TakeRecordOptions options;
                options.DurationSeconds = 2.0f;
                options.FrameRate = timeline.FrameRate;
                options.ClipType = "animation_take";
                auto takeResult = RecordAnimationTake(ctx, timeline.Guid, ctx.Selection.Primary, options);
                if (!takeResult.Ok) {
                    ctx.Diagnostics.push_back("Take record failed: " + takeResult.Error);
                }
            }
        }

        ImGui::Separator();
        ImGui::Text("Duration: %.2f s", timeline.DurationSeconds);
        ImGui::Text("Tracks: %zu", timeline.Tracks.size());
        for (const auto& track : timeline.Tracks) {
            ImGui::BulletText("%s (%s) - %zu clip(s)",
                              track.DisplayName.c_str(),
                              TrackTypeToString(track.Type),
                              track.Clips.size());
        }

        ImGui::End();
    }

} // namespace

    void EditorModule::Initialize(ECS::Scene* activeScene) {
        m_Context.ActiveScene = activeScene;
        m_Context.Initialized = true;
        m_Context.Panels.Hierarchy.Open = true;
        m_Context.Panels.Inspector.Open = true;
        m_Context.Panels.VisualScripting.Open = false;
        m_Context.Panels.Sequencer.Open = false;
    }

    void EditorModule::Shutdown() {
        m_Context = EditorContext{};
        m_Enabled = false;
    }

    void EditorModule::SetActiveScene(ECS::Scene* activeScene) {
        m_Context.ActiveScene = activeScene;
    }

    void EditorModule::Update(float deltaTime) {
        if (!m_Enabled || !m_Context.Initialized) {
            return;
        }

        if (!m_Context.Panels.Sequencer.IsPlaying || m_Context.Panels.Sequencer.ActiveTimelineGuid.empty()) {
            return;
        }

        auto timelineIt = m_Context.Sequencer.Timelines.find(m_Context.Panels.Sequencer.ActiveTimelineGuid);
        if (timelineIt == m_Context.Sequencer.Timelines.end()) {
            return;
        }

        TimelineAsset& timeline = timelineIt->second;
        m_Context.Panels.Sequencer.PlayheadSeconds += deltaTime;
        if (m_Context.Panels.Sequencer.PlayheadSeconds > timeline.DurationSeconds) {
            if (m_Context.Panels.Sequencer.LoopPlayback) {
                m_Context.Panels.Sequencer.PlayheadSeconds = 0.0f;
            } else {
                m_Context.Panels.Sequencer.PlayheadSeconds = timeline.DurationSeconds;
                m_Context.Panels.Sequencer.IsPlaying = false;
            }
        }

        EvaluateTimelineAtTime(
            m_Context,
            timeline.Guid,
            m_Context.Panels.Sequencer.PlayheadSeconds);
    }

    void EditorModule::RenderPanels() {
        if (!m_Enabled || !m_Context.Initialized) {
            return;
        }

        ImGui::DockSpaceOverViewport(0U, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Editor")) {
                if (ImGui::MenuItem("Hierarchy", nullptr, m_Context.Panels.Hierarchy.Open)) {
                    m_Context.Panels.Hierarchy.Open = !m_Context.Panels.Hierarchy.Open;
                }
                if (ImGui::MenuItem("Inspector", nullptr, m_Context.Panels.Inspector.Open)) {
                    m_Context.Panels.Inspector.Open = !m_Context.Panels.Inspector.Open;
                }
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_Context.History.CanUndo())) {
                    UndoEditorAction(m_Context);
                }
                if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_Context.History.CanRedo())) {
                    RedoEditorAction(m_Context);
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Authoring")) {
                if (ImGui::MenuItem("Visual Scripting")) {
                    const std::string guid = m_Context.Panels.VisualScripting.ActiveGraphGuid.empty()
                        ? "default-graph"
                        : m_Context.Panels.VisualScripting.ActiveGraphGuid;
                    OpenVisualScriptingGraphEditor(m_Context, guid);
                }
                if (ImGui::MenuItem("Sequencer")) {
                    const std::string guid = m_Context.Panels.Sequencer.ActiveTimelineGuid.empty()
                        ? "default-timeline"
                        : m_Context.Panels.Sequencer.ActiveTimelineGuid;
                    OpenCinematicSequencerEditor(m_Context, guid);
                }
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        OpenSceneHierarchyPanel(m_Context);
        OpenInspectorPanel(m_Context);
        DrawVisualScriptingEditor(m_Context);
        DrawSequencerEditor(m_Context);
    }

} // namespace Editor
} // namespace Core

