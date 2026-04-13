#include "Core/Animation/Graph/AnimationGraphValidator.h"

#include <algorithm>
#include <functional>
#include <utility>
#include <unordered_map>
#include <unordered_set>

namespace Core::Animation {

namespace {

void AppendDiagnostic(std::vector<AnimationDiagnostic>& diagnostics,
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

} // namespace

bool ValidateAnimationGraphBuildRequest(
    const AnimationGraphBuildRequest& request,
    std::vector<AnimationDiagnostic>& diagnostics) {
    diagnostics.clear();

    if (request.GraphName.empty()) {
        AppendDiagnostic(
            diagnostics,
            "ANIM_GRAPH_NAME_MISSING",
            "GraphName must not be empty.",
            DiagnosticSeverity::Error);
    }

    if (request.DefaultState.empty()) {
        AppendDiagnostic(
            diagnostics,
            "ANIM_GRAPH_DEFAULT_STATE_MISSING",
            "DefaultState must not be empty.",
            DiagnosticSeverity::Error);
    }

    if (request.States.empty()) {
        AppendDiagnostic(
            diagnostics,
            "ANIM_GRAPH_NO_STATES",
            "Graph must define at least one state.",
            DiagnosticSeverity::Error);
    }

    std::unordered_set<std::string> layerNames;
    std::unordered_set<int32_t> layerIndices;
    for (const AnimationGraphLayerDefinition& layer : request.Layers) {
        if (layer.Name.empty()) {
            AppendDiagnostic(
                diagnostics,
                "ANIM_GRAPH_LAYER_NAME_MISSING",
                "Layer name must not be empty.",
                DiagnosticSeverity::Error);
            continue;
        }

        if (!layerNames.insert(layer.Name).second) {
            AppendDiagnostic(
                diagnostics,
                "ANIM_GRAPH_LAYER_DUPLICATE",
                "Duplicate layer name detected.",
                DiagnosticSeverity::Error,
                layer.Name);
        }

        if (!layerIndices.insert(layer.LayerIndex).second) {
            AppendDiagnostic(
                diagnostics,
                "ANIM_GRAPH_LAYER_INDEX_DUPLICATE",
                "Duplicate layer index detected.",
                DiagnosticSeverity::Error,
                std::to_string(layer.LayerIndex));
        }
    }

    std::unordered_set<std::string> stateIds;
    for (const AnimationGraphStateDefinition& state : request.States) {
        if (state.Id.empty()) {
            AppendDiagnostic(
                diagnostics,
                "ANIM_GRAPH_STATE_ID_MISSING",
                "State id must not be empty.",
                DiagnosticSeverity::Error);
            continue;
        }

        if (!stateIds.insert(state.Id).second) {
            AppendDiagnostic(
                diagnostics,
                "ANIM_GRAPH_STATE_DUPLICATE",
                "Duplicate state id detected.",
                DiagnosticSeverity::Error,
                state.Id);
        }
    }

    if (!request.DefaultState.empty() && stateIds.find(request.DefaultState) == stateIds.end()) {
        AppendDiagnostic(
            diagnostics,
            "ANIM_GRAPH_DEFAULT_STATE_UNKNOWN",
            "DefaultState does not exist in state list.",
            DiagnosticSeverity::Error,
            request.DefaultState);
    }

    std::unordered_map<std::string, std::vector<std::string>> adjacency;
    adjacency.reserve(request.States.size());
    for (const AnimationGraphStateDefinition& state : request.States) {
        adjacency[state.Id] = {};
    }

    for (const AnimationGraphTransitionDefinition& transition : request.Transitions) {
        if (stateIds.find(transition.SourceState) == stateIds.end()) {
            AppendDiagnostic(
                diagnostics,
                "ANIM_GRAPH_STATE_MISSING",
                "Transition source state is missing.",
                DiagnosticSeverity::Error,
                transition.SourceState);
            continue;
        }
        if (stateIds.find(transition.TargetState) == stateIds.end()) {
            AppendDiagnostic(
                diagnostics,
                "ANIM_GRAPH_STATE_MISSING",
                "Transition target state is missing.",
                DiagnosticSeverity::Error,
                transition.TargetState);
            continue;
        }

        adjacency[transition.SourceState].push_back(transition.TargetState);
    }

    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> recursion;
    std::vector<std::string> stack;
    bool cycleDetected = false;

    const std::function<void(const std::string&)> dfs = [&](const std::string& node) {
        if (cycleDetected) {
            return;
        }

        visited.insert(node);
        recursion.insert(node);
        stack.push_back(node);

        const auto adjacencyIt = adjacency.find(node);
        if (adjacencyIt != adjacency.end()) {
            std::vector<std::string> sortedEdges = adjacencyIt->second;
            std::sort(sortedEdges.begin(), sortedEdges.end());
            for (const std::string& next : sortedEdges) {
                if (recursion.find(next) != recursion.end()) {
                    cycleDetected = true;
                    std::string cycleContext;
                    for (const std::string& step : stack) {
                        if (!cycleContext.empty()) {
                            cycleContext += " -> ";
                        }
                        cycleContext += step;
                    }
                    cycleContext += " -> " + next;
                    AppendDiagnostic(
                        diagnostics,
                        "ANIM_GRAPH_CYCLE_DETECTED",
                        "Cycle detected in graph transitions.",
                        DiagnosticSeverity::Error,
                        cycleContext);
                    return;
                }
                if (visited.find(next) == visited.end()) {
                    dfs(next);
                    if (cycleDetected) {
                        return;
                    }
                }
            }
        }

        recursion.erase(node);
        stack.pop_back();
    };

    std::vector<std::string> orderedStates;
    orderedStates.reserve(adjacency.size());
    for (const auto& [stateId, _] : adjacency) {
        orderedStates.push_back(stateId);
    }
    std::sort(orderedStates.begin(), orderedStates.end());
    for (const std::string& stateId : orderedStates) {
        if (visited.find(stateId) == visited.end()) {
            dfs(stateId);
            if (cycleDetected) {
                break;
            }
        }
    }

    if (!HasError(diagnostics)) {
        AppendDiagnostic(
            diagnostics,
            "ANIM_GRAPH_VALIDATED",
            "Animation graph request validated successfully.",
            DiagnosticSeverity::Info,
            request.GraphName);
    }

    return !HasError(diagnostics);
}

} // namespace Core::Animation
