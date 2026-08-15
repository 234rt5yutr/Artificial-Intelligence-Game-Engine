#pragma once

// Material graph: node authoring -> GLSL codegen -> shader permutation.
//
// The engine had no authorable material path at all. `ShaderPermutationLibrary`
// could compile permutations but nothing declared them from content, and
// `MeshComponent` carried a mesh path with a bare integer material index that
// pointed at nothing.
//
// This is the content side of that gap: a small directed acyclic node graph that
// compiles to the body of a fragment shader. It deliberately owns no Vulkan
// state - `MaterialPipelineCache` (RHI/Vulkan) turns a compiled graph into a
// pipeline, and `MaterialLibrary` below is what the draw command's
// `MaterialIndex` actually indexes into.

#include "Core/Math/Math.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Core {
namespace Renderer {

    enum class MaterialNodeType : uint8_t {
        // Sources
        ConstantScalar = 0,   // Value.x
        ConstantColor,        // Value.rgba
        TextureSample,        // TextureSlot, UV from input 0 (defaults to mesh UV)
        UV,                   // mesh texcoord as vec4(u, v, 0, 0)
        WorldPosition,
        WorldNormal,
        VertexColor,
        Time,
        CameraVector,

        // Operators
        Multiply,             // in0 * in1
        Add,                  // in0 + in1
        Subtract,             // in0 - in1
        Lerp,                 // mix(in0, in1, in2.x)
        DotProduct,           // dot(in0.xyz, in1.xyz) splatted
        OneMinus,             // 1 - in0
        Saturate,             // clamp(in0, 0, 1)
        Power,                // pow(in0, in1.x)
        Fresnel,              // schlick against camera vector, in0.x = power
        Panner,               // in0.xy + Time * Value.xy
        Normalize,
        Split,                // in0.x splatted (channel select via Value.x)

        // Sink
        Output                // slots: BaseColor, Metallic, Roughness, Emissive, Normal, Opacity
    };

    // Output node input slots, in declaration order.
    enum class MaterialOutputSlot : uint8_t {
        BaseColor = 0,
        Metallic = 1,
        Roughness = 2,
        Emissive = 3,
        Normal = 4,
        Opacity = 5,
        Count = 6
    };

    struct MaterialNode {
        uint32_t Id = 0;
        MaterialNodeType Type = MaterialNodeType::ConstantColor;
        std::string Name;
        Math::Vec4 Value{1.0f, 1.0f, 1.0f, 1.0f};
        // Sampler slot name for TextureSample. Slots are collected in
        // declaration order and bound as an array in the fragment shader.
        std::string TextureSlot;
    };

    struct MaterialLink {
        uint32_t FromNode = 0;
        uint32_t ToNode = 0;
        uint8_t ToSlot = 0;
    };

    struct MaterialCompileResult {
        bool Succeeded = false;
        std::string Error;
        // Fragment-shader body computing `MaterialSurface surf`. Injected into
        // the lit shader template by MaterialPipelineCache.
        std::string FragmentBody;
        // Texture slot names in binding order.
        std::vector<std::string> TextureSlots;
        // Stable across runs; keys the pipeline cache.
        uint64_t Hash = 0;
        bool UsesTime = false;
    };

    // Fixed values used when a graph is absent or fails to compile, so a scene
    // with broken content still renders rather than dropping the mesh.
    struct MaterialFallback {
        Math::Vec3 BaseColor{0.78f, 0.78f, 0.80f};
        float Metallic = 0.0f;
        float Roughness = 0.6f;
        Math::Vec3 Emissive{0.0f};
    };

    class MaterialGraph {
    public:
        MaterialGraph() = default;
        explicit MaterialGraph(std::string name) : m_Name(std::move(name)) {}

        const std::string& GetName() const { return m_Name; }
        void SetName(std::string name) { m_Name = std::move(name); }

        // Returns the new node id. Ids are dense and never reused, so a link
        // referring to a removed node fails validation instead of rebinding to
        // whatever took its place.
        uint32_t AddNode(MaterialNodeType type, const std::string& name = {});
        bool RemoveNode(uint32_t nodeId);
        MaterialNode* FindNode(uint32_t nodeId);
        const MaterialNode* FindNode(uint32_t nodeId) const;
        const std::vector<MaterialNode>& GetNodes() const { return m_Nodes; }
        const std::vector<MaterialLink>& GetLinks() const { return m_Links; }

        bool SetNodeValue(uint32_t nodeId, const Math::Vec4& value);
        bool SetNodeTextureSlot(uint32_t nodeId, const std::string& slot);

        // Replaces any existing link into (toNode, toSlot): an input takes one
        // source, and silently keeping both would make codegen order-dependent.
        bool Connect(uint32_t fromNode, uint32_t toNode, uint8_t toSlot, std::string* error = nullptr);
        bool Disconnect(uint32_t toNode, uint8_t toSlot);

        // Compiles to GLSL. Detects cycles, missing inputs, and unreachable
        // output; never throws.
        MaterialCompileResult Compile() const;

        // A graph with a single ConstantColor feeding BaseColor plus scalar
        // metallic/roughness. Enough to see a material working end to end.
        static MaterialGraph MakeDefault(const std::string& name,
                                         const Math::Vec3& baseColor,
                                         float metallic,
                                         float roughness);

        std::string ToJson() const;
        static bool FromJson(const std::string& json, MaterialGraph& out, std::string* error = nullptr);

    private:
        std::string m_Name;
        std::vector<MaterialNode> m_Nodes;
        std::vector<MaterialLink> m_Links;
        uint32_t m_NextNodeId = 1;
    };

    // How a material's opacity is meant to be read. The engine drew everything
    // as if it were Opaque, so a leaf texture rendered as a rectangle and glass
    // as a wall.
    enum class MaterialAlphaMode : uint8_t {
        Opaque = 0,
        // Fully in or fully out per pixel. No sorting needed, so these stay in
        // the ordinary depth-writing pass.
        Masked,
        // Blended, so they must draw after everything they show through and
        // must not write depth.
        Blend,
    };

    inline const char* MaterialAlphaModeName(MaterialAlphaMode mode) {
        switch (mode) {
            case MaterialAlphaMode::Masked: return "masked";
            case MaterialAlphaMode::Blend:  return "blend";
            default:                        return "opaque";
        }
    }

    // Runtime material: a graph plus its last compile result and the scalar
    // parameters the fixed fallback path uses.
    struct MaterialInstance {
        std::string Name;
        MaterialGraph Graph;
        MaterialCompileResult Compiled;
        MaterialFallback Fallback;
        bool DoubleSided = false;
        MaterialAlphaMode AlphaMode = MaterialAlphaMode::Opaque;
        float AlphaCutoff = 0.5f;
        bool Dirty = true;
    };

    // Index-addressed registry. `ECS::DrawCommand::MaterialIndex` indexes this,
    // which is what previously pointed at nothing.
    class MaterialLibrary {
    public:
        static MaterialLibrary& Get();

        // Slot 0 is always a valid default material, created on first use.
        uint32_t GetOrCreateDefault();

        uint32_t CreateMaterial(const std::string& name);
        // Returns UINT32_MAX when absent.
        uint32_t FindMaterial(const std::string& name) const;
        MaterialInstance* GetMaterial(uint32_t index);
        const MaterialInstance* GetMaterial(uint32_t index) const;
        uint32_t GetMaterialCount() const { return static_cast<uint32_t>(m_Materials.size()); }

        // Compiles any material marked dirty. Returns the number recompiled.
        uint32_t CompileDirty();
        bool MarkDirty(uint32_t index);

        // Monotonic; a pipeline cache compares this to know it must rebuild.
        uint64_t GetRevision() const { return m_Revision; }

        void Clear();

    private:
        MaterialLibrary() = default;

        std::vector<MaterialInstance> m_Materials;
        std::unordered_map<std::string, uint32_t> m_NameToIndex;
        uint64_t m_Revision = 0;
    };

} // namespace Renderer
} // namespace Core
