#include "Core/RHI/ShaderPermutationLibrary.h"

#include "Core/Log.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <set>
#include <sstream>
#include <string_view>

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

        std::string BuildPermutationKey(
            const std::string& platform,
            const std::vector<std::string>& enabledMaterialFeatures,
            const std::vector<std::string>& enabledRendererFeatures) {
            std::ostringstream stream;
            stream << "platform=" << platform << ";";
            stream << "material=";
            for (size_t index = 0; index < enabledMaterialFeatures.size(); ++index) {
                stream << enabledMaterialFeatures[index];
                if (index + 1u < enabledMaterialFeatures.size()) {
                    stream << ",";
                }
            }
            stream << ";renderer=";
            for (size_t index = 0; index < enabledRendererFeatures.size(); ++index) {
                stream << enabledRendererFeatures[index];
                if (index + 1u < enabledRendererFeatures.size()) {
                    stream << ",";
                }
            }
            return stream.str();
        }

        std::string SanitizePermutationKey(const std::string& permutationKey) {
            std::string sanitized = permutationKey;
            for (char& character : sanitized) {
                if (!(std::isalnum(static_cast<unsigned char>(character)) != 0)) {
                    character = '_';
                }
            }
            return sanitized;
        }

        std::vector<std::vector<std::string>> EnumerateFeatureSubsets(std::vector<std::string> features) {
            std::sort(features.begin(), features.end());
            const size_t featureCount = features.size();
            const size_t iteratedFeatureCount = (featureCount > 20u) ? 20u : featureCount;
            const size_t subsetCount = static_cast<size_t>(1u) << iteratedFeatureCount;

            std::vector<std::vector<std::string>> subsets;
            subsets.reserve(subsetCount);
            for (size_t mask = 0; mask < subsetCount; ++mask) {
                std::vector<std::string> subset;
                for (size_t featureIndex = 0; featureIndex < iteratedFeatureCount; ++featureIndex) {
                    if ((mask & (static_cast<size_t>(1u) << featureIndex)) != 0u) {
                        subset.push_back(features[featureIndex]);
                    }
                }
                subsets.push_back(std::move(subset));
            }
            return subsets;
        }

        bool IsConstraintSatisfied(
            const ShaderPermutationConstraint& constraint,
            const std::set<std::string>& activeFeatures) {
            for (const std::string& requiredFeature : constraint.RequiresAllFeatures) {
                if (activeFeatures.find(requiredFeature) == activeFeatures.end()) {
                    return false;
                }
            }
            for (const std::string& forbiddenFeature : constraint.ForbidsAnyFeature) {
                if (activeFeatures.find(forbiddenFeature) != activeFeatures.end()) {
                    return false;
                }
            }
            return true;
        }

        std::unordered_map<std::string, std::string> BuildFeatureDefines(
            const std::vector<std::string>& materialFeatures,
            const std::vector<std::string>& rendererFeatures) {
            std::unordered_map<std::string, std::string> defines;
            for (const std::string& feature : materialFeatures) {
                defines.emplace("MAT_" + feature, "1");
            }
            for (const std::string& feature : rendererFeatures) {
                defines.emplace("REN_" + feature, "1");
            }
            return defines;
        }

        std::string StageToString(ShaderStage stage) {
            switch (stage) {
                case ShaderStage::Vertex: {
                    return "vertex";
                }
                case ShaderStage::Fragment: {
                    return "fragment";
                }
                case ShaderStage::Compute: {
                    return "compute";
                }
                case ShaderStage::Geometry: {
                    return "geometry";
                }
                case ShaderStage::Tessellation: {
                    return "tessellation";
                }
            }
            return "unknown";
        }
    } // namespace

    Result<ShaderPermutationLibraryResult> CompileShaderPermutationLibrary(
        const ShaderPermutationLibraryRequest& request) {
        ShaderPermutationLibraryResult result{};
        if (request.Platform.empty()) {
            return Result<ShaderPermutationLibraryResult>::Failure("Platform must not be empty.");
        }
        if (request.ShaderSources.empty()) {
            return Result<ShaderPermutationLibraryResult>::Failure("No shader sources supplied for permutation compilation.");
        }

        const std::vector<std::vector<std::string>> materialSubsets = EnumerateFeatureSubsets(request.MaterialFeatures);
        const std::vector<std::vector<std::string>> rendererSubsets = EnumerateFeatureSubsets(request.RendererFeatures);

        std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>> combinations;
        combinations.reserve(materialSubsets.size() * rendererSubsets.size());
        for (const auto& materialSubset : materialSubsets) {
            for (const auto& rendererSubset : rendererSubsets) {
                std::set<std::string> activeFeatures;
                activeFeatures.insert(materialSubset.begin(), materialSubset.end());
                activeFeatures.insert(rendererSubset.begin(), rendererSubset.end());

                bool valid = true;
                for (const ShaderPermutationConstraint& constraint : request.Constraints) {
                    if (!IsConstraintSatisfied(constraint, activeFeatures)) {
                        valid = false;
                        break;
                    }
                }

                if (valid) {
                    combinations.emplace_back(materialSubset, rendererSubset);
                }
            }
        }

        std::sort(combinations.begin(), combinations.end(),
                  [&](const auto& lhs, const auto& rhs) {
                      const std::string lhsKey = BuildPermutationKey(request.Platform, lhs.first, lhs.second);
                      const std::string rhsKey = BuildPermutationKey(request.Platform, rhs.first, rhs.second);
                      return lhsKey < rhsKey;
                  });

        result.TotalPermutations = static_cast<uint32_t>(combinations.size());
        if (combinations.empty()) {
            return Result<ShaderPermutationLibraryResult>::Failure("No valid shader permutations generated after pruning.");
        }

        std::filesystem::create_directories(request.CacheDirectory);

        std::ostringstream digestStream;
        digestStream << "platform=" << request.Platform << ";";

        for (const auto& combination : combinations) {
            const std::string permutationKey = BuildPermutationKey(request.Platform, combination.first, combination.second);
            result.DeterministicOrder.push_back(permutationKey);
            digestStream << permutationKey << "|";

            std::filesystem::path permutationDirectory = request.CacheDirectory / SanitizePermutationKey(permutationKey);
            std::filesystem::create_directories(permutationDirectory);

            const auto defines = BuildFeatureDefines(combination.first, combination.second);
            bool permutationFailed = false;

            for (const auto& shaderSource : request.ShaderSources) {
                ShaderCompileRequest compileRequest{};
                compileRequest.Source = shaderSource.second;
                compileRequest.Stage = shaderSource.first;
                compileRequest.SourceName = permutationKey + "::" + StageToString(shaderSource.first);
                compileRequest.Optimize = request.Optimize;
                compileRequest.Defines = defines;

                const std::string stageName = StageToString(shaderSource.first);
                const ShaderCompileResult probe = ShaderCompiler::CompileToSPIRVChecked(compileRequest);
                const std::string sourceDigest = probe.SourceDigest;
                std::filesystem::path artifactPath = permutationDirectory / (stageName + "_" + sourceDigest + ".spv");

                ShaderPermutationCompiledArtifact artifact{};
                artifact.PermutationKey = permutationKey;
                artifact.Stage = shaderSource.first;
                artifact.ArtifactPath = artifactPath;
                artifact.SourceDigest = sourceDigest;

                if (request.IncrementalBuild && std::filesystem::exists(artifactPath) && probe.Succeeded) {
                    artifact.CacheHit = true;
                    artifact.Compiled = true;
                    ++result.CacheHitCount;
                    ++result.CompiledPermutationCount;
                    digestStream << stageName << ":" << sourceDigest << ":cache|";
                    result.Artifacts.push_back(std::move(artifact));
                    continue;
                }

                if (!probe.Succeeded) {
                    permutationFailed = true;
                    ++result.FailedPermutationCount;
                    ShaderPermutationDiagnostic diagnostic{};
                    diagnostic.PermutationKey = permutationKey;
                    diagnostic.Stage = shaderSource.first;
                    diagnostic.Severity = ShaderPermutationDiagnosticSeverity::Error;
                    diagnostic.Message = probe.ErrorMessage.empty() ? "Unknown shader compilation failure." : probe.ErrorMessage;
                    result.Diagnostics.push_back(std::move(diagnostic));
                    digestStream << stageName << ":" << sourceDigest << ":fail|";
                    result.Artifacts.push_back(std::move(artifact));
                    continue;
                }

                std::ofstream outputFile(artifactPath, std::ios::binary | std::ios::trunc);
                if (!outputFile.is_open()) {
                    permutationFailed = true;
                    ++result.FailedPermutationCount;
                    ShaderPermutationDiagnostic diagnostic{};
                    diagnostic.PermutationKey = permutationKey;
                    diagnostic.Stage = shaderSource.first;
                    diagnostic.Severity = ShaderPermutationDiagnosticSeverity::Error;
                    diagnostic.Message = "Failed to open artifact output file: " + artifactPath.string();
                    result.Diagnostics.push_back(std::move(diagnostic));
                    result.Artifacts.push_back(std::move(artifact));
                    continue;
                }

                outputFile.write(reinterpret_cast<const char*>(probe.Spirv.data()),
                                 static_cast<std::streamsize>(probe.Spirv.size() * sizeof(uint32_t)));
                outputFile.close();

                artifact.Compiled = true;
                ++result.CompiledPermutationCount;
                digestStream << stageName << ":" << sourceDigest << ":build|";
                result.Artifacts.push_back(std::move(artifact));
            }

            if (permutationFailed) {
                ShaderPermutationDiagnostic diagnostic{};
                diagnostic.PermutationKey = permutationKey;
                diagnostic.Stage = ShaderStage::Vertex;
                diagnostic.Severity = ShaderPermutationDiagnosticSeverity::Warning;
                diagnostic.Message = "Permutation had one or more stage failures; partial result retained.";
                result.Diagnostics.push_back(std::move(diagnostic));
            }
        }

        result.LibraryDigest = ToHex(HashBytes(digestStream.str()));
        result.Success = result.FailedPermutationCount == 0u;
        result.PartialSuccess = (!result.Success) && (result.CompiledPermutationCount > 0u);

        std::filesystem::path metadataPath = request.CacheDirectory / "library_metadata.txt";
        std::ofstream metadataFile(metadataPath, std::ios::trunc);
        if (metadataFile.is_open()) {
            const auto nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            metadataFile << "digest=" << result.LibraryDigest << "\n";
            metadataFile << "platform=" << request.Platform << "\n";
            metadataFile << "total_permutations=" << result.TotalPermutations << "\n";
            metadataFile << "compiled_count=" << result.CompiledPermutationCount << "\n";
            metadataFile << "failed_count=" << result.FailedPermutationCount << "\n";
            metadataFile << "cache_hits=" << result.CacheHitCount << "\n";
            metadataFile << "generated_at_epoch=" << nowSeconds << "\n";
            metadataFile.close();
            result.ArtifactMetadataPath = metadataPath;
        } else {
            ENGINE_CORE_WARN("Failed to write permutation metadata file at '{0}'.", metadataPath.string());
        }

        return Result<ShaderPermutationLibraryResult>::Success(std::move(result));
    }

} // namespace RHI
} // namespace Core

