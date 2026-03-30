#pragma once

#include <vector>
#include <string>
#include <memory>
#include "Core/Math/Math.h"
#include "Core/RHI/RHIBuffer.h"

namespace Core {
namespace Renderer {

    struct Vertex {
        Math::Vec3 position;
        Math::Vec3 normal;
        Math::Vec2 texCoord;
        Math::Vec4 tangent;
    };

    struct Primitive {
        uint32_t firstIndex;
        uint32_t indexCount;
        uint32_t materialIndex;
    };

    class Mesh {
    public:
        Mesh();
        ~Mesh();

        bool LoadGLTF(const std::string& filepath);

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<Primitive> primitives;

        std::shared_ptr<RHI::RHIBuffer> vertexBuffer;
        std::shared_ptr<RHI::RHIBuffer> indexBuffer;
    };

} // namespace Renderer
} // namespace Core
