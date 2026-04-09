#pragma once

#include "AssetTypes.h"
#include "Core/Security/PathValidator.h"
#include <filesystem>
#include <memory>
#include <vector>
#include <future>
#include <fstream>

namespace Core {
namespace Asset {

    // Loaded asset data
    struct LoadedTexture {
        CookedTextureHeader Header;
        std::vector<uint8_t> PixelData;
        bool IsValid = false;
    };

    struct LoadedMesh {
        CookedMeshHeader Header;
        std::vector<uint8_t> VertexData;
        std::vector<uint8_t> IndexData;
        bool IsValid = false;
    };

    struct LoadedShader {
        CookedShaderHeader Header;
        std::vector<uint32_t> Spirv;
        std::string Reflection;
        bool IsValid = false;
    };

    // Load status
    enum class LoadStatus {
        Success,
        FileNotFound,
        InvalidMagic,
        VersionMismatch,
        CorruptData,
        ReadError
    };

    // Asset loader - loads cooked assets at runtime
    class AssetLoader {
    public:
        // Synchronous loading
        static LoadedTexture LoadTexture(const std::filesystem::path& cookedPath);
        static LoadedMesh LoadMesh(const std::filesystem::path& cookedPath);
        static LoadedShader LoadShader(const std::filesystem::path& cookedPath);
        
        // Asynchronous loading
        static std::future<LoadedTexture> LoadTextureAsync(const std::filesystem::path& cookedPath);
        static std::future<LoadedMesh> LoadMeshAsync(const std::filesystem::path& cookedPath);
        static std::future<LoadedShader> LoadShaderAsync(const std::filesystem::path& cookedPath);
        
        // Validate cooked asset header
        static LoadStatus ValidateHeader(const std::filesystem::path& cookedPath, AssetType expectedType);
        
        // Read just the header (for inspection)
        static std::optional<CookedAssetHeader> ReadHeader(const std::filesystem::path& cookedPath);
        
    private:
        static bool ReadFile(const std::filesystem::path& path, std::vector<uint8_t>& data, 
                              size_t maxSize = Security::MAX_GENERIC_SIZE);
    };

    // Streaming asset loader for large assets
    class StreamingAssetLoader {
    public:
        struct StreamHandle {
            std::ifstream Stream;
            CookedAssetHeader Header;
            uint64_t CurrentOffset = 0;
            bool IsValid = false;
        };
        
        // Open asset for streaming
        static StreamHandle OpenAsset(const std::filesystem::path& cookedPath);
        
        // Read chunk of data
        static bool ReadChunk(StreamHandle& handle, void* buffer, size_t size);
        
        // Seek to specific mip level (for textures)
        static bool SeekToMipLevel(StreamHandle& handle, const CookedTextureHeader& texHeader, 
                                   uint32_t mipLevel);
        
        // Close handle
        static void CloseAsset(StreamHandle& handle);
    };

} // namespace Asset
} // namespace Core
