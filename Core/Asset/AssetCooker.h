#pragma once

#include "AssetTypes.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <filesystem>

namespace Core {
namespace Asset {

    // Cooking result status
    enum class CookResult {
        Success,
        UpToDate,            // Source hasn't changed
        SourceNotFound,
        InvalidFormat,
        CompressionFailed,
        WriteFailed,
        DependencyFailed
    };

    // Cooking options
    struct CookOptions {
        bool ForceRebuild = false;       // Ignore timestamps, always rebuild
        bool GenerateMipmaps = true;     // For textures
        bool CompressTextures = true;    // Use BCn compression
        bool OptimizeMeshes = true;      // Vertex cache optimization
        bool StripDebugInfo = true;      // Remove debug symbols from shaders
        bool Parallel = true;            // Use parallel cooking
        int MaxThreads = 0;              // 0 = auto (use all cores)
        std::string OutputDirectory;     // Where to write cooked assets
        std::string Platform;            // Target platform (PC, Console, etc.)
    };

    // Progress callback for cooking operations
    using CookProgressCallback = std::function<void(const std::string& asset, float progress)>;

    // Asset cooker interface
    class IAssetCooker {
    public:
        virtual ~IAssetCooker() = default;
        
        // Get the asset types this cooker handles
        virtual std::vector<AssetType> GetSupportedTypes() const = 0;
        
        // Get file extensions this cooker handles
        virtual std::vector<std::string> GetSupportedExtensions() const = 0;
        
        // Cook a single asset
        virtual CookResult Cook(
            const std::filesystem::path& sourcePath,
            const std::filesystem::path& outputPath,
            const CookOptions& options) = 0;
        
        // Check if asset needs recooking
        virtual bool NeedsCooking(
            const std::filesystem::path& sourcePath,
            const std::filesystem::path& outputPath) const = 0;
    };

    // Texture cooker implementation
    class TextureCooker : public IAssetCooker {
    public:
        std::vector<AssetType> GetSupportedTypes() const override;
        std::vector<std::string> GetSupportedExtensions() const override;
        
        CookResult Cook(
            const std::filesystem::path& sourcePath,
            const std::filesystem::path& outputPath,
            const CookOptions& options) override;
        
        bool NeedsCooking(
            const std::filesystem::path& sourcePath,
            const std::filesystem::path& outputPath) const override;

    private:
        // Texture processing helpers
        bool LoadSourceTexture(const std::filesystem::path& path,
                               std::vector<uint8_t>& data,
                               uint32_t& width, uint32_t& height,
                               uint32_t& channels);
        
        std::vector<uint8_t> GenerateMipmaps(const std::vector<uint8_t>& data,
                                             uint32_t width, uint32_t height,
                                             uint32_t channels,
                                             std::vector<uint32_t>& mipSizes);
        
        std::vector<uint8_t> CompressBC(const std::vector<uint8_t>& data,
                                        uint32_t width, uint32_t height,
                                        uint32_t channels,
                                        TextureFormat& outFormat);
    };

    // Mesh cooker implementation
    class MeshCooker : public IAssetCooker {
    public:
        std::vector<AssetType> GetSupportedTypes() const override;
        std::vector<std::string> GetSupportedExtensions() const override;
        
        CookResult Cook(
            const std::filesystem::path& sourcePath,
            const std::filesystem::path& outputPath,
            const CookOptions& options) override;
        
        bool NeedsCooking(
            const std::filesystem::path& sourcePath,
            const std::filesystem::path& outputPath) const override;

    private:
        // Mesh optimization helpers
        void OptimizeVertexCache(std::vector<uint32_t>& indices, uint32_t vertexCount);
        void OptimizeOverdraw(std::vector<uint32_t>& indices,
                              const float* positions, uint32_t vertexCount);
        void QuantizePositions(std::vector<float>& positions,
                               const float* boundsMin, const float* boundsMax);
    };

    // Shader cooker implementation
    class ShaderCooker : public IAssetCooker {
    public:
        std::vector<AssetType> GetSupportedTypes() const override;
        std::vector<std::string> GetSupportedExtensions() const override;
        
        CookResult Cook(
            const std::filesystem::path& sourcePath,
            const std::filesystem::path& outputPath,
            const CookOptions& options) override;
        
        bool NeedsCooking(
            const std::filesystem::path& sourcePath,
            const std::filesystem::path& outputPath) const override;

    private:
        // Compile GLSL/HLSL to SPIR-V
        bool CompileToSpirv(const std::filesystem::path& sourcePath,
                            std::vector<uint32_t>& spirv,
                            std::string& errorLog);
        
        // Extract reflection data
        std::string ExtractReflection(const std::vector<uint32_t>& spirv);
    };

    // Structured JSON asset cooker (prefabs, graphs, timelines)
    class StructuredDataCooker : public IAssetCooker {
    public:
        std::vector<AssetType> GetSupportedTypes() const override;
        std::vector<std::string> GetSupportedExtensions() const override;

        CookResult Cook(
            const std::filesystem::path& sourcePath,
            const std::filesystem::path& outputPath,
            const CookOptions& options) override;

        bool NeedsCooking(
            const std::filesystem::path& sourcePath,
            const std::filesystem::path& outputPath) const override;

    private:
        AssetType GetAssetTypeForExtension(const std::string& extension) const;
    };

    // Stage 27 UI authoring asset cooker (widget blueprint/layout/style/localization)
    class UIAuthoringCooker : public IAssetCooker {
    public:
        std::vector<AssetType> GetSupportedTypes() const override;
        std::vector<std::string> GetSupportedExtensions() const override;

        CookResult Cook(
            const std::filesystem::path& sourcePath,
            const std::filesystem::path& outputPath,
            const CookOptions& options) override;

        bool NeedsCooking(
            const std::filesystem::path& sourcePath,
            const std::filesystem::path& outputPath) const override;

    private:
        AssetType GetAssetTypeForExtension(const std::string& extension) const;
    };

    // Asset manifest entry
    struct ManifestEntry {
        uint64_t AssetId;
        AssetType Type;
        std::string SourcePath;
        std::string CookedPath;
        uint64_t SourceHash;
        uint64_t CookedTimestamp;
        std::string AddressKey;
        std::string BundleId;
        std::vector<uint64_t> Dependencies;
    };

    // Asset manifest - tracks all cooked assets
    class AssetManifest {
    public:
        bool Load(const std::filesystem::path& path);
        bool Save(const std::filesystem::path& path) const;
        
        void AddEntry(const ManifestEntry& entry);
        void RemoveEntry(uint64_t assetId);
        
        const ManifestEntry* FindByPath(const std::string& sourcePath) const;
        const ManifestEntry* FindById(uint64_t assetId) const;
        
        const std::vector<ManifestEntry>& GetAllEntries() const { return m_Entries; }
        void Clear() { m_Entries.clear(); }

    private:
        std::vector<ManifestEntry> m_Entries;
    };

} // namespace Asset
} // namespace Core
