#pragma once

#include <cstdint>
#include <vector>
#include <memory>

namespace Core {
namespace RHI {

    enum class ShaderStage {
        Vertex,
        Fragment,
        Compute
    };

    struct ShaderBytecode {
        const uint32_t* data = nullptr;
        std::size_t size = 0; // Size in bytes
        ShaderStage stage;
        const char* entryPoint = "main";
    };

    enum class PrimitiveTopology {
        PointList,
        LineList,
        LineStrip,
        TriangleList,
        TriangleStrip
    };

    enum class PolygonMode {
        Fill,
        Line,
        Point
    };

    enum class CullMode {
        None,
        Front,
        Back,
        FrontAndBack
    };

    struct VertexInputAttribute {
        uint32_t location;
        uint32_t binding;
        uint32_t format; // Format could be an enum, using uint32_t/VkFormat placeholder for now. Let's define one.
        uint32_t offset;
    };

    struct VertexInputBinding {
        uint32_t binding;
        uint32_t stride;
        bool inputRateInstance; // false = vertex, true = instance
    };

    //=========================================================================
    // Blend Factor Enums (maps to Vulkan)
    //=========================================================================

    enum class BlendFactor : uint32_t {
        Zero = 0,
        One = 1,
        SrcColor = 2,
        OneMinusSrcColor = 3,
        DstColor = 4,
        OneMinusDstColor = 5,
        SrcAlpha = 6,
        OneMinusSrcAlpha = 7,
        DstAlpha = 8,
        OneMinusDstAlpha = 9,
        ConstantColor = 10,
        OneMinusConstantColor = 11,
        ConstantAlpha = 12,
        OneMinusConstantAlpha = 13,
        SrcAlphaSaturate = 14
    };

    enum class BlendOp : uint32_t {
        Add = 0,
        Subtract = 1,
        ReverseSubtract = 2,
        Min = 3,
        Max = 4
    };

    enum class ColorWriteMask : uint32_t {
        None = 0,
        R = 1 << 0,
        G = 1 << 1,
        B = 1 << 2,
        A = 1 << 3,
        All = R | G | B | A
    };

    inline ColorWriteMask operator|(ColorWriteMask a, ColorWriteMask b) {
        return static_cast<ColorWriteMask>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline ColorWriteMask operator&(ColorWriteMask a, ColorWriteMask b) {
        return static_cast<ColorWriteMask>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    //=========================================================================
    // Depth Compare Operations
    //=========================================================================

    enum class CompareOp : uint32_t {
        Never = 0,
        Less = 1,
        Equal = 2,
        LessOrEqual = 3,
        Greater = 4,
        NotEqual = 5,
        GreaterOrEqual = 6,
        Always = 7
    };

    //=========================================================================
    // Render Target Blend State
    //=========================================================================

    struct RenderTargetBlendState {
        bool blendEnable = false;
        BlendFactor srcColorBlendFactor = BlendFactor::One;
        BlendFactor dstColorBlendFactor = BlendFactor::Zero;
        BlendOp colorBlendOp = BlendOp::Add;
        BlendFactor srcAlphaBlendFactor = BlendFactor::One;
        BlendFactor dstAlphaBlendFactor = BlendFactor::Zero;
        BlendOp alphaBlendOp = BlendOp::Add;
        ColorWriteMask colorWriteMask = ColorWriteMask::All;
    };

    struct GraphicsPipelineDescriptor {
        std::vector<ShaderBytecode> shaders;
        std::vector<VertexInputBinding> vertexBindings;
        std::vector<VertexInputAttribute> vertexAttributes;
        
        PrimitiveTopology topology = PrimitiveTopology::TriangleList;
        PolygonMode polygonMode = PolygonMode::Fill;
        CullMode cullMode = CullMode::Back;

        bool depthTestEnable = true;
        bool depthWriteEnable = true;
        CompareOp depthCompareOp = CompareOp::Less;

        std::vector<RenderTargetBlendState> blendStates;
        uint32_t renderTargetCount = 1;

        // Missing dynamic state, pipeline layout, and render pass stuff.
        // For Vulkan without dynamic rendering, we need a RenderPass. 
        // We'll leave it as an implementation detail or add it if needed.
    };

    class RHIPipelineState {
    public:
        virtual ~RHIPipelineState() = default;
    };

} // namespace RHI
} // namespace Core
