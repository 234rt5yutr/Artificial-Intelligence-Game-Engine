#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace Core {
namespace Editor {

    struct EntitySelectionState {
        entt::entity Primary = entt::null;
        std::vector<entt::entity> SelectionSet;
        std::string Source = "none";
        bool Dirty = false;
    };

    struct SceneHierarchyPanelState {
        bool Open = true;
        bool FocusSelection = true;
        std::string SearchQuery;
        std::unordered_set<uint32_t> ExpandedEntities;
    };

    struct InspectorPanelState {
        bool Open = true;
        bool LockSelection = false;
        entt::entity LockedEntity = entt::null;
    };

    struct VisualScriptingPanelState {
        bool Open = false;
        std::string ActiveGraphGuid;
    };

    struct SequencerPanelState {
        bool Open = false;
        std::string ActiveTimelineGuid;
        float PlayheadSeconds = 0.0f;
        bool IsPlaying = false;
        bool LoopPlayback = false;
    };

    struct EditorPanelState {
        SceneHierarchyPanelState Hierarchy;
        InspectorPanelState Inspector;
        VisualScriptingPanelState VisualScripting;
        SequencerPanelState Sequencer;
    };

} // namespace Editor
} // namespace Core

