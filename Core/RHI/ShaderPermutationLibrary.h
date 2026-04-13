#pragma once

#include "Core/RHI/ShaderCompiler.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Core {
namespace RHI {

    template <typename T>
    struct Result {
        bool Ok = false;
        T Value{};
        std::string Error;

        static Result Success(T value) {
            Result result{};
            result.Ok = true;
            result.Value = std::move(value);
            return result;
        }

        static Result Failure(std::string error) {
            Result result{};
            result.Ok = false;
            result.Error = std::move(error);
            return result;
        }
    };

    template <>
    struct Result<void> {
        bool Ok = false;
        std::string Error;

        static Result Success() {
            Result result{};
            result.Ok = true;
            return result;
        }

        static Result Failure(std::string error) {
            Result result{};
            result.Ok = false;
            result.Error = std::move(error);
            return result;
        }
    };

    enum class ShaderPermutationDiagnosticSeverity : uint8_t {
        Info = 0,
        Warning = 1,
        Error = 2
    };

    struct ShaderPermutationConstraint {
        std::vector<std::string> RequiresAllFeatures;
        std::vector<std::string> ForbidsAnyFeature;
        std::string Reason;
    };

    struct ShaderPermutationCompiledArtifact {
        std::string PermutationKey;
        ShaderStage Stage = ShaderStage::Vertex;
        std::filesystem::path ArtifactPath;
        std::string SourceDigest;
        bool CacheHit = false;
        bool Compiled = false;
    };

    struct ShaderPermutationDiagnostic {
        std::string PermutationKey;
        ShaderStage Stage = ShaderStage::Vertex;
        ShaderPermutationDiagnosticSeverity Severity = ShaderPermutationDiagnosticSeverity::Info;
        std::string Message;
    };

    struct ShaderPermutationLibraryRequest {
        std::string Platform = "windows";
        std::vector<std::string> MaterialFeatures;
        std::vector<std::string> RendererFeatures;
        std::vector<ShaderPermutationConstraint> Constraints;
        std::unordered_map<ShaderStage, std::string> ShaderSources;
        std::filesystem::path CacheDirectory = std::filesystem::path("build") / "shader_permutations";
        bool IncrementalBuild = true;
        bool Optimize = true;
    };

    struct ShaderPermutationLibraryResult {
        bool Success = false;
        bool PartialSuccess = false;
        uint32_t TotalPermutations = 0;
        uint32_t CompiledPermutationCount = 0;
        uint32_t FailedPermutationCount = 0;
        uint32_t CacheHitCount = 0;
        std::vector<std::string> DeterministicOrder;
        std::vector<ShaderPermutationCompiledArtifact> Artifacts;
        std::vector<ShaderPermutationDiagnostic> Diagnostics;
        std::string LibraryDigest;
        std::filesystem::path ArtifactMetadataPath;
    };

    Result<ShaderPermutationLibraryResult> CompileShaderPermutationLibrary(
        const ShaderPermutationLibraryRequest& request);

} // namespace RHI
} // namespace Core

