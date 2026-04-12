#pragma once

#include "EditorContext.h"
#include "Result.h"

#include <string_view>

namespace Core {
namespace Editor {

    // Step 21.1
    void OpenSceneHierarchyPanel(EditorContext& ctx);
    void OpenInspectorPanel(EditorContext& ctx);
    bool SelectEntityInEditor(EditorContext& ctx, entt::entity entity, const char* source);
    PropertyEditResult EditComponentPropertiesInEditor(EditorContext& ctx, const PropertyEditRequest& req);

    // Step 21.2
    bool UndoEditorAction(EditorContext& ctx);
    bool RedoEditorAction(EditorContext& ctx);

    // Step 21.3
    Result<std::string> CreatePrefabAsset(EditorContext& ctx, entt::entity rootEntity, std::string_view outputPath);
    Result<entt::entity> InstantiatePrefabAsset(EditorContext& ctx, std::string_view prefabGuid, const SpawnOptions& options);
    Result<void> ApplyPrefabOverrides(EditorContext& ctx, std::string_view instanceGuid);
    Result<std::string> CreatePrefabVariant(EditorContext& ctx, std::string_view parentPrefabGuid, const VariantOptions& options);

    // Step 21.4
    void OpenVisualScriptingGraphEditor(EditorContext& ctx, std::string_view graphGuid);
    Result<CompiledGraphIR> CompileVisualScriptingGraph(EditorContext& ctx, std::string_view graphGuid);
    Result<void> ExecuteVisualScriptingGraph(EditorContext& ctx, std::string_view graphGuid, ExecutionMode mode);

    // Step 21.5
    void OpenCinematicSequencerEditor(EditorContext& ctx, std::string_view timelineGuid);
    Result<std::string> AddTimelineTrack(EditorContext& ctx, std::string_view timelineGuid, TrackType type, std::string_view displayName);
    Result<std::string> AddTimelineClip(EditorContext& ctx, std::string_view trackId, const TimelineClipCreateInfo& clipInfo);
    Result<TimelineEvaluationResult> EvaluateTimelineAtTime(EditorContext& ctx, std::string_view timelineGuid, float timeSeconds);
    Result<std::string> RecordAnimationTake(EditorContext& ctx, std::string_view timelineGuid, entt::entity sourceEntity, const TakeRecordOptions& options);

} // namespace Editor
} // namespace Core

