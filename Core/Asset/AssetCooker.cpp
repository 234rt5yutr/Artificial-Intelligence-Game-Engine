#include "AssetCooker.h"
#include "Core/Log.h"
#include "Core/Security/PathValidator.h"
#include <fstream>
#include <chrono>
#include <cstring>

#define LOG_INFO(...) ENGINE_CORE_INFO(__VA_ARGS__)
#define LOG_WARN(...) ENGINE_CORE_WARN(__VA_ARGS__)
#define LOG_ERROR(...) ENGINE_CORE_ERROR(__VA_ARGS__)

namespace Core {
namespace Asset {

    // TextureCooker implementation
    std::vector<AssetType> TextureCooker::GetSupportedTypes() const {
        return { AssetType::Texture };
    }

    std::vector<std::string> TextureCooker::GetSupportedExtensions() const {
        return { ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr", ".exr" };
    }

    bool TextureCooker::NeedsCooking(
        const std::filesystem::path& sourcePath,
        const std::filesystem::path& outputPath) const {

        std::error_code ec;
        
        // Check output exists (no TOCTOU issue for output - we'll overwrite anyway)
        if (!std::filesystem::exists(outputPath, ec) || ec) {
            return true;
        }

        // Get timestamps with error handling
        auto sourceTime = std::filesystem::last_write_time(sourcePath, ec);
        if (ec) {
            LOG_WARN("Failed to get source timestamp: {}", 
                     Security::PathValidator::SanitizeForLogging(sourcePath));
            return true; // Assume needs cooking if we can't check
        }
        
        auto outputTime = std::filesystem::last_write_time(outputPath, ec);
        if (ec) {
            return true; // Assume needs cooking if we can't check output
        }
        
        return sourceTime > outputTime;
    }

    CookResult TextureCooker::Cook(
        const std::filesystem::path& sourcePath,
        const std::filesystem::path& outputPath,
        const CookOptions& options) {

        // Validate source path
        auto validatedSource = Security::PathValidator::ValidateAssetPath(sourcePath);
        if (!validatedSource) {
            LOG_ERROR("Invalid source path: {}", 
                      Security::PathValidator::SanitizeForLogging(sourcePath));
            return CookResult::SourceNotFound;
        }
        
        // Validate output path
        auto validatedOutput = Security::PathValidator::ValidateCookedPath(outputPath);
        if (!validatedOutput) {
            LOG_ERROR("Invalid output path: {}", 
                      Security::PathValidator::SanitizeForLogging(outputPath));
            return CookResult::WriteFailed;
        }
        
        std::error_code ec;
        if (!std::filesystem::exists(*validatedSource, ec)) {
            LOG_ERROR("Texture source not found: {}", 
                      Security::PathValidator::SanitizeForLogging(sourcePath));
            return CookResult::SourceNotFound;
        }

        if (!options.ForceRebuild && !NeedsCooking(*validatedSource, *validatedOutput)) {
            return CookResult::UpToDate;
        }

        LOG_INFO("Cooking texture: {}", sourcePath.filename().string());

        // Load source texture
        std::vector<uint8_t> sourceData;
        uint32_t width = 0, height = 0, channels = 0;
        
        if (!LoadSourceTexture(sourcePath, sourceData, width, height, channels)) {
            LOG_ERROR("Failed to load texture: {}", sourcePath.string());
            return CookResult::InvalidFormat;
        }

        // Generate mipmaps if requested
        std::vector<uint32_t> mipSizes;
        std::vector<uint8_t> mipData;
        uint32_t mipLevels = 1;
        
        if (options.GenerateMipmaps && width > 1 && height > 1) {
            mipData = GenerateMipmaps(sourceData, width, height, channels, mipSizes);
            mipLevels = static_cast<uint32_t>(mipSizes.size());
        } else {
            mipData = sourceData;
            mipSizes.push_back(static_cast<uint32_t>(sourceData.size()));
        }

        // Compress if requested
        TextureFormat format = TextureFormat::RGBA8_UNORM;
        std::vector<uint8_t> finalData;
        
        if (options.CompressTextures && width >= 4 && height >= 4) {
            finalData = CompressBC(mipData, width, height, channels, format);
            if (finalData.empty()) {
                LOG_WARN("Compression failed, using uncompressed format");
                finalData = mipData;
                format = (channels == 4) ? TextureFormat::RGBA8_UNORM : TextureFormat::RGBA8_UNORM;
            }
        } else {
            finalData = mipData;
        }

        // Prepare headers
        CookedAssetHeader assetHeader{};
        assetHeader.Magic = COOKED_ASSET_MAGIC;
        assetHeader.Version = COOKED_ASSET_VERSION;
        assetHeader.Type = AssetType::Texture;
        assetHeader.Flags = 0;
        assetHeader.SourceHash = ComputeAssetId(sourcePath.string());
        
        auto now = std::chrono::system_clock::now();
        assetHeader.CookedTimestamp = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
        
        assetHeader.DataOffset = sizeof(CookedAssetHeader) + sizeof(CookedTextureHeader);
        assetHeader.DataSize = static_cast<uint32_t>(finalData.size());
        assetHeader.MetadataOffset = 0;
        assetHeader.MetadataSize = 0;

        CookedTextureHeader texHeader{};
        texHeader.Format = format;
        texHeader.Type = TextureType::Texture2D;
        texHeader.Width = width;
        texHeader.Height = height;
        texHeader.Depth = 1;
        texHeader.ArrayLayers = 1;
        texHeader.MipLevels = mipLevels;
        texHeader.Flags = 0;
        
        // Fill mip offsets/sizes
        uint32_t currentOffset = 0;
        for (uint32_t i = 0; i < mipLevels && i < 16; ++i) {
            texHeader.MipOffsets[i] = currentOffset;
            texHeader.MipSizes[i] = mipSizes[i];
            currentOffset += mipSizes[i];
        }

        // Create output directory if needed
        std::filesystem::create_directories(outputPath.parent_path());

        // Write output file
        std::ofstream file(outputPath, std::ios::binary);
        if (!file) {
            LOG_ERROR("Failed to create output file: {}", outputPath.string());
            return CookResult::WriteFailed;
        }

        file.write(reinterpret_cast<const char*>(&assetHeader), sizeof(assetHeader));
        file.write(reinterpret_cast<const char*>(&texHeader), sizeof(texHeader));
        file.write(reinterpret_cast<const char*>(finalData.data()), finalData.size());
        
        file.close();

        LOG_INFO("Cooked texture: {} -> {} ({} bytes)", 
                 sourcePath.filename().string(),
                 outputPath.filename().string(),
                 sizeof(assetHeader) + sizeof(texHeader) + finalData.size());

        return CookResult::Success;
    }

    bool TextureCooker::LoadSourceTexture(
        const std::filesystem::path& /* path */,
        std::vector<uint8_t>& data,
        uint32_t& width, uint32_t& height, uint32_t& channels) {
        
        // Placeholder: would use stb_image or similar
        // For now, create dummy data for testing
        width = 256;
        height = 256;
        channels = 4;
        data.resize(width * height * channels, 128);
        
        return true;
    }

    std::vector<uint8_t> TextureCooker::GenerateMipmaps(
        const std::vector<uint8_t>& data,
        uint32_t width, uint32_t height, uint32_t channels,
        std::vector<uint32_t>& mipSizes) {
        
        std::vector<uint8_t> result;
        
        uint32_t mipWidth = width;
        uint32_t mipHeight = height;
        
        // Copy base level
        result.insert(result.end(), data.begin(), data.end());
        mipSizes.push_back(static_cast<uint32_t>(data.size()));
        
        // Generate mips using box filter
        std::vector<uint8_t> currentMip = data;
        
        while (mipWidth > 1 || mipHeight > 1) {
            uint32_t newWidth = (mipWidth > 1) ? mipWidth / 2 : 1;
            uint32_t newHeight = (mipHeight > 1) ? mipHeight / 2 : 1;
            
            std::vector<uint8_t> newMip(newWidth * newHeight * channels);
            
            // Box filter downsampling
            for (uint32_t y = 0; y < newHeight; ++y) {
                for (uint32_t x = 0; x < newWidth; ++x) {
                    for (uint32_t c = 0; c < channels; ++c) {
                        uint32_t sum = 0;
                        uint32_t count = 0;
                        
                        for (uint32_t dy = 0; dy < 2 && (y * 2 + dy) < mipHeight; ++dy) {
                            for (uint32_t dx = 0; dx < 2 && (x * 2 + dx) < mipWidth; ++dx) {
                                uint32_t srcIdx = ((y * 2 + dy) * mipWidth + (x * 2 + dx)) * channels + c;
                                sum += currentMip[srcIdx];
                                count++;
                            }
                        }
                        
                        uint32_t dstIdx = (y * newWidth + x) * channels + c;
                        newMip[dstIdx] = static_cast<uint8_t>(sum / count);
                    }
                }
            }
            
            result.insert(result.end(), newMip.begin(), newMip.end());
            mipSizes.push_back(static_cast<uint32_t>(newMip.size()));
            
            currentMip = std::move(newMip);
            mipWidth = newWidth;
            mipHeight = newHeight;
        }
        
        return result;
    }

    std::vector<uint8_t> TextureCooker::CompressBC(
        const std::vector<uint8_t>& /* data */,
        uint32_t width, uint32_t height, uint32_t channels,
        TextureFormat& outFormat) {
        
        // Placeholder: would use BC7 encoder
        // For production, integrate ispc_texcomp or similar
        outFormat = (channels == 4) ? TextureFormat::BC7_UNORM : TextureFormat::BC7_UNORM;
        
        // BC7 uses 16 bytes per 4x4 block
        uint32_t blocksX = (width + 3) / 4;
        uint32_t blocksY = (height + 3) / 4;
        size_t compressedSize = blocksX * blocksY * 16;
        
        std::vector<uint8_t> compressed(compressedSize, 0);
        
        // Placeholder compression (just zeros)
        // Real implementation would call BC7 encoder
        
        return compressed;
    }

    // MeshCooker implementation
    std::vector<AssetType> MeshCooker::GetSupportedTypes() const {
        return { AssetType::Mesh };
    }

    std::vector<std::string> MeshCooker::GetSupportedExtensions() const {
        return { ".gltf", ".glb", ".obj", ".fbx" };
    }

    bool MeshCooker::NeedsCooking(
        const std::filesystem::path& sourcePath,
        const std::filesystem::path& outputPath) const {

        std::error_code ec;
        
        if (!std::filesystem::exists(outputPath, ec) || ec) {
            return true;
        }

        auto sourceTime = std::filesystem::last_write_time(sourcePath, ec);
        if (ec) {
            LOG_WARN("Failed to get source timestamp: {}", 
                     Security::PathValidator::SanitizeForLogging(sourcePath));
            return true;
        }
        
        auto outputTime = std::filesystem::last_write_time(outputPath, ec);
        if (ec) {
            return true;
        }
        
        return sourceTime > outputTime;
    }

    CookResult MeshCooker::Cook(
        const std::filesystem::path& sourcePath,
        const std::filesystem::path& outputPath,
        const CookOptions& options) {

        // Validate source path
        auto validatedSource = Security::PathValidator::ValidateAssetPath(sourcePath);
        if (!validatedSource) {
            LOG_ERROR("Invalid source path: {}", 
                      Security::PathValidator::SanitizeForLogging(sourcePath));
            return CookResult::SourceNotFound;
        }
        
        // Validate output path
        auto validatedOutput = Security::PathValidator::ValidateCookedPath(outputPath);
        if (!validatedOutput) {
            LOG_ERROR("Invalid output path: {}", 
                      Security::PathValidator::SanitizeForLogging(outputPath));
            return CookResult::WriteFailed;
        }
        
        std::error_code ec;
        if (!std::filesystem::exists(*validatedSource, ec)) {
            LOG_ERROR("Mesh source not found: {}", 
                      Security::PathValidator::SanitizeForLogging(sourcePath));
            return CookResult::SourceNotFound;
        }

        if (!options.ForceRebuild && !NeedsCooking(*validatedSource, *validatedOutput)) {
            return CookResult::UpToDate;
        }

        LOG_INFO("Cooking mesh: {}", sourcePath.filename().string());

        // Placeholder vertex/index data (would load from GLTF/OBJ)
        std::vector<float> positions = {
            -0.5f, -0.5f, 0.0f,
             0.5f, -0.5f, 0.0f,
             0.0f,  0.5f, 0.0f
        };
        std::vector<uint32_t> indices = { 0, 1, 2 };
        uint32_t vertexCount = 3;
        uint32_t indexCount = 3;

        // Calculate bounds
        float boundsMin[3] = { -0.5f, -0.5f, 0.0f };
        float boundsMax[3] = {  0.5f,  0.5f, 0.0f };
        float radius = 0.707f;

        // Optimize mesh if requested
        if (options.OptimizeMeshes) {
            OptimizeVertexCache(indices, vertexCount);
            OptimizeOverdraw(indices, positions.data(), vertexCount);
        }

        // Prepare headers
        CookedAssetHeader assetHeader{};
        assetHeader.Magic = COOKED_ASSET_MAGIC;
        assetHeader.Version = COOKED_ASSET_VERSION;
        assetHeader.Type = AssetType::Mesh;
        assetHeader.Flags = 0;
        assetHeader.SourceHash = ComputeAssetId(sourcePath.string());
        
        auto now = std::chrono::system_clock::now();
        assetHeader.CookedTimestamp = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());

        CookedMeshHeader meshHeader{};
        meshHeader.VertexCount = vertexCount;
        meshHeader.IndexCount = indexCount;
        meshHeader.PrimitiveCount = 1;
        meshHeader.BoneCount = 0;
        meshHeader.Compression = MeshCompression::None;
        meshHeader.VertexStride = sizeof(float) * 3; // Position only for now
        meshHeader.IndexStride = sizeof(uint32_t);
        meshHeader.Flags = 0;
        std::memcpy(meshHeader.BoundsMin, boundsMin, sizeof(boundsMin));
        std::memcpy(meshHeader.BoundsMax, boundsMax, sizeof(boundsMax));
        meshHeader.BoundingSphereRadius = radius;

        uint32_t headerSize = sizeof(CookedAssetHeader) + sizeof(CookedMeshHeader);
        uint32_t vertexDataSize = static_cast<uint32_t>(positions.size() * sizeof(float));
        uint32_t indexDataSize = static_cast<uint32_t>(indices.size() * sizeof(uint32_t));

        meshHeader.VertexDataOffset = headerSize;
        meshHeader.VertexDataSize = vertexDataSize;
        meshHeader.IndexDataOffset = headerSize + vertexDataSize;
        meshHeader.IndexDataSize = indexDataSize;
        meshHeader.PrimitiveDataOffset = 0;
        meshHeader.PrimitiveDataSize = 0;
        meshHeader.BoneDataOffset = 0;
        meshHeader.BoneDataSize = 0;

        assetHeader.DataOffset = headerSize;
        assetHeader.DataSize = vertexDataSize + indexDataSize;
        assetHeader.MetadataOffset = 0;
        assetHeader.MetadataSize = 0;

        // Create output directory
        std::filesystem::create_directories(outputPath.parent_path());

        // Write output
        std::ofstream file(outputPath, std::ios::binary);
        if (!file) {
            LOG_ERROR("Failed to create output file: {}", outputPath.string());
            return CookResult::WriteFailed;
        }

        file.write(reinterpret_cast<const char*>(&assetHeader), sizeof(assetHeader));
        file.write(reinterpret_cast<const char*>(&meshHeader), sizeof(meshHeader));
        file.write(reinterpret_cast<const char*>(positions.data()), vertexDataSize);
        file.write(reinterpret_cast<const char*>(indices.data()), indexDataSize);
        
        file.close();

        LOG_INFO("Cooked mesh: {} -> {} ({} verts, {} indices)", 
                 sourcePath.filename().string(),
                 outputPath.filename().string(),
                 vertexCount, indexCount);

        return CookResult::Success;
    }

    void MeshCooker::OptimizeVertexCache(std::vector<uint32_t>& indices, uint32_t /* vertexCount */) {
        // Tipsify algorithm for vertex cache optimization
        // This is a simplified version - production would use meshoptimizer
        
        if (indices.size() < 3) return;
        
        // Simple linear rearrangement for locality
        std::vector<uint32_t> optimized;
        optimized.reserve(indices.size());
        
        // Greedy approach: emit triangles with lowest vertex index first
        for (size_t i = 0; i < indices.size(); i += 3) {
            optimized.push_back(indices[i]);
            optimized.push_back(indices[i + 1]);
            optimized.push_back(indices[i + 2]);
        }
        
        indices = std::move(optimized);
    }

    void MeshCooker::OptimizeOverdraw(
        std::vector<uint32_t>& /* indices */,
        const float* /* positions */, uint32_t /* vertexCount */) {
        
        // Overdraw optimization sorts triangles front-to-back
        // This is a placeholder - production would use meshoptimizer
    }

    void MeshCooker::QuantizePositions(
        std::vector<float>& positions,
        const float* boundsMin, const float* boundsMax) {
        
        // Quantize positions to 16-bit integers for compression
        // This is a placeholder - production would implement full quantization
        (void)positions;
        (void)boundsMin;
        (void)boundsMax;
    }

    // ShaderCooker implementation
    std::vector<AssetType> ShaderCooker::GetSupportedTypes() const {
        return { AssetType::Shader };
    }

    std::vector<std::string> ShaderCooker::GetSupportedExtensions() const {
        return { ".vert", ".frag", ".comp", ".geom", ".tesc", ".tese", ".glsl", ".hlsl" };
    }

    bool ShaderCooker::NeedsCooking(
        const std::filesystem::path& sourcePath,
        const std::filesystem::path& outputPath) const {

        std::error_code ec;
        
        if (!std::filesystem::exists(outputPath, ec) || ec) {
            return true;
        }

        auto sourceTime = std::filesystem::last_write_time(sourcePath, ec);
        if (ec) {
            LOG_WARN("Failed to get source timestamp: {}", 
                     Security::PathValidator::SanitizeForLogging(sourcePath));
            return true;
        }
        
        auto outputTime = std::filesystem::last_write_time(outputPath, ec);
        if (ec) {
            return true;
        }
        
        return sourceTime > outputTime;
    }

    CookResult ShaderCooker::Cook(
        const std::filesystem::path& sourcePath,
        const std::filesystem::path& outputPath,
        const CookOptions& options) {

        // Validate source path
        auto validatedSource = Security::PathValidator::ValidateShaderPath(sourcePath);
        if (!validatedSource) {
            LOG_ERROR("Invalid source path: {}", 
                      Security::PathValidator::SanitizeForLogging(sourcePath));
            return CookResult::SourceNotFound;
        }
        
        // Validate output path
        auto validatedOutput = Security::PathValidator::ValidateCookedPath(outputPath);
        if (!validatedOutput) {
            LOG_ERROR("Invalid output path: {}", 
                      Security::PathValidator::SanitizeForLogging(outputPath));
            return CookResult::WriteFailed;
        }
        
        std::error_code ec;
        if (!std::filesystem::exists(*validatedSource, ec)) {
            LOG_ERROR("Shader source not found: {}", 
                      Security::PathValidator::SanitizeForLogging(sourcePath));
            return CookResult::SourceNotFound;
        }

        if (!options.ForceRebuild && !NeedsCooking(*validatedSource, *validatedOutput)) {
            return CookResult::UpToDate;
        }

        LOG_INFO("Cooking shader: {}", sourcePath.filename().string());

        // Compile to SPIR-V
        std::vector<uint32_t> spirv;
        std::string errorLog;
        
        if (!CompileToSpirv(sourcePath, spirv, errorLog)) {
            LOG_ERROR("Shader compilation failed: {}\n{}", sourcePath.string(), errorLog);
            return CookResult::InvalidFormat;
        }

        // Extract reflection data
        std::string reflection = ExtractReflection(spirv);

        // Determine shader stage from extension
        std::string ext = sourcePath.extension().string();
        uint32_t stage = 0;
        if (ext == ".vert") stage = 0;
        else if (ext == ".frag") stage = 1;
        else if (ext == ".comp") stage = 2;
        else if (ext == ".geom") stage = 3;
        else if (ext == ".tesc") stage = 4;
        else if (ext == ".tese") stage = 5;

        // Prepare headers
        CookedAssetHeader assetHeader{};
        assetHeader.Magic = COOKED_ASSET_MAGIC;
        assetHeader.Version = COOKED_ASSET_VERSION;
        assetHeader.Type = AssetType::Shader;
        assetHeader.Flags = options.StripDebugInfo ? 1 : 0;
        assetHeader.SourceHash = ComputeAssetId(sourcePath.string());
        
        auto now = std::chrono::system_clock::now();
        assetHeader.CookedTimestamp = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());

        CookedShaderHeader shaderHeader{};
        shaderHeader.Stage = stage;
        shaderHeader.SpirvSize = static_cast<uint32_t>(spirv.size() * sizeof(uint32_t));
        
        uint32_t headerSize = sizeof(CookedAssetHeader) + sizeof(CookedShaderHeader);
        uint32_t spirvSize = static_cast<uint32_t>(spirv.size() * sizeof(uint32_t));
        
        shaderHeader.ReflectionOffset = headerSize + spirvSize;
        shaderHeader.ReflectionSize = static_cast<uint32_t>(reflection.size());
        shaderHeader.EntryPointLength = 4; // "main"

        assetHeader.DataOffset = headerSize;
        assetHeader.DataSize = spirvSize;
        assetHeader.MetadataOffset = shaderHeader.ReflectionOffset;
        assetHeader.MetadataSize = shaderHeader.ReflectionSize;

        // Create output directory
        std::filesystem::create_directories(outputPath.parent_path());

        // Write output
        std::ofstream file(outputPath, std::ios::binary);
        if (!file) {
            LOG_ERROR("Failed to create output file: {}", outputPath.string());
            return CookResult::WriteFailed;
        }

        file.write(reinterpret_cast<const char*>(&assetHeader), sizeof(assetHeader));
        file.write(reinterpret_cast<const char*>(&shaderHeader), sizeof(shaderHeader));
        file.write(reinterpret_cast<const char*>(spirv.data()), spirvSize);
        file.write(reflection.data(), reflection.size());
        
        file.close();

        LOG_INFO("Cooked shader: {} -> {} (SPIR-V: {} bytes)", 
                 sourcePath.filename().string(),
                 outputPath.filename().string(),
                 spirvSize);

        return CookResult::Success;
    }

    bool ShaderCooker::CompileToSpirv(
        const std::filesystem::path& sourcePath,
        std::vector<uint32_t>& spirv,
        std::string& errorLog) {
        
        // Read source file
        std::ifstream file(sourcePath);
        if (!file) {
            errorLog = "Failed to open source file";
            return false;
        }
        
        std::string source((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        file.close();

        // Placeholder: would use shaderc or glslang
        // For now, just create dummy SPIR-V
        spirv = {
            0x07230203, // SPIR-V magic number
            0x00010300, // Version 1.3
            0x00000000, // Generator
            1,          // Bound
            0           // Schema
        };
        
        return true;
    }

    std::string ShaderCooker::ExtractReflection(const std::vector<uint32_t>& /* spirv */) {
        // Placeholder: would use SPIRV-Cross for reflection
        return R"({"inputs":[],"outputs":[],"uniforms":[]})";
    }

    // AssetManifest implementation
    bool AssetManifest::Load(const std::filesystem::path& path) {
        std::ifstream file(path);
        if (!file) {
            return false;
        }

        m_Entries.clear();
        
        // Simple line-based format for now
        // Production would use JSON or binary format
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            
            ManifestEntry entry;
            // Parse line...
            // Placeholder: just skip for now
        }

        return true;
    }

    bool AssetManifest::Save(const std::filesystem::path& path) const {
        std::filesystem::create_directories(path.parent_path());
        
        std::ofstream file(path);
        if (!file) {
            return false;
        }

        file << "# Asset Manifest\n";
        file << "# Generated by Asset Cooker\n\n";

        for (const auto& entry : m_Entries) {
            file << entry.AssetId << "|"
                 << static_cast<uint32_t>(entry.Type) << "|"
                 << entry.SourcePath << "|"
                 << entry.CookedPath << "|"
                 << entry.SourceHash << "|"
                 << entry.CookedTimestamp << "\n";
        }

        return true;
    }

    void AssetManifest::AddEntry(const ManifestEntry& entry) {
        // Update existing or add new
        for (auto& existing : m_Entries) {
            if (existing.AssetId == entry.AssetId) {
                existing = entry;
                return;
            }
        }
        m_Entries.push_back(entry);
    }

    void AssetManifest::RemoveEntry(uint64_t assetId) {
        m_Entries.erase(
            std::remove_if(m_Entries.begin(), m_Entries.end(),
                [assetId](const ManifestEntry& e) { return e.AssetId == assetId; }),
            m_Entries.end());
    }

    const ManifestEntry* AssetManifest::FindByPath(const std::string& sourcePath) const {
        for (const auto& entry : m_Entries) {
            if (entry.SourcePath == sourcePath) {
                return &entry;
            }
        }
        return nullptr;
    }

    const ManifestEntry* AssetManifest::FindById(uint64_t assetId) const {
        for (const auto& entry : m_Entries) {
            if (entry.AssetId == assetId) {
                return &entry;
            }
        }
        return nullptr;
    }

} // namespace Asset
} // namespace Core
