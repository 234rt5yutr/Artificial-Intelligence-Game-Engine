#include "Core/Animation/Blend/AnimationBlendTreeBuilder.h"

#include <algorithm>
#include <functional>
#include <utility>
#include <unordered_map>
#include <unordered_set>

namespace Core::Animation {

namespace {

void PushDiagnostic(std::vector<AnimationDiagnostic>& diagnostics,
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

bool ValidateBlendTree(
    const AnimationBlendTreeBuildRequest& request,
    std::vector<AnimationDiagnostic>& diagnostics) {
    diagnostics.clear();

    if (request.TreeName.empty()) {
        PushDiagnostic(
            diagnostics,
            "ANIM_BLENDTREE_NAME_MISSING",
            "TreeName must not be empty.",
            DiagnosticSeverity::Error);
    }

    if (request.Nodes.empty()) {
        PushDiagnostic(
            diagnostics,
            "ANIM_BLENDTREE_NO_NODES",
            "Blend tree must include at least one node.",
            DiagnosticSeverity::Error);
    }

    if (request.RootNodeId.empty()) {
        PushDiagnostic(
            diagnostics,
            "ANIM_BLENDTREE_INVALID_ROOT",
            "RootNodeId must not be empty.",
            DiagnosticSeverity::Error);
    }

    std::unordered_map<std::string, const AnimationBlendTreeNodeDefinition*> nodesById;
    nodesById.reserve(request.Nodes.size());
    for (const AnimationBlendTreeNodeDefinition& node : request.Nodes) {
        if (node.Id.empty()) {
            PushDiagnostic(
                diagnostics,
                "ANIM_BLENDTREE_NODE_ID_MISSING",
                "Blend tree node id must not be empty.",
                DiagnosticSeverity::Error);
            continue;
        }
        if (!nodesById.emplace(node.Id, &node).second) {
            PushDiagnostic(
                diagnostics,
                "ANIM_BLENDTREE_NODE_DUPLICATE",
                "Duplicate blend tree node id detected.",
                DiagnosticSeverity::Error,
                node.Id);
        }
    }

    if (!request.RootNodeId.empty() && nodesById.find(request.RootNodeId) == nodesById.end()) {
        PushDiagnostic(
            diagnostics,
            "ANIM_BLENDTREE_INVALID_ROOT",
            "RootNodeId was not found in node set.",
            DiagnosticSeverity::Error,
            request.RootNodeId);
    }

    for (const AnimationBlendTreeNodeDefinition& node : request.Nodes) {
        if (node.Type != AnimationBlendTreeNodeType::Clip && node.Children.empty()) {
            PushDiagnostic(
                diagnostics,
                "ANIM_BLENDTREE_NODE_CHILDREN_MISSING",
                "Blend node requires at least one child.",
                DiagnosticSeverity::Error,
                node.Id);
        }
        for (const std::string& child : node.Children) {
            if (nodesById.find(child) == nodesById.end()) {
                PushDiagnostic(
                    diagnostics,
                    "ANIM_BLENDTREE_NODE_CHILD_UNKNOWN",
                    "Blend node references an unknown child.",
                    DiagnosticSeverity::Error,
                    node.Id + " -> " + child);
            }
        }
    }

    if (HasError(diagnostics)) {
        return false;
    }

    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> recursion;
    bool cycleDetected = false;

    const std::function<void(const std::string&)> dfs = [&](const std::string& nodeId) {
        if (cycleDetected) {
            return;
        }
        visited.insert(nodeId);
        recursion.insert(nodeId);

        const auto* node = nodesById.at(nodeId);
        std::vector<std::string> orderedChildren = node->Children;
        std::sort(orderedChildren.begin(), orderedChildren.end());
        for (const std::string& child : orderedChildren) {
            if (recursion.find(child) != recursion.end()) {
                cycleDetected = true;
                PushDiagnostic(
                    diagnostics,
                    "ANIM_BLENDTREE_CYCLE_DETECTED",
                    "Cycle detected in blend tree node graph.",
                    DiagnosticSeverity::Error,
                    nodeId + " -> " + child);
                return;
            }
            if (visited.find(child) == visited.end()) {
                dfs(child);
                if (cycleDetected) {
                    return;
                }
            }
        }

        recursion.erase(nodeId);
    };

    std::vector<std::string> orderedNodeIds;
    orderedNodeIds.reserve(nodesById.size());
    for (const auto& [nodeId, _] : nodesById) {
        orderedNodeIds.push_back(nodeId);
    }
    std::sort(orderedNodeIds.begin(), orderedNodeIds.end());
    for (const std::string& nodeId : orderedNodeIds) {
        if (visited.find(nodeId) == visited.end()) {
            dfs(nodeId);
            if (cycleDetected) {
                break;
            }
        }
    }

    return !HasError(diagnostics);
}

} // namespace

AnimationBlendTreeBuildResult CreateAnimationBlendTree(
    const AnimationBlendTreeBuildRequest& request) {
    AnimationBlendTreeBuildResult result;
    if (!ValidateBlendTree(request, result.Diagnostics)) {
        result.Success = false;
        return result;
    }

    AnimationBlendTree blendTree;
    blendTree.TreeName = request.TreeName;
    blendTree.ParameterX = request.ParameterX;
    blendTree.ParameterY = request.ParameterY;
    blendTree.MinX = request.MinX;
    blendTree.MaxX = request.MaxX;
    blendTree.MinY = request.MinY;
    blendTree.MaxY = request.MaxY;

    std::vector<AnimationBlendTreeNodeDefinition> orderedNodes = request.Nodes;
    std::sort(
        orderedNodes.begin(),
        orderedNodes.end(),
        [](const AnimationBlendTreeNodeDefinition& lhs, const AnimationBlendTreeNodeDefinition& rhs) {
            return lhs.Id < rhs.Id;
        });

    blendTree.Nodes.reserve(orderedNodes.size());
    for (const AnimationBlendTreeNodeDefinition& node : orderedNodes) {
        AnimationBlendTreeRuntimeNode runtimeNode;
        runtimeNode.Id = node.Id;
        runtimeNode.Type = node.Type;
        runtimeNode.Clip = node.Clip;
        runtimeNode.PositionX = node.PositionX;
        runtimeNode.PositionY = node.PositionY;
        runtimeNode.AdditiveWeight = std::max(0.0f, node.AdditiveWeight);

        const uint32_t nodeIndex = static_cast<uint32_t>(blendTree.Nodes.size());
        blendTree.NodeIdToIndex[node.Id] = nodeIndex;
        blendTree.Nodes.push_back(std::move(runtimeNode));
    }

    for (const AnimationBlendTreeNodeDefinition& sourceNode : orderedNodes) {
        const uint32_t nodeIndex = blendTree.NodeIdToIndex[sourceNode.Id];
        AnimationBlendTreeRuntimeNode& runtimeNode = blendTree.Nodes[nodeIndex];

        std::vector<std::string> orderedChildren = sourceNode.Children;
        std::sort(orderedChildren.begin(), orderedChildren.end());
        runtimeNode.ChildIndices.reserve(orderedChildren.size());
        for (const std::string& childId : orderedChildren) {
            runtimeNode.ChildIndices.push_back(blendTree.NodeIdToIndex.at(childId));
        }
    }

    blendTree.RootNodeIndex = blendTree.NodeIdToIndex.at(request.RootNodeId);
    result.BlendTree = std::move(blendTree);
    result.Success = true;
    PushDiagnostic(
        result.Diagnostics,
        "ANIM_BLENDTREE_BUILD_SUCCESS",
        "Blend tree compiled with deterministic node and child ordering.",
        DiagnosticSeverity::Info,
        request.TreeName);
    return result;
}

} // namespace Core::Animation
