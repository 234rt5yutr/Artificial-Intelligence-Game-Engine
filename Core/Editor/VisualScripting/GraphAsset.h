#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Core {
namespace Editor {

    using EditorJson = nlohmann::json;

    struct GraphNode {
        std::string NodeId;
        std::string NodeType;
        EditorJson Data = EditorJson::object();
    };

    struct GraphEdge {
        std::string EdgeId;
        std::string SourceNodeId;
        std::string TargetNodeId;
        std::string SourcePin;
        std::string TargetPin;
    };

    struct VisualScriptGraphAsset {
        std::string Guid;
        uint32_t SchemaVersion = 1;
        std::string EntryNodeId;
        std::vector<GraphNode> Nodes;
        std::vector<GraphEdge> Edges;
        EditorJson CompileOptions = EditorJson::object();
    };

    struct VisualScriptRuntimeCache {
        std::unordered_map<std::string, VisualScriptGraphAsset> Graphs;
    };

} // namespace Editor
} // namespace Core

