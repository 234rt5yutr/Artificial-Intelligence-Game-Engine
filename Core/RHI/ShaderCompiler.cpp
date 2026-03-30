#include "ShaderCompiler.h"
#include "Core/Log.h"
#include "Core/Assert.h"
#include <shaderc/shaderc.hpp>

namespace Core {
namespace RHI {

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

    std::vector<uint32_t> ShaderCompiler::CompileToSPIRV(
        const std::string& source,
        ShaderStage stage,
        const std::string& sourceName,
        bool optimize) 
    {
        shaderc::Compiler compiler;
        shaderc::CompileOptions options;

        // If you need more advanced options (e.g. #defines, include resolution), configure 'options' here.
        if (optimize) {
            options.SetOptimizationLevel(shaderc_optimization_level_size);
        } else {
            options.SetOptimizationLevel(shaderc_optimization_level_zero);
            // options.SetGenerateDebugInfo(); // Optional: helps with debugging
        }

        // Set target environment for Vulkan 1.3
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);

        shaderc_shader_kind kind = GetShadercStage(stage);

        shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(
            source, kind, sourceName.c_str(), options);

        if (module.GetCompilationStatus() != shaderc_compilation_status_success) {
            ENGINE_CORE_ERROR("Shader Compilation Error in {0}:\n{1}", sourceName, module.GetErrorMessage());
            ENGINE_CORE_ASSERT(false, "Failed to compile shader to SPIR-V!");
            return {};
        }

        return { module.cbegin(), module.cend() };
    }

} // namespace RHI
} // namespace Core