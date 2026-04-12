#pragma once

#include "Commands/CommandStack.h"
#include "EditorState.h"
#include "Prefab/PrefabAsset.h"
#include "Sequencer/TimelineAsset.h"
#include "VisualScripting/GraphAsset.h"

#include "Core/ECS/Scene.h"
#include "Core/Math/Math.h"

#include <nlohmann/json.hpp>

#include <entt/entt.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace Core {
namespace Editor {

    using EditorJson = nlohmann::json;

    struct PropertyEditRequest {
        entt::entity Entity = entt::null;
        std::string ComponentType;
        std::string PropertyPath;
        EditorJson NewValue = EditorJson::object();
        std::string TransactionLabel;
    };

    struct PropertyEditResult {
        bool Success = false;
        bool ValueChanged = false;
        bool DirtyStateChanged = false;
        std::string Error;
        std::string TransactionLabel;
    };

    struct SpawnOptions {
        Math::Vec3 Position{0.0f, 0.0f, 0.0f};
        Math::Vec3 Rotation{0.0f, 0.0f, 0.0f};
        Math::Vec3 Scale{1.0f, 1.0f, 1.0f};
        entt::entity Parent = entt::null;
        bool SelectAfterSpawn = true;
    };

    struct VariantOptions {
        std::string DisplayName;
        std::string OutputPath;
        EditorJson Overrides = EditorJson::array();
    };

    enum class ExecutionMode : uint8_t {
        OnStart = 0,
        OnEvent = 1,
        Manual = 2
    };

    struct CompiledGraphIR {
        std::string GraphGuid;
        uint64_t ContentHash = 0;
        std::vector<std::string> ExecutionOrder;
        std::vector<std::string> Diagnostics;
        bool IsValid = false;
    };

    struct TimelineClipCreateInfo {
        float StartTime = 0.0f;
        float EndTime = 0.0f;
        std::string ClipType;
        EditorJson Payload = EditorJson::object();
        float BlendInSeconds = 0.0f;
        float BlendOutSeconds = 0.0f;
        std::string Easing = "linear";
    };

    struct TimelineEvaluationResult {
        bool Valid = false;
        float TimeSeconds = 0.0f;
        std::vector<std::string> ActiveTrackIds;
        std::vector<std::string> ActiveClipIds;
        EditorJson ResolvedPayload = EditorJson::object();
    };

    struct TakeRecordOptions {
        float DurationSeconds = 3.0f;
        float FrameRate = 30.0f;
        bool OverwriteExisting = false;
        std::string ClipType = "animation_take";
    };

    struct EditorContext {
        ECS::Scene* ActiveScene = nullptr;
        EntitySelectionState Selection;
        EditorPanelState Panels;
        Commands::CommandStack History;
        PrefabRuntimeCache Prefabs;
        VisualScriptRuntimeCache GraphRuntime;
        SequencerRuntimeCache Sequencer;
        std::unordered_map<std::string, CompiledGraphIR> CompiledGraphs;
        std::vector<std::string> Diagnostics;
        bool IsPlayMode = false;
        bool Dirty = false;
        bool Initialized = false;
    };

} // namespace Editor
} // namespace Core

