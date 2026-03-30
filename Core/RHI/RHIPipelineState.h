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

    struct RenderTargetBlendState {
        bool blendEnable = false;
        // Simplified blend state
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
