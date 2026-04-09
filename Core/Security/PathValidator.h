#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <cstddef>
#include <algorithm>

namespace Core {
namespace Security {

    // Maximum file sizes for different asset types
    constexpr size_t MAX_TEXTURE_SIZE = 256 * 1024 * 1024;   // 256MB
    constexpr size_t MAX_MESH_SIZE = 512 * 1024 * 1024;      // 512MB  
    constexpr size_t MAX_SHADER_SIZE = 16 * 1024 * 1024;     // 16MB
    constexpr size_t MAX_AUDIO_SIZE = 256 * 1024 * 1024;     // 256MB
    constexpr size_t MAX_GENERIC_SIZE = 512 * 1024 * 1024;   // 512MB

    class PathValidator {
    public:
        struct Config {
            std::filesystem::path AssetRoot;
            std::filesystem::path CookedAssetRoot;
            std::filesystem::path ShaderRoot;
            bool AllowSymlinks = false;
        };

        static void Initialize(const Config& config) {
            s_Config = config;
            s_Initialized = true;
        }

        static bool IsInitialized() {
            return s_Initialized;
        }

        // Check if path contains dangerous traversal components
        static bool ContainsTraversalComponents(const std::filesystem::path& path) {
            std::string pathStr = path.string();
            
            // Check for ".." anywhere in path
            if (pathStr.find("..") != std::string::npos) {
                return true;
            }
            
            // Check individual components
            for (const auto& component : path) {
                std::string comp = component.string();
                if (comp == ".." || comp == "...") {
                    return true;
                }
            }
            
            return false;
        }

        // Check if path is within allowed root directory
        static bool IsWithinRoot(const std::filesystem::path& path, 
                                  const std::filesystem::path& root) {
            std::error_code ec;
            
            // Get canonical paths for comparison
            auto canonicalPath = std::filesystem::weakly_canonical(path, ec);
            if (ec) return false;
            
            auto canonicalRoot = std::filesystem::weakly_canonical(root, ec);
            if (ec) return false;

            // Compare normalized path components to avoid separator/encoding issues.
            auto pathIt = canonicalPath.begin();
            auto rootIt = canonicalRoot.begin();

            for (; rootIt != canonicalRoot.end(); ++rootIt, ++pathIt) {
                if (pathIt == canonicalPath.end() || *pathIt != *rootIt) {
                    return false;
                }
            }

            return true;
        }

        // Validate an asset path - returns sanitized canonical path or nullopt
        static std::optional<std::filesystem::path> ValidateAssetPath(
            const std::filesystem::path& path) {
            
            // Check for traversal attempts
            if (ContainsTraversalComponents(path)) {
                return std::nullopt;
            }
            
            // If initialized, check against root
            if (s_Initialized && !s_Config.AssetRoot.empty()) {
                if (!IsWithinRoot(path, s_Config.AssetRoot)) {
                    return std::nullopt;
                }
            }
            
            // Check for symlinks if not allowed
            if (s_Initialized && !s_Config.AllowSymlinks) {
                std::error_code ec;
                if (std::filesystem::is_symlink(path, ec)) {
                    return std::nullopt;
                }
            }
            
            std::error_code ec;
            return std::filesystem::weakly_canonical(path, ec);
        }

        // Validate cooked asset path
        static std::optional<std::filesystem::path> ValidateCookedPath(
            const std::filesystem::path& path) {
            
            if (ContainsTraversalComponents(path)) {
                return std::nullopt;
            }
            
            if (s_Initialized && !s_Config.CookedAssetRoot.empty()) {
                if (!IsWithinRoot(path, s_Config.CookedAssetRoot)) {
                    return std::nullopt;
                }
            }
            
            std::error_code ec;
            return std::filesystem::weakly_canonical(path, ec);
        }

        // Validate shader path
        static std::optional<std::filesystem::path> ValidateShaderPath(
            const std::filesystem::path& path) {
            
            if (ContainsTraversalComponents(path)) {
                return std::nullopt;
            }
            
            if (s_Initialized && !s_Config.ShaderRoot.empty()) {
                if (!IsWithinRoot(path, s_Config.ShaderRoot)) {
                    return std::nullopt;
                }
            }
            
            std::error_code ec;
            return std::filesystem::weakly_canonical(path, ec);
        }

        // Validate file size is within limits
        static bool ValidateFileSize(const std::filesystem::path& path, size_t maxSize) {
            std::error_code ec;
            auto size = std::filesystem::file_size(path, ec);
            if (ec) {
                return false;
            }
            return size <= maxSize;
        }

        // Sanitize path for logging (hide full paths in production)
        static std::string SanitizeForLogging(const std::filesystem::path& path) {
            #ifdef NDEBUG
                // In release builds, only show filename
                return path.filename().string();
            #else
                // In debug builds, show full path
                return path.string();
            #endif
        }

        // Quick validation for any path - blocks obvious attacks
        static bool QuickValidate(const std::filesystem::path& path) {
            return !ContainsTraversalComponents(path);
        }

    private:
        inline static Config s_Config;
        inline static bool s_Initialized = false;
    };

} // namespace Security
} // namespace Core
