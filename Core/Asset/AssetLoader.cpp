#include "AssetLoader.h"
#include "Core/Log.h"
#include "Core/Security/PathValidator.h"
#include <fstream>
#include <cstring>

#define LOG_ERROR(...) ENGINE_CORE_ERROR(__VA_ARGS__)
#define LOG_WARN(...) ENGINE_CORE_WARN(__VA_ARGS__)
#define LOG_TRACE(...) ENGINE_CORE_TRACE(__VA_ARGS__)

namespace Core {
namespace Asset {
namespace {

    LoadedStructuredAsset LoadStructuredAssetInternal(const std::filesystem::path& cookedPath,
                                                      AssetType expectedType) {
        LoadedStructuredAsset result;

        std::vector<uint8_t> fileData;

        auto validatedPath = Security::PathValidator::ValidateCookedPath(cookedPath);
        if (!validatedPath) {
            LOG_ERROR("Path validation failed: {}",
                      Security::PathValidator::SanitizeForLogging(cookedPath));
            return result;
        }

        std::ifstream file(*validatedPath, std::ios::binary | std::ios::ate);
        if (!file) {
            LOG_ERROR("Failed to open structured asset file: {}",
                      Security::PathValidator::SanitizeForLogging(cookedPath));
            return result;
        }

        const std::streamsize size = file.tellg();
        if (size < 0 || static_cast<size_t>(size) > Security::MAX_GENERIC_SIZE) {
            LOG_ERROR("Structured asset size invalid: {}",
                      Security::PathValidator::SanitizeForLogging(cookedPath));
            return result;
        }

        file.seekg(0, std::ios::beg);
        fileData.resize(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(fileData.data()), size)) {
            LOG_ERROR("Failed to read structured asset bytes: {}",
                      Security::PathValidator::SanitizeForLogging(cookedPath));
            return result;
        }

        if (fileData.size() < sizeof(CookedAssetHeader)) {
            LOG_ERROR("Structured asset file too small: {}",
                      Security::PathValidator::SanitizeForLogging(cookedPath));
            return result;
        }

        CookedAssetHeader header{};
        std::memcpy(&header, fileData.data(), sizeof(header));

        if (header.Magic != COOKED_ASSET_MAGIC || header.Type != expectedType) {
            LOG_ERROR("Structured asset type mismatch: {}",
                      Security::PathValidator::SanitizeForLogging(cookedPath));
            return result;
        }

        if (header.DataOffset + header.DataSize > fileData.size()) {
            LOG_ERROR("Structured asset payload out of bounds: {}",
                      Security::PathValidator::SanitizeForLogging(cookedPath));
            return result;
        }

        const char* dataBegin = reinterpret_cast<const char*>(fileData.data() + header.DataOffset);
        const char* dataEnd = dataBegin + header.DataSize;

        try {
            result.Document = nlohmann::json::parse(dataBegin, dataEnd);
        } catch (const nlohmann::json::parse_error&) {
            LOG_ERROR("Structured asset contains invalid JSON: {}",
                      Security::PathValidator::SanitizeForLogging(cookedPath));
            return result;
        }

        result.Type = expectedType;
        result.IsValid = true;
        return result;
    }

} // namespace

    bool AssetLoader::ReadFile(const std::filesystem::path& path, std::vector<uint8_t>& data, 
                                 size_t maxSize) {
        // Validate path against traversal attacks
        auto validatedPath = Security::PathValidator::ValidateCookedPath(path);
        if (!validatedPath) {
            LOG_ERROR("Path validation failed: {}", 
                      Security::PathValidator::SanitizeForLogging(path));
            return false;
        }
        
        std::ifstream file(*validatedPath, std::ios::binary | std::ios::ate);
        if (!file) {
            return false;
        }
        
        std::streamsize size = file.tellg();
        
        // Validate file size against limit
        if (size < 0) {
            LOG_ERROR("Invalid file size for: {}", 
                      Security::PathValidator::SanitizeForLogging(path));
            return false;
        }
        
        if (static_cast<size_t>(size) > maxSize) {
            LOG_ERROR("File size {} exceeds limit {} for: {}", 
                      size, maxSize, Security::PathValidator::SanitizeForLogging(path));
            return false;
        }
        
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
        if (!ReadFile(cookedPath, fileData, Security::MAX_TEXTURE_SIZE)) {
            LOG_ERROR("Failed to read texture file: {}", 
                      Security::PathValidator::SanitizeForLogging(cookedPath));
            return result;
        }
        
        if (fileData.size() < sizeof(CookedAssetHeader) + sizeof(CookedTextureHeader)) {
            LOG_ERROR("Texture file too small: {}", 
                      Security::PathValidator::SanitizeForLogging(cookedPath));
            return result;
        }
        
        // Read headers
        CookedAssetHeader assetHeader;
        std::memcpy(&assetHeader, fileData.data(), sizeof(assetHeader));
        
        if (assetHeader.Magic != COOKED_ASSET_MAGIC) {
            LOG_ERROR("Invalid magic in texture: {}", 
                      Security::PathValidator::SanitizeForLogging(cookedPath));
            return result;
        }
        
        if (assetHeader.Type != AssetType::Texture) {
            LOG_ERROR("Asset is not a texture: {}", 
                      Security::PathValidator::SanitizeForLogging(cookedPath));
            return result;
        }
        
        std::memcpy(&result.Header, fileData.data() + sizeof(CookedAssetHeader), 
                    sizeof(CookedTextureHeader));
        
        // Copy pixel data
        size_t dataOffset = assetHeader.DataOffset;
        size_t dataSize = assetHeader.DataSize;
        
        if (dataOffset + dataSize > fileData.size()) {
            LOG_ERROR("Corrupt texture data: {}", 
                      Security::PathValidator::SanitizeForLogging(cookedPath));
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
        if (!ReadFile(cookedPath, fileData, Security::MAX_MESH_SIZE)) {
            LOG_ERROR("Failed to read mesh file: {}", 
                      Security::PathValidator::SanitizeForLogging(cookedPath));
            return result;
        }
        
        if (fileData.size() < sizeof(CookedAssetHeader) + sizeof(CookedMeshHeader)) {
            LOG_ERROR("Mesh file too small: {}", 
                      Security::PathValidator::SanitizeForLogging(cookedPath));
            return result;
        }
        
        // Read headers
        CookedAssetHeader assetHeader;
        std::memcpy(&assetHeader, fileData.data(), sizeof(assetHeader));
        
        if (assetHeader.Magic != COOKED_ASSET_MAGIC) {
            LOG_ERROR("Invalid magic in mesh: {}", 
                      Security::PathValidator::SanitizeForLogging(cookedPath));
            return result;
        }
        
        if (assetHeader.Type != AssetType::Mesh) {
            LOG_ERROR("Asset is not a mesh: {}", 
                      Security::PathValidator::SanitizeForLogging(cookedPath));
            return result;
        }
        
        std::memcpy(&result.Header, fileData.data() + sizeof(CookedAssetHeader), 
                    sizeof(CookedMeshHeader));
        
        // Copy vertex data
        if (result.Header.VertexDataOffset + result.Header.VertexDataSize > fileData.size()) {
            LOG_ERROR("Corrupt vertex data: {}", 
                      Security::PathValidator::SanitizeForLogging(cookedPath));
            return result;
        }
        
        result.VertexData.resize(result.Header.VertexDataSize);
        std::memcpy(result.VertexData.data(), 
                    fileData.data() + result.Header.VertexDataOffset,
                    result.Header.VertexDataSize);
        
        // Copy index data
        if (result.Header.IndexDataOffset + result.Header.IndexDataSize > fileData.size()) {
            LOG_ERROR("Corrupt index data: {}", 
                      Security::PathValidator::SanitizeForLogging(cookedPath));
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
        if (!ReadFile(cookedPath, fileData, Security::MAX_SHADER_SIZE)) {
            LOG_ERROR("Failed to read shader file: {}", 
                      Security::PathValidator::SanitizeForLogging(cookedPath));
            return result;
        }
        
        if (fileData.size() < sizeof(CookedAssetHeader) + sizeof(CookedShaderHeader)) {
            LOG_ERROR("Shader file too small: {}", 
                      Security::PathValidator::SanitizeForLogging(cookedPath));
            return result;
        }
        
        // Read headers
        CookedAssetHeader assetHeader;
        std::memcpy(&assetHeader, fileData.data(), sizeof(assetHeader));
        
        if (assetHeader.Magic != COOKED_ASSET_MAGIC) {
            LOG_ERROR("Invalid magic in shader: {}", 
                      Security::PathValidator::SanitizeForLogging(cookedPath));
            return result;
        }
        
        if (assetHeader.Type != AssetType::Shader) {
            LOG_ERROR("Asset is not a shader: {}", 
                      Security::PathValidator::SanitizeForLogging(cookedPath));
            return result;
        }
        
        std::memcpy(&result.Header, fileData.data() + sizeof(CookedAssetHeader), 
                    sizeof(CookedShaderHeader));
        
        // Copy SPIR-V data
        size_t spirvOffset = assetHeader.DataOffset;
        size_t spirvSize = result.Header.SpirvSize;
        
        if (spirvOffset + spirvSize > fileData.size()) {
            LOG_ERROR("Corrupt shader data: {}", 
                      Security::PathValidator::SanitizeForLogging(cookedPath));
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

    LoadedStructuredAsset AssetLoader::LoadPrefab(const std::filesystem::path& cookedPath) {
        return LoadStructuredAssetInternal(cookedPath, AssetType::Prefab);
    }

    LoadedStructuredAsset AssetLoader::LoadVisualScriptGraph(const std::filesystem::path& cookedPath) {
        return LoadStructuredAssetInternal(cookedPath, AssetType::VisualScriptGraph);
    }

    LoadedStructuredAsset AssetLoader::LoadTimeline(const std::filesystem::path& cookedPath) {
        return LoadStructuredAssetInternal(cookedPath, AssetType::Timeline);
    }

    LoadedStructuredAsset AssetLoader::LoadSceneAsset(const std::filesystem::path& cookedPath) {
        return LoadStructuredAssetInternal(cookedPath, AssetType::Scene);
    }

    LoadedStructuredAsset AssetLoader::LoadWorldPartitionCellAsset(const std::filesystem::path& cookedPath) {
        return LoadStructuredAssetInternal(cookedPath, AssetType::WorldPartitionCell);
    }

    LoadedStructuredAsset AssetLoader::LoadHierarchicalLODAsset(const std::filesystem::path& cookedPath) {
        return LoadStructuredAssetInternal(cookedPath, AssetType::HierarchicalLOD);
    }

    LoadedStructuredAsset AssetLoader::LoadAnimationGraphAsset(const std::filesystem::path& cookedPath) {
        return LoadStructuredAssetInternal(cookedPath, AssetType::AnimationGraph);
    }

    LoadedStructuredAsset AssetLoader::LoadRetargetProfileAsset(const std::filesystem::path& cookedPath) {
        return LoadStructuredAssetInternal(cookedPath, AssetType::RetargetProfile);
    }

    LoadedStructuredAsset AssetLoader::LoadControlRigAsset(const std::filesystem::path& cookedPath) {
        return LoadStructuredAssetInternal(cookedPath, AssetType::ControlRig);
    }

    LoadedStructuredAsset AssetLoader::LoadMotionDatabaseAsset(const std::filesystem::path& cookedPath) {
        return LoadStructuredAssetInternal(cookedPath, AssetType::MotionDatabase);
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
        
        // Validate path against traversal attacks
        auto validatedPath = Security::PathValidator::ValidateCookedPath(cookedPath);
        if (!validatedPath) {
            LOG_ERROR("Path validation failed: {}", 
                      Security::PathValidator::SanitizeForLogging(cookedPath));
            return handle;
        }
        
        handle.Stream.open(*validatedPath, std::ios::binary);
        if (!handle.Stream) {
            LOG_ERROR("Failed to open asset for streaming: {}", 
                      Security::PathValidator::SanitizeForLogging(cookedPath));
            return handle;
        }
        
        if (!handle.Stream.read(reinterpret_cast<char*>(&handle.Header), 
                                sizeof(handle.Header))) {
            LOG_ERROR("Failed to read asset header: {}", 
                      Security::PathValidator::SanitizeForLogging(cookedPath));
            handle.Stream.close();
            return handle;
        }
        
        if (handle.Header.Magic != COOKED_ASSET_MAGIC) {
            LOG_ERROR("Invalid magic in streamed asset: {}", 
                      Security::PathValidator::SanitizeForLogging(cookedPath));
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
