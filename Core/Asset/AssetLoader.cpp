#include "AssetLoader.h"
#include "Core/Log.h"
#include <fstream>
#include <cstring>

#define LOG_ERROR(...) ENGINE_CORE_ERROR(__VA_ARGS__)
#define LOG_WARN(...) ENGINE_CORE_WARN(__VA_ARGS__)
#define LOG_TRACE(...) ENGINE_CORE_TRACE(__VA_ARGS__)

namespace Core {
namespace Asset {

    bool AssetLoader::ReadFile(const std::filesystem::path& path, std::vector<uint8_t>& data) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            return false;
        }
        
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        data.resize(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
            return false;
        }
        
        return true;
    }

    LoadStatus AssetLoader::ValidateHeader(const std::filesystem::path& cookedPath, 
                                           AssetType expectedType) {
        std::ifstream file(cookedPath, std::ios::binary);
        if (!file) {
            return LoadStatus::FileNotFound;
        }
        
        CookedAssetHeader header;
        if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) {
            return LoadStatus::ReadError;
        }
        
        if (header.Magic != COOKED_ASSET_MAGIC) {
            return LoadStatus::InvalidMagic;
        }
        
        if (header.Version != COOKED_ASSET_VERSION) {
            return LoadStatus::VersionMismatch;
        }
        
        if (header.Type != expectedType) {
            return LoadStatus::CorruptData;
        }
        
        return LoadStatus::Success;
    }

    std::optional<CookedAssetHeader> AssetLoader::ReadHeader(
        const std::filesystem::path& cookedPath) {
        
        std::ifstream file(cookedPath, std::ios::binary);
        if (!file) {
            return std::nullopt;
        }
        
        CookedAssetHeader header;
        if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) {
            return std::nullopt;
        }
        
        if (header.Magic != COOKED_ASSET_MAGIC) {
            return std::nullopt;
        }
        
        return header;
    }

    LoadedTexture AssetLoader::LoadTexture(const std::filesystem::path& cookedPath) {
        LoadedTexture result;
        
        std::vector<uint8_t> fileData;
        if (!ReadFile(cookedPath, fileData)) {
            LOG_ERROR("Failed to read texture file: {}", cookedPath.string());
            return result;
        }
        
        if (fileData.size() < sizeof(CookedAssetHeader) + sizeof(CookedTextureHeader)) {
            LOG_ERROR("Texture file too small: {}", cookedPath.string());
            return result;
        }
        
        // Read headers
        CookedAssetHeader assetHeader;
        std::memcpy(&assetHeader, fileData.data(), sizeof(assetHeader));
        
        if (assetHeader.Magic != COOKED_ASSET_MAGIC) {
            LOG_ERROR("Invalid magic in texture: {}", cookedPath.string());
            return result;
        }
        
        if (assetHeader.Type != AssetType::Texture) {
            LOG_ERROR("Asset is not a texture: {}", cookedPath.string());
            return result;
        }
        
        std::memcpy(&result.Header, fileData.data() + sizeof(CookedAssetHeader), 
                    sizeof(CookedTextureHeader));
        
        // Copy pixel data
        size_t dataOffset = assetHeader.DataOffset;
        size_t dataSize = assetHeader.DataSize;
        
        if (dataOffset + dataSize > fileData.size()) {
            LOG_ERROR("Corrupt texture data: {}", cookedPath.string());
            return result;
        }
        
        result.PixelData.resize(dataSize);
        std::memcpy(result.PixelData.data(), fileData.data() + dataOffset, dataSize);
        result.IsValid = true;
        
        LOG_TRACE("Loaded texture: {}x{}, {} mips", 
                  result.Header.Width, result.Header.Height, result.Header.MipLevels);
        
        return result;
    }

    LoadedMesh AssetLoader::LoadMesh(const std::filesystem::path& cookedPath) {
        LoadedMesh result;
        
        std::vector<uint8_t> fileData;
        if (!ReadFile(cookedPath, fileData)) {
            LOG_ERROR("Failed to read mesh file: {}", cookedPath.string());
            return result;
        }
        
        if (fileData.size() < sizeof(CookedAssetHeader) + sizeof(CookedMeshHeader)) {
            LOG_ERROR("Mesh file too small: {}", cookedPath.string());
            return result;
        }
        
        // Read headers
        CookedAssetHeader assetHeader;
        std::memcpy(&assetHeader, fileData.data(), sizeof(assetHeader));
        
        if (assetHeader.Magic != COOKED_ASSET_MAGIC) {
            LOG_ERROR("Invalid magic in mesh: {}", cookedPath.string());
            return result;
        }
        
        if (assetHeader.Type != AssetType::Mesh) {
            LOG_ERROR("Asset is not a mesh: {}", cookedPath.string());
            return result;
        }
        
        std::memcpy(&result.Header, fileData.data() + sizeof(CookedAssetHeader), 
                    sizeof(CookedMeshHeader));
        
        // Copy vertex data
        if (result.Header.VertexDataOffset + result.Header.VertexDataSize > fileData.size()) {
            LOG_ERROR("Corrupt vertex data: {}", cookedPath.string());
            return result;
        }
        
        result.VertexData.resize(result.Header.VertexDataSize);
        std::memcpy(result.VertexData.data(), 
                    fileData.data() + result.Header.VertexDataOffset,
                    result.Header.VertexDataSize);
        
        // Copy index data
        if (result.Header.IndexDataOffset + result.Header.IndexDataSize > fileData.size()) {
            LOG_ERROR("Corrupt index data: {}", cookedPath.string());
            return result;
        }
        
        result.IndexData.resize(result.Header.IndexDataSize);
        std::memcpy(result.IndexData.data(),
                    fileData.data() + result.Header.IndexDataOffset,
                    result.Header.IndexDataSize);
        
        result.IsValid = true;
        
        LOG_TRACE("Loaded mesh: {} verts, {} indices", 
                  result.Header.VertexCount, result.Header.IndexCount);
        
        return result;
    }

    LoadedShader AssetLoader::LoadShader(const std::filesystem::path& cookedPath) {
        LoadedShader result;
        
        std::vector<uint8_t> fileData;
        if (!ReadFile(cookedPath, fileData)) {
            LOG_ERROR("Failed to read shader file: {}", cookedPath.string());
            return result;
        }
        
        if (fileData.size() < sizeof(CookedAssetHeader) + sizeof(CookedShaderHeader)) {
            LOG_ERROR("Shader file too small: {}", cookedPath.string());
            return result;
        }
        
        // Read headers
        CookedAssetHeader assetHeader;
        std::memcpy(&assetHeader, fileData.data(), sizeof(assetHeader));
        
        if (assetHeader.Magic != COOKED_ASSET_MAGIC) {
            LOG_ERROR("Invalid magic in shader: {}", cookedPath.string());
            return result;
        }
        
        if (assetHeader.Type != AssetType::Shader) {
            LOG_ERROR("Asset is not a shader: {}", cookedPath.string());
            return result;
        }
        
        std::memcpy(&result.Header, fileData.data() + sizeof(CookedAssetHeader), 
                    sizeof(CookedShaderHeader));
        
        // Copy SPIR-V data
        size_t spirvOffset = assetHeader.DataOffset;
        size_t spirvSize = result.Header.SpirvSize;
        
        if (spirvOffset + spirvSize > fileData.size()) {
            LOG_ERROR("Corrupt shader data: {}", cookedPath.string());
            return result;
        }
        
        result.Spirv.resize(spirvSize / sizeof(uint32_t));
        std::memcpy(result.Spirv.data(), fileData.data() + spirvOffset, spirvSize);
        
        // Copy reflection data
        if (result.Header.ReflectionSize > 0) {
            size_t reflectOffset = result.Header.ReflectionOffset;
            if (reflectOffset + result.Header.ReflectionSize <= fileData.size()) {
                result.Reflection.assign(
                    reinterpret_cast<const char*>(fileData.data() + reflectOffset),
                    result.Header.ReflectionSize);
            }
        }
        
        result.IsValid = true;
        
        LOG_TRACE("Loaded shader: stage {}, {} bytes SPIR-V", 
                  result.Header.Stage, spirvSize);
        
        return result;
    }

    std::future<LoadedTexture> AssetLoader::LoadTextureAsync(
        const std::filesystem::path& cookedPath) {
        return std::async(std::launch::async, [cookedPath]() {
            return LoadTexture(cookedPath);
        });
    }

    std::future<LoadedMesh> AssetLoader::LoadMeshAsync(
        const std::filesystem::path& cookedPath) {
        return std::async(std::launch::async, [cookedPath]() {
            return LoadMesh(cookedPath);
        });
    }

    std::future<LoadedShader> AssetLoader::LoadShaderAsync(
        const std::filesystem::path& cookedPath) {
        return std::async(std::launch::async, [cookedPath]() {
            return LoadShader(cookedPath);
        });
    }

    // StreamingAssetLoader implementation
    StreamingAssetLoader::StreamHandle StreamingAssetLoader::OpenAsset(
        const std::filesystem::path& cookedPath) {
        
        StreamHandle handle;
        
        handle.Stream.open(cookedPath, std::ios::binary);
        if (!handle.Stream) {
            LOG_ERROR("Failed to open asset for streaming: {}", cookedPath.string());
            return handle;
        }
        
        if (!handle.Stream.read(reinterpret_cast<char*>(&handle.Header), 
                                sizeof(handle.Header))) {
            LOG_ERROR("Failed to read asset header: {}", cookedPath.string());
            handle.Stream.close();
            return handle;
        }
        
        if (handle.Header.Magic != COOKED_ASSET_MAGIC) {
            LOG_ERROR("Invalid magic in streamed asset: {}", cookedPath.string());
            handle.Stream.close();
            return handle;
        }
        
        handle.CurrentOffset = sizeof(CookedAssetHeader);
        handle.IsValid = true;
        
        return handle;
    }

    bool StreamingAssetLoader::ReadChunk(StreamHandle& handle, void* buffer, size_t size) {
        if (!handle.IsValid || !handle.Stream) {
            return false;
        }
        
        if (!handle.Stream.read(reinterpret_cast<char*>(buffer), 
                                static_cast<std::streamsize>(size))) {
            return false;
        }
        
        handle.CurrentOffset += size;
        return true;
    }

    bool StreamingAssetLoader::SeekToMipLevel(StreamHandle& handle,
                                               const CookedTextureHeader& texHeader,
                                               uint32_t mipLevel) {
        if (!handle.IsValid || mipLevel >= texHeader.MipLevels) {
            return false;
        }
        
        uint64_t offset = handle.Header.DataOffset + texHeader.MipOffsets[mipLevel];
        handle.Stream.seekg(static_cast<std::streamoff>(offset));
        handle.CurrentOffset = offset;
        
        return handle.Stream.good();
    }

    void StreamingAssetLoader::CloseAsset(StreamHandle& handle) {
        if (handle.Stream.is_open()) {
            handle.Stream.close();
        }
        handle.IsValid = false;
    }

} // namespace Asset
} // namespace Core
