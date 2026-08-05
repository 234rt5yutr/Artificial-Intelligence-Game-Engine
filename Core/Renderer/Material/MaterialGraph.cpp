#include "MaterialGraph.h"

#include "Core/Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <sstream>

namespace Core {
namespace Renderer {

    namespace {

        constexpr uint8_t kOutputSlotCount = static_cast<uint8_t>(MaterialOutputSlot::Count);

        struct NodeTypeInfo {
            const char* Name;
            uint8_t InputCount;
        };

        NodeTypeInfo InfoFor(MaterialNodeType type) {
            switch (type) {
                case MaterialNodeType::ConstantScalar: return {"ConstantScalar", 0};
                case MaterialNodeType::ConstantColor:  return {"ConstantColor", 0};
                case MaterialNodeType::TextureSample:  return {"TextureSample", 1};
                case MaterialNodeType::UV:             return {"UV", 0};
                case MaterialNodeType::WorldPosition:  return {"WorldPosition", 0};
                case MaterialNodeType::WorldNormal:    return {"WorldNormal", 0};
                case MaterialNodeType::VertexColor:    return {"VertexColor", 0};
                case MaterialNodeType::Time:           return {"Time", 0};
                case MaterialNodeType::CameraVector:   return {"CameraVector", 0};
                case MaterialNodeType::Multiply:       return {"Multiply", 2};
                case MaterialNodeType::Add:            return {"Add", 2};
                case MaterialNodeType::Subtract:       return {"Subtract", 2};
                case MaterialNodeType::Lerp:           return {"Lerp", 3};
                case MaterialNodeType::DotProduct:     return {"DotProduct", 2};
                case MaterialNodeType::OneMinus:       return {"OneMinus", 1};
                case MaterialNodeType::Saturate:       return {"Saturate", 1};
                case MaterialNodeType::Power:          return {"Power", 2};
                case MaterialNodeType::Fresnel:        return {"Fresnel", 1};
                case MaterialNodeType::Panner:         return {"Panner", 1};
                case MaterialNodeType::Normalize:      return {"Normalize", 1};
                case MaterialNodeType::Split:          return {"Split", 1};
                case MaterialNodeType::Output:         return {"Output", kOutputSlotCount};
            }
            return {"Unknown", 0};
        }

        bool TypeFromName(const std::string& name, MaterialNodeType& out) {
            static const std::unordered_map<std::string, MaterialNodeType> kMap = {
                {"ConstantScalar", MaterialNodeType::ConstantScalar},
                {"ConstantColor",  MaterialNodeType::ConstantColor},
                {"TextureSample",  MaterialNodeType::TextureSample},
                {"UV",             MaterialNodeType::UV},
                {"WorldPosition",  MaterialNodeType::WorldPosition},
                {"WorldNormal",    MaterialNodeType::WorldNormal},
                {"VertexColor",    MaterialNodeType::VertexColor},
                {"Time",           MaterialNodeType::Time},
                {"CameraVector",   MaterialNodeType::CameraVector},
                {"Multiply",       MaterialNodeType::Multiply},
                {"Add",            MaterialNodeType::Add},
                {"Subtract",       MaterialNodeType::Subtract},
                {"Lerp",           MaterialNodeType::Lerp},
                {"DotProduct",     MaterialNodeType::DotProduct},
                {"OneMinus",       MaterialNodeType::OneMinus},
                {"Saturate",       MaterialNodeType::Saturate},
                {"Power",          MaterialNodeType::Power},
                {"Fresnel",        MaterialNodeType::Fresnel},
                {"Panner",         MaterialNodeType::Panner},
                {"Normalize",      MaterialNodeType::Normalize},
                {"Split",          MaterialNodeType::Split},
                {"Output",         MaterialNodeType::Output},
            };
            auto it = kMap.find(name);
            if (it == kMap.end()) {
                return false;
            }
            out = it->second;
            return true;
        }

        std::string FloatLiteral(float value) {
            // GLSL rejects "1" where a float is required in some drivers'
            // strict paths, so always emit a decimal point.
            std::ostringstream stream;
            stream.setf(std::ios::fixed);
            stream.precision(6);
            stream << value;
            return stream.str();
        }

        std::string Vec4Literal(const Math::Vec4& v) {
            return "vec4(" + FloatLiteral(v.x) + ", " + FloatLiteral(v.y) + ", " +
                   FloatLiteral(v.z) + ", " + FloatLiteral(v.w) + ")";
        }

        uint64_t HashCombine(uint64_t seed, uint64_t value) {
            seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
            return seed;
        }

        uint64_t HashString(uint64_t seed, const std::string& text) {
            for (char c : text) {
                seed = HashCombine(seed, static_cast<uint64_t>(static_cast<unsigned char>(c)));
            }
            return seed;
        }

        uint64_t HashFloat(uint64_t seed, float value) {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            return HashCombine(seed, static_cast<uint64_t>(bits));
        }

        const char* OutputSlotName(uint8_t slot) {
            switch (static_cast<MaterialOutputSlot>(slot)) {
                case MaterialOutputSlot::BaseColor: return "BaseColor";
                case MaterialOutputSlot::Metallic:  return "Metallic";
                case MaterialOutputSlot::Roughness: return "Roughness";
                case MaterialOutputSlot::Emissive:  return "Emissive";
                case MaterialOutputSlot::Normal:    return "Normal";
                case MaterialOutputSlot::Opacity:   return "Opacity";
                default: return "Unknown";
            }
        }

    } // namespace

    // ========================================================================
    // Graph editing
    // ========================================================================

    uint32_t MaterialGraph::AddNode(MaterialNodeType type, const std::string& name) {
        MaterialNode node;
        node.Id = m_NextNodeId++;
        node.Type = type;
        node.Name = name.empty() ? InfoFor(type).Name : name;
        if (type == MaterialNodeType::ConstantScalar) {
            node.Value = Math::Vec4(0.0f);
        }
        m_Nodes.push_back(node);
        return node.Id;
    }

    bool MaterialGraph::RemoveNode(uint32_t nodeId) {
        auto it = std::find_if(m_Nodes.begin(), m_Nodes.end(),
                               [nodeId](const MaterialNode& n) { return n.Id == nodeId; });
        if (it == m_Nodes.end()) {
            return false;
        }
        m_Nodes.erase(it);
        m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
                                     [nodeId](const MaterialLink& link) {
                                         return link.FromNode == nodeId || link.ToNode == nodeId;
                                     }),
                      m_Links.end());
        return true;
    }

    MaterialNode* MaterialGraph::FindNode(uint32_t nodeId) {
        for (auto& node : m_Nodes) {
            if (node.Id == nodeId) {
                return &node;
            }
        }
        return nullptr;
    }

    const MaterialNode* MaterialGraph::FindNode(uint32_t nodeId) const {
        for (const auto& node : m_Nodes) {
            if (node.Id == nodeId) {
                return &node;
            }
        }
        return nullptr;
    }

    bool MaterialGraph::SetNodeValue(uint32_t nodeId, const Math::Vec4& value) {
        if (auto* node = FindNode(nodeId)) {
            node->Value = value;
            return true;
        }
        return false;
    }

    bool MaterialGraph::SetNodeTextureSlot(uint32_t nodeId, const std::string& slot) {
        auto* node = FindNode(nodeId);
        if (!node) {
            return false;
        }
        node->TextureSlot = slot;
        return true;
    }

    bool MaterialGraph::Connect(uint32_t fromNode, uint32_t toNode, uint8_t toSlot, std::string* error) {
        const MaterialNode* source = FindNode(fromNode);
        const MaterialNode* sink = FindNode(toNode);
        if (!source || !sink) {
            if (error) *error = "Connect: node id not found";
            return false;
        }
        if (fromNode == toNode) {
            if (error) *error = "Connect: a node cannot feed itself";
            return false;
        }
        if (toSlot >= InfoFor(sink->Type).InputCount) {
            if (error) {
                *error = std::string("Connect: ") + InfoFor(sink->Type).Name + " has no input slot " +
                         std::to_string(static_cast<int>(toSlot));
            }
            return false;
        }
        if (source->Type == MaterialNodeType::Output) {
            if (error) *error = "Connect: the Output node produces no value";
            return false;
        }

        Disconnect(toNode, toSlot);
        m_Links.push_back({fromNode, toNode, toSlot});

        // Reject the edge if it closed a cycle; Compile() would otherwise be the
        // first place a user found out, after they had built more on top of it.
        MaterialGraph probe = *this;
        const auto result = probe.Compile();
        if (!result.Succeeded && result.Error.find("cycle") != std::string::npos) {
            m_Links.pop_back();
            if (error) *error = "Connect: would create a cycle";
            return false;
        }
        return true;
    }

    bool MaterialGraph::Disconnect(uint32_t toNode, uint8_t toSlot) {
        const auto before = m_Links.size();
        m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
                                     [toNode, toSlot](const MaterialLink& link) {
                                         return link.ToNode == toNode && link.ToSlot == toSlot;
                                     }),
                      m_Links.end());
        return m_Links.size() != before;
    }

    // ========================================================================
    // Codegen
    // ========================================================================

    MaterialCompileResult MaterialGraph::Compile() const {
        MaterialCompileResult result;

        const MaterialNode* outputNode = nullptr;
        for (const auto& node : m_Nodes) {
            if (node.Type == MaterialNodeType::Output) {
                if (outputNode) {
                    result.Error = "Graph has more than one Output node";
                    return result;
                }
                outputNode = &node;
            }
        }
        if (!outputNode) {
            result.Error = "Graph has no Output node";
            return result;
        }

        // slot -> source node id, per sink node.
        std::unordered_map<uint32_t, std::vector<uint32_t>> inputs;
        for (const auto& node : m_Nodes) {
            inputs[node.Id].assign(InfoFor(node.Type).InputCount, 0u);
        }
        for (const auto& link : m_Links) {
            auto it = inputs.find(link.ToNode);
            if (it == inputs.end() || link.ToSlot >= it->second.size()) {
                result.Error = "Dangling link to node " + std::to_string(link.ToNode);
                return result;
            }
            it->second[link.ToSlot] = link.FromNode;
        }

        // Iterative depth-first post-order so a deep chain cannot blow the
        // stack, with an on-stack marker for cycle detection.
        enum class Mark : uint8_t { Unvisited = 0, InProgress, Done };
        std::unordered_map<uint32_t, Mark> marks;
        std::vector<uint32_t> order;
        std::vector<std::pair<uint32_t, std::size_t>> stack;

        stack.push_back({outputNode->Id, 0});
        marks[outputNode->Id] = Mark::InProgress;

        while (!stack.empty()) {
            auto& [nodeId, cursor] = stack.back();
            const MaterialNode* node = FindNode(nodeId);
            if (!node) {
                result.Error = "Graph references missing node " + std::to_string(nodeId);
                return result;
            }
            const auto& nodeInputs = inputs[nodeId];

            if (cursor < nodeInputs.size()) {
                const uint32_t sourceId = nodeInputs[cursor];
                ++cursor;
                if (sourceId == 0) {
                    continue; // unconnected slot: falls back to a literal below
                }
                const Mark mark = marks[sourceId];
                if (mark == Mark::InProgress) {
                    result.Error = "Graph contains a cycle through node " + std::to_string(sourceId);
                    return result;
                }
                if (mark == Mark::Unvisited) {
                    marks[sourceId] = Mark::InProgress;
                    stack.push_back({sourceId, 0});
                }
                continue;
            }

            marks[nodeId] = Mark::Done;
            order.push_back(nodeId);
            stack.pop_back();
        }

        std::ostringstream body;
        std::unordered_map<std::string, uint32_t> textureSlotIndex;
        uint64_t hash = 0xcbf29ce484222325ULL;

        auto expressionFor = [&](uint32_t sourceId, const char* fallback) -> std::string {
            if (sourceId == 0) {
                return fallback;
            }
            return "n" + std::to_string(sourceId);
        };

        for (uint32_t nodeId : order) {
            const MaterialNode* node = FindNode(nodeId);
            const auto& in = inputs[nodeId];
            const std::string var = "n" + std::to_string(nodeId);

            hash = HashCombine(hash, static_cast<uint64_t>(node->Type));
            hash = HashCombine(hash, nodeId);
            hash = HashFloat(hash, node->Value.x);
            hash = HashFloat(hash, node->Value.y);
            hash = HashFloat(hash, node->Value.z);
            hash = HashFloat(hash, node->Value.w);
            hash = HashString(hash, node->TextureSlot);
            for (uint32_t source : in) {
                hash = HashCombine(hash, source);
            }

            switch (node->Type) {
                case MaterialNodeType::ConstantScalar:
                    body << "    vec4 " << var << " = vec4(" << FloatLiteral(node->Value.x) << ");\n";
                    break;
                case MaterialNodeType::ConstantColor:
                    body << "    vec4 " << var << " = " << Vec4Literal(node->Value) << ";\n";
                    break;
                case MaterialNodeType::UV:
                    body << "    vec4 " << var << " = vec4(inTexCoord, 0.0, 0.0);\n";
                    break;
                case MaterialNodeType::WorldPosition:
                    body << "    vec4 " << var << " = vec4(inWorldPos, 1.0);\n";
                    break;
                case MaterialNodeType::WorldNormal:
                    body << "    vec4 " << var << " = vec4(normalize(inWorldNormal), 0.0);\n";
                    break;
                case MaterialNodeType::VertexColor:
                    // No vertex colour stream on Renderer::Vertex; white keeps
                    // graphs that use it neutral instead of black.
                    body << "    vec4 " << var << " = vec4(1.0);\n";
                    break;
                case MaterialNodeType::Time:
                    result.UsesTime = true;
                    body << "    vec4 " << var << " = vec4(uMaterial.TimeSeconds);\n";
                    break;
                case MaterialNodeType::CameraVector:
                    body << "    vec4 " << var
                         << " = vec4(normalize(uMaterial.CameraPosition.xyz - inWorldPos), 0.0);\n";
                    break;
                case MaterialNodeType::TextureSample: {
                    if (node->TextureSlot.empty()) {
                        body << "    vec4 " << var << " = " << Vec4Literal(node->Value) << ";\n";
                        break;
                    }
                    auto slotIt = textureSlotIndex.find(node->TextureSlot);
                    if (slotIt == textureSlotIndex.end()) {
                        const uint32_t index = static_cast<uint32_t>(result.TextureSlots.size());
                        result.TextureSlots.push_back(node->TextureSlot);
                        slotIt = textureSlotIndex.emplace(node->TextureSlot, index).first;
                    }
                    const std::string uv = in[0] == 0 ? "inTexCoord"
                                                      : (expressionFor(in[0], "vec4(0.0)") + ".xy");
                    body << "    vec4 " << var << " = texture(uMaterialTextures[" << slotIt->second
                         << "], " << uv << ");\n";
                    break;
                }
                case MaterialNodeType::Multiply:
                    body << "    vec4 " << var << " = " << expressionFor(in[0], "vec4(1.0)") << " * "
                         << expressionFor(in[1], "vec4(1.0)") << ";\n";
                    break;
                case MaterialNodeType::Add:
                    body << "    vec4 " << var << " = " << expressionFor(in[0], "vec4(0.0)") << " + "
                         << expressionFor(in[1], "vec4(0.0)") << ";\n";
                    break;
                case MaterialNodeType::Subtract:
                    body << "    vec4 " << var << " = " << expressionFor(in[0], "vec4(0.0)") << " - "
                         << expressionFor(in[1], "vec4(0.0)") << ";\n";
                    break;
                case MaterialNodeType::Lerp:
                    body << "    vec4 " << var << " = mix(" << expressionFor(in[0], "vec4(0.0)") << ", "
                         << expressionFor(in[1], "vec4(1.0)") << ", clamp("
                         << expressionFor(in[2], "vec4(0.5)") << ".x, 0.0, 1.0));\n";
                    break;
                case MaterialNodeType::DotProduct:
                    body << "    vec4 " << var << " = vec4(dot(" << expressionFor(in[0], "vec4(0.0)")
                         << ".xyz, " << expressionFor(in[1], "vec4(0.0)") << ".xyz));\n";
                    break;
                case MaterialNodeType::OneMinus:
                    body << "    vec4 " << var << " = vec4(1.0) - " << expressionFor(in[0], "vec4(0.0)") << ";\n";
                    break;
                case MaterialNodeType::Saturate:
                    body << "    vec4 " << var << " = clamp(" << expressionFor(in[0], "vec4(0.0)")
                         << ", vec4(0.0), vec4(1.0));\n";
                    break;
                case MaterialNodeType::Power:
                    // Negative bases make pow undefined in GLSL; clamp first.
                    body << "    vec4 " << var << " = pow(max(" << expressionFor(in[0], "vec4(0.0)")
                         << ", vec4(0.0)), vec4(" << expressionFor(in[1], "vec4(1.0)") << ".x));\n";
                    break;
                case MaterialNodeType::Fresnel:
                    body << "    vec4 " << var << " = vec4(pow(1.0 - clamp(dot(normalize(inWorldNormal), "
                         << "normalize(uMaterial.CameraPosition.xyz - inWorldPos)), 0.0, 1.0), max("
                         << expressionFor(in[0], "vec4(5.0)") << ".x, 0.001)));\n";
                    break;
                case MaterialNodeType::Panner:
                    result.UsesTime = true;
                    body << "    vec4 " << var << " = " << expressionFor(in[0], "vec4(inTexCoord, 0.0, 0.0)")
                         << " + vec4(" << FloatLiteral(node->Value.x) << ", " << FloatLiteral(node->Value.y)
                         << ", 0.0, 0.0) * uMaterial.TimeSeconds;\n";
                    break;
                case MaterialNodeType::Normalize:
                    body << "    vec4 " << var << " = vec4(normalize("
                         << expressionFor(in[0], "vec4(0.0, 1.0, 0.0, 0.0)") << ".xyz), 0.0);\n";
                    break;
                case MaterialNodeType::Split: {
                    const int channel = std::clamp(static_cast<int>(node->Value.x), 0, 3);
                    const char* swizzle[] = {"x", "y", "z", "w"};
                    body << "    vec4 " << var << " = vec4(" << expressionFor(in[0], "vec4(0.0)") << "."
                         << swizzle[channel] << ");\n";
                    break;
                }
                case MaterialNodeType::Output:
                    body << "    surf.BaseColor = " << expressionFor(in[0], "vec4(0.78, 0.78, 0.80, 1.0)") << ".rgb;\n";
                    body << "    surf.Metallic = clamp(" << expressionFor(in[1], "vec4(0.0)") << ".x, 0.0, 1.0);\n";
                    body << "    surf.Roughness = clamp(" << expressionFor(in[2], "vec4(0.6)") << ".x, 0.04, 1.0);\n";
                    body << "    surf.Emissive = " << expressionFor(in[3], "vec4(0.0)") << ".rgb;\n";
                    if (in[4] != 0) {
                        // Tangent-space normal map, expanded from [0,1].
                        body << "    surf.TangentNormal = normalize(" << expressionFor(in[4], "vec4(0.5, 0.5, 1.0, 0.0)")
                             << ".xyz * 2.0 - 1.0);\n";
                        body << "    surf.HasTangentNormal = true;\n";
                    }
                    body << "    surf.Opacity = clamp(" << expressionFor(in[5], "vec4(1.0)") << ".x, 0.0, 1.0);\n";
                    break;
            }
        }

        result.FragmentBody = body.str();
        result.Hash = hash;
        result.Succeeded = true;
        return result;
    }

    MaterialGraph MaterialGraph::MakeDefault(const std::string& name,
                                             const Math::Vec3& baseColor,
                                             float metallic,
                                             float roughness) {
        MaterialGraph graph(name);
        const uint32_t colorNode = graph.AddNode(MaterialNodeType::ConstantColor, "BaseColor");
        graph.SetNodeValue(colorNode, Math::Vec4(baseColor, 1.0f));
        const uint32_t metallicNode = graph.AddNode(MaterialNodeType::ConstantScalar, "Metallic");
        graph.SetNodeValue(metallicNode, Math::Vec4(metallic));
        const uint32_t roughnessNode = graph.AddNode(MaterialNodeType::ConstantScalar, "Roughness");
        graph.SetNodeValue(roughnessNode, Math::Vec4(roughness));
        const uint32_t output = graph.AddNode(MaterialNodeType::Output, "Output");

        graph.Connect(colorNode, output, static_cast<uint8_t>(MaterialOutputSlot::BaseColor));
        graph.Connect(metallicNode, output, static_cast<uint8_t>(MaterialOutputSlot::Metallic));
        graph.Connect(roughnessNode, output, static_cast<uint8_t>(MaterialOutputSlot::Roughness));
        return graph;
    }

    // ========================================================================
    // Serialization
    // ========================================================================

    std::string MaterialGraph::ToJson() const {
        nlohmann::json root;
        root["name"] = m_Name;
        root["nodes"] = nlohmann::json::array();
        for (const auto& node : m_Nodes) {
            nlohmann::json nodeJson;
            nodeJson["id"] = node.Id;
            nodeJson["type"] = InfoFor(node.Type).Name;
            nodeJson["name"] = node.Name;
            nodeJson["value"] = {node.Value.x, node.Value.y, node.Value.z, node.Value.w};
            if (!node.TextureSlot.empty()) {
                nodeJson["texture"] = node.TextureSlot;
            }
            root["nodes"].push_back(nodeJson);
        }
        root["links"] = nlohmann::json::array();
        for (const auto& link : m_Links) {
            root["links"].push_back({{"from", link.FromNode}, {"to", link.ToNode}, {"slot", link.ToSlot}});
        }
        return root.dump(2);
    }

    bool MaterialGraph::FromJson(const std::string& json, MaterialGraph& out, std::string* error) {
        nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
            if (error) *error = "Material graph JSON is not a valid object";
            return false;
        }

        MaterialGraph graph(root.value("name", std::string("Material")));
        // Ids come from the document, so keep the allocator ahead of the
        // highest one rather than renumbering and invalidating the links.
        uint32_t highestId = 0;

        if (root.contains("nodes") && root["nodes"].is_array()) {
            for (const auto& nodeJson : root["nodes"]) {
                MaterialNode node;
                node.Id = nodeJson.value("id", 0u);
                if (node.Id == 0) {
                    if (error) *error = "Node ids must be non-zero";
                    return false;
                }
                if (!TypeFromName(nodeJson.value("type", std::string()), node.Type)) {
                    if (error) *error = "Unknown node type '" + nodeJson.value("type", std::string()) + "'";
                    return false;
                }
                node.Name = nodeJson.value("name", std::string(InfoFor(node.Type).Name));
                if (nodeJson.contains("value") && nodeJson["value"].is_array() && nodeJson["value"].size() == 4) {
                    node.Value = Math::Vec4(nodeJson["value"][0].get<float>(),
                                            nodeJson["value"][1].get<float>(),
                                            nodeJson["value"][2].get<float>(),
                                            nodeJson["value"][3].get<float>());
                }
                node.TextureSlot = nodeJson.value("texture", std::string());
                highestId = std::max(highestId, node.Id);
                graph.m_Nodes.push_back(node);
            }
        }

        if (root.contains("links") && root["links"].is_array()) {
            for (const auto& linkJson : root["links"]) {
                MaterialLink link;
                link.FromNode = linkJson.value("from", 0u);
                link.ToNode = linkJson.value("to", 0u);
                link.ToSlot = static_cast<uint8_t>(linkJson.value("slot", 0u));
                if (!graph.FindNode(link.FromNode) || !graph.FindNode(link.ToNode)) {
                    if (error) *error = "Link references a node that is not in the document";
                    return false;
                }
                graph.m_Links.push_back(link);
            }
        }

        graph.m_NextNodeId = highestId + 1;

        const auto compiled = graph.Compile();
        if (!compiled.Succeeded) {
            if (error) *error = compiled.Error;
            return false;
        }

        out = std::move(graph);
        return true;
    }

    // ========================================================================
    // Library
    // ========================================================================

    MaterialLibrary& MaterialLibrary::Get() {
        static MaterialLibrary instance;
        return instance;
    }

    uint32_t MaterialLibrary::GetOrCreateDefault() {
        if (!m_Materials.empty()) {
            return 0;
        }
        const uint32_t index = CreateMaterial("Default");
        MaterialInstance* material = GetMaterial(index);
        material->Graph = MaterialGraph::MakeDefault("Default", Math::Vec3(0.78f, 0.78f, 0.80f), 0.0f, 0.6f);
        material->Dirty = true;
        CompileDirty();
        return index;
    }

    uint32_t MaterialLibrary::CreateMaterial(const std::string& name) {
        auto existing = m_NameToIndex.find(name);
        if (existing != m_NameToIndex.end()) {
            return existing->second;
        }

        const uint32_t index = static_cast<uint32_t>(m_Materials.size());
        MaterialInstance material;
        material.Name = name;
        material.Graph = MaterialGraph::MakeDefault(name, Math::Vec3(0.78f, 0.78f, 0.80f), 0.0f, 0.6f);
        material.Dirty = true;
        m_Materials.push_back(std::move(material));
        m_NameToIndex[name] = index;
        ++m_Revision;
        return index;
    }

    uint32_t MaterialLibrary::FindMaterial(const std::string& name) const {
        auto it = m_NameToIndex.find(name);
        return it == m_NameToIndex.end() ? UINT32_MAX : it->second;
    }

    MaterialInstance* MaterialLibrary::GetMaterial(uint32_t index) {
        return index < m_Materials.size() ? &m_Materials[index] : nullptr;
    }

    const MaterialInstance* MaterialLibrary::GetMaterial(uint32_t index) const {
        return index < m_Materials.size() ? &m_Materials[index] : nullptr;
    }

    uint32_t MaterialLibrary::CompileDirty() {
        uint32_t compiled = 0;
        for (auto& material : m_Materials) {
            if (!material.Dirty) {
                continue;
            }
            material.Compiled = material.Graph.Compile();
            material.Dirty = false;
            ++compiled;
            if (!material.Compiled.Succeeded) {
                ENGINE_CORE_WARN("Material '{}' failed to compile: {}",
                                 material.Name, material.Compiled.Error);
            }
        }
        if (compiled > 0) {
            ++m_Revision;
        }
        return compiled;
    }

    bool MaterialLibrary::MarkDirty(uint32_t index) {
        if (index >= m_Materials.size()) {
            return false;
        }
        m_Materials[index].Dirty = true;
        return true;
    }

    void MaterialLibrary::Clear() {
        m_Materials.clear();
        m_NameToIndex.clear();
        ++m_Revision;
    }

} // namespace Renderer
} // namespace Core
