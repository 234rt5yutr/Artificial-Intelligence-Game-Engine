#include "ShaderCompiler.h"
#include "Core/Log.h"
#include "Core/Assert.h"
#include <shaderc/shaderc.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <sstream>
#include <string_view>
#include <vector>

namespace Core {
namespace RHI {

    namespace {
        constexpr uint64_t kFnvOffsetBasis = 1469598103934665603ull;
        constexpr uint64_t kFnvPrime = 1099511628211ull;

        uint64_t HashBytes(const std::string_view bytes) {
            uint64_t hash = kFnvOffsetBasis;
            for (const char byte : bytes) {
                hash ^= static_cast<uint64_t>(static_cast<unsigned char>(byte));
                hash *= kFnvPrime;
            }
            return hash;
        }

        std::string ToHex(uint64_t value) {
            constexpr std::array<char, 16> kHex = {'0', '1', '2', '3', '4', '5', '6', '7',
                                                   '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
            std::string out(16, '0');
            for (int32_t index = 15; index >= 0; --index) {
                out[static_cast<size_t>(index)] = kHex[static_cast<size_t>(value & 0xfull)];
                value >>= 4ull;
            }
            return out;
        }
    } // namespace

    static shaderc_shader_kind GetShadercStage(ShaderStage stage) {
        switch (stage) {
            case ShaderStage::Vertex:       return shaderc_glsl_vertex_shader;
            case ShaderStage::Fragment:     return shaderc_glsl_fragment_shader;
            case ShaderStage::Compute:      return shaderc_glsl_compute_shader;
            case ShaderStage::Geometry:     return shaderc_glsl_geometry_shader;
            case ShaderStage::Tessellation: return shaderc_glsl_tess_control_shader; // or eval, but let's keep simple
        }
        return shaderc_glsl_infer_from_source;
    }

    ShaderCompileResult ShaderCompiler::CompileToSPIRVChecked(const ShaderCompileRequest& request) {
        ShaderCompileResult result{};
        shaderc::Compiler compiler;
        shaderc::CompileOptions options;

        if (request.Optimize) {
            options.SetOptimizationLevel(shaderc_optimization_level_size);
        } else {
            options.SetOptimizationLevel(shaderc_optimization_level_zero);
        }

        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);

        std::vector<std::pair<std::string, std::string>> sortedDefines;
        sortedDefines.reserve(request.Defines.size());
        for (const auto& definePair : request.Defines) {
            sortedDefines.emplace_back(definePair.first, definePair.second);
        }
        std::sort(sortedDefines.begin(), sortedDefines.end(),
                  [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

        std::ostringstream sourceStream;
        for (const auto& definePair : sortedDefines) {
            sourceStream << "#define " << definePair.first;
            if (!definePair.second.empty()) {
                sourceStream << " " << definePair.second;
            }
            sourceStream << "\n";
        }
        sourceStream << request.Source;
        const std::string finalSource = sourceStream.str();
        result.SourceDigest = ToHex(HashBytes(finalSource));

        shaderc_shader_kind kind = GetShadercStage(request.Stage);
        shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(
            finalSource,
            kind,
            request.SourceName.c_str(),
            options);

        if (module.GetCompilationStatus() != shaderc_compilation_status_success) {
            result.Succeeded = false;
            result.ErrorMessage = module.GetErrorMessage();
            return result;
        }

        result.Succeeded = true;
        result.Spirv.assign(module.cbegin(), module.cend());
        return result;
    }

    std::vector<uint32_t> ShaderCompiler::CompileToSPIRV(
        const std::string& source,
        ShaderStage stage,
        const std::string& sourceName,
        bool optimize) 
    {
        ShaderCompileRequest request{};
        request.Source = source;
        request.Stage = stage;
        request.SourceName = sourceName;
        request.Optimize = optimize;
        const ShaderCompileResult result = CompileToSPIRVChecked(request);

        if (!result.Succeeded) {
            ENGINE_CORE_ERROR("Shader Compilation Error in {0}:\n{1}", sourceName, result.ErrorMessage);
            ENGINE_CORE_ASSERT(false, "Failed to compile shader to SPIR-V!");
            return {};
        }

        return result.Spirv;
    }

} // namespace RHI
} // namespace Core
