#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace Core {
namespace RHI {

    enum class ShaderStage {
        Vertex,
        Fragment,
        Compute,
        Geometry,
        Tessellation
    };

    class ShaderCompiler {
    public:
        // Compiles GLSL or HLSL source code into a SPIR-V binary vector.
        static std::vector<uint32_t> CompileToSPIRV(
            const std::string& source,
            ShaderStage stage,
            const std::string& sourceName = "shader",
            bool optimize = false
        );
    };

} // namespace RHI
} // namespace Core