#include "Core/Animation/Graph/AnimationStateMachineGraph.h"

#include "Core/Animation/Graph/AnimationGraphValidator.h"

#include <algorithm>
#include <utility>
#include <unordered_map>

namespace Core::Animation {

namespace {

void AddDiagnostic(std::vector<AnimationDiagnostic>& diagnostics,
                   const std::string& code,
                   const std::string& message,
                   const DiagnosticSeverity severity,
                   const std::string& context = {}) {
    AnimationDiagnostic diagnostic;
    diagnostic.Code = code;
    diagnostic.Message = message;
    diagnostic.Severity = severity;
    diagnostic.Context = context;
    diagnostics.push_back(std::move(diagnostic));
}

bool HasError(const std::vector<AnimationDiagnostic>& diagnostics) {
    return std::any_of(
        diagnostics.begin(),
        diagnostics.end(),
        [](const AnimationDiagnostic& diagnostic) {
            return diagnostic.Severity == DiagnosticSeverity::Error;
        });
}

std::string BuildTransitionId(const AnimationGraphTransitionDefinition& transition,
                              const uint32_t sequence) {
    if (!transition.Id.empty()) {
        return transition.Id;
    }
    return transition.SourceState + "->" + transition.TargetState + "#" + std::to_string(sequence);
}

} // namespace

AnimationGraphBuildResult CreateAnimationStateMachineGraph(
    const AnimationGraphBuildRequest& request) {
    AnimationGraphBuildResult result;

    std::vector<AnimationDiagnostic> validationDiagnostics;
    if (!ValidateAnimationGraphBuildRequest(request, validationDiagnostics)) {
        result.Diagnostics = std::move(validationDiagnostics);
        result.Success = false;
        return result;
    }

    result.Diagnostics = std::move(validationDiagnostics);

    AnimationStateMachineGraph graph;
    graph.GraphName = request.GraphName;
    graph.DefaultStateId = request.DefaultState;

    graph.Layers.reserve(request.Layers.size());
    for (const AnimationGraphLayerDefinition& layer : request.Layers) {
        AnimationGraphCompiledLayer compiledLayer;
        compiledLayer.Name = layer.Name;
        compiledLayer.LayerIndex = layer.LayerIndex;
        compiledLayer.Additive = layer.Additive;
        compiledLayer.DefaultWeight = std::clamp(layer.DefaultWeight, 0.0f, 1.0f);
        graph.Layers.push_back(std::move(compiledLayer));
    }
    std::sort(
        graph.Layers.begin(),
        graph.Layers.end(),
        [](const AnimationGraphCompiledLayer& lhs, const AnimationGraphCompiledLayer& rhs) {
            if (lhs.LayerIndex != rhs.LayerIndex) {
                return lhs.LayerIndex < rhs.LayerIndex;
            }
            return lhs.Name < rhs.Name;
        });

    std::unordered_map<std::string, int32_t> layerIndexByName;
    layerIndexByName.reserve(graph.Layers.size());
    for (const AnimationGraphCompiledLayer& layer : graph.Layers) {
        layerIndexByName[layer.Name] = layer.LayerIndex;
    }

    graph.States.reserve(request.States.size());
    for (const AnimationGraphStateDefinition& state : request.States) {
        int32_t layerIndex = 0;
        if (!state.LayerName.empty()) {
            const auto layerIt = layerIndexByName.find(state.LayerName);
            if (layerIt != layerIndexByName.end()) {
                layerIndex = layerIt->second;
            } else {
                AddDiagnostic(
                    result.Diagnostics,
                    "ANIM_GRAPH_LAYER_MISSING",
                    "State references a layer that does not exist; defaulting to base layer.",
                    DiagnosticSeverity::Warning,
                    state.LayerName);
            }
        }

        AnimationGraphCompiledState compiledState;
        compiledState.Id = state.Id;
        compiledState.Clip = state.Clip;
        compiledState.LayerIndex = layerIndex;
        compiledState.Speed = state.Speed;
        compiledState.Loop = state.Loop;
        compiledState.BlendTreeId = state.BlendTreeId;
        graph.States.push_back(std::move(compiledState));
    }

    std::sort(
        graph.States.begin(),
        graph.States.end(),
        [](const AnimationGraphCompiledState& lhs, const AnimationGraphCompiledState& rhs) {
            if (lhs.LayerIndex != rhs.LayerIndex) {
                return lhs.LayerIndex < rhs.LayerIndex;
            }
            return lhs.Id < rhs.Id;
        });

    std::unordered_map<std::string, uint32_t> stateIndexById;
    stateIndexById.reserve(graph.States.size());
    for (uint32_t index = 0; index < static_cast<uint32_t>(graph.States.size()); ++index) {
        stateIndexById[graph.States[index].Id] = index;
    }

    const auto defaultStateIt = stateIndexById.find(graph.DefaultStateId);
    if (defaultStateIt == stateIndexById.end()) {
        AddDiagnostic(
            result.Diagnostics,
            "ANIM_GRAPH_DEFAULT_STATE_MISSING",
            "Compiled graph default state does not exist.",
            DiagnosticSeverity::Error,
            graph.DefaultStateId);
        result.Success = false;
        return result;
    }
    graph.DefaultStateIndex = defaultStateIt->second;

    graph.Transitions.reserve(request.Transitions.size());
    uint32_t transitionSequence = 0;
    for (const AnimationGraphTransitionDefinition& transition : request.Transitions) {
        const auto sourceIt = stateIndexById.find(transition.SourceState);
        const auto targetIt = stateIndexById.find(transition.TargetState);
        if (sourceIt == stateIndexById.end() || targetIt == stateIndexById.end()) {
            continue;
        }

        AnimationGraphCompiledTransition compiledTransition;
        compiledTransition.Id = BuildTransitionId(transition, transitionSequence++);
        compiledTransition.SourceStateIndex = sourceIt->second;
        compiledTransition.TargetStateIndex = targetIt->second;
        compiledTransition.Duration = std::max(0.0f, transition.Duration);
        compiledTransition.CurveType = transition.CurveType;
        compiledTransition.Priority = transition.Priority;
        compiledTransition.Conditions = transition.Conditions;
        graph.Transitions.push_back(std::move(compiledTransition));
    }

    std::sort(
        graph.Transitions.begin(),
        graph.Transitions.end(),
        [&](const AnimationGraphCompiledTransition& lhs, const AnimationGraphCompiledTransition& rhs) {
            if (lhs.SourceStateIndex != rhs.SourceStateIndex) {
                return lhs.SourceStateIndex < rhs.SourceStateIndex;
            }
            if (lhs.Priority != rhs.Priority) {
                return lhs.Priority > rhs.Priority;
            }
            const std::string& lhsTargetId = graph.States[lhs.TargetStateIndex].Id;
            const std::string& rhsTargetId = graph.States[rhs.TargetStateIndex].Id;
            if (lhsTargetId != rhsTargetId) {
                return lhsTargetId < rhsTargetId;
            }
            return lhs.Id < rhs.Id;
        });

    for (uint32_t transitionIndex = 0;
         transitionIndex < static_cast<uint32_t>(graph.Transitions.size());
         ++transitionIndex) {
        const uint32_t sourceIndex = graph.Transitions[transitionIndex].SourceStateIndex;
        graph.OutgoingTransitions[sourceIndex].push_back(transitionIndex);
    }

    AddDiagnostic(
        result.Diagnostics,
        "ANIM_GRAPH_COMPILE_SUCCESS",
        "Animation graph compiled with deterministic state and transition ordering.",
        DiagnosticSeverity::Info,
        request.GraphName);

    result.Graph = std::move(graph);
    result.Success = !HasError(result.Diagnostics);
    return result;
}

} // namespace Core::Animation
