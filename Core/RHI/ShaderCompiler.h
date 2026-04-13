#pragma once

#include "Core/RHI/RHIPipelineState.h"

#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>

namespace Core {
namespace RHI {

    struct ShaderCompileRequest {
        std::string Source;
        ShaderStage Stage = ShaderStage::Vertex;
        std::string SourceName = "shader";
        bool Optimize = false;
        std::unordered_map<std::string, std::string> Defines;
    };

    struct ShaderCompileResult {
        bool Succeeded = false;
        std::vector<uint32_t> Spirv;
        std::string ErrorMessage;
        std::string SourceDigest;
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

        // Compiles without asserting on failure; used by incremental systems.
        static ShaderCompileResult CompileToSPIRVChecked(const ShaderCompileRequest& request);
    };

} // namespace RHI
} // namespace Core
