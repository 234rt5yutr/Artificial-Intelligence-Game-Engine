#pragma once

#include <cstdint>
#include <string>
#include <array>

namespace Core {
namespace Asset {

    // 4-byte magic identifier for cooked asset files
    constexpr uint32_t COOKED_ASSET_MAGIC = 0x434B4153; // "CKAS" in ASCII
    constexpr uint32_t COOKED_ASSET_VERSION = 1;
    constexpr uint32_t ADDRESSABLE_CATALOG_SCHEMA_VERSION = 1;
    constexpr uint32_t ASSET_BUNDLE_SCHEMA_VERSION = 1;
    constexpr uint32_t ASSET_BUNDLE_PATCH_SCHEMA_VERSION = 1;

    // Asset type identifiers
    enum class AssetType : uint32_t {
        Unknown = 0,
        Texture = 1,
        Mesh = 2,
        Material = 3,
        Shader = 4,
        Audio = 5,
        Animation = 6,
        Prefab = 7,
        VisualScriptGraph = 8,
        Timeline = 9,
        Level = 10,
        Font = 11,
        Script = 12,
        Scene = 13,
        WorldPartitionCell = 14,
        HierarchicalLOD = 15,
        AddressablesCatalog = 16,
        AssetBundle = 17,
        AssetBundlePatch = 18
    };

    // Texture-specific enums
    enum class TextureFormat : uint32_t {
        Unknown = 0,
        R8_UNORM = 1,
        RG8_UNORM = 2,
        RGBA8_UNORM = 3,
        RGBA8_SRGB = 4,
        R16_FLOAT = 5,
        RG16_FLOAT = 6,
        RGBA16_FLOAT = 7,
        R32_FLOAT = 8,
        RG32_FLOAT = 9,
        RGBA32_FLOAT = 10,
        BC1_UNORM = 11,      // DXT1 - RGB
        BC1_SRGB = 12,
        BC3_UNORM = 13,      // DXT5 - RGBA
        BC3_SRGB = 14,
        BC4_UNORM = 15,      // Single channel
        BC5_UNORM = 16,      // Two channels (normal maps)
        BC7_UNORM = 17,      // High quality RGBA
        BC7_SRGB = 18,
        BC6H_UFLOAT = 19,    // HDR
        BC6H_SFLOAT = 20
    };

    enum class TextureType : uint32_t {
        Unknown = 0,
        Texture2D = 1,
        TextureCube = 2,
        Texture3D = 3,
        Texture2DArray = 4
    };

    // Mesh compression modes
    enum class MeshCompression : uint32_t {
        None = 0,
        Quantized = 1,       // Position/normal quantization
        Meshlet = 2,         // Meshlet-based (for mesh shaders)
        Full = 3             // Maximum compression
    };

    // Common header for all cooked assets
    struct CookedAssetHeader {
        uint32_t Magic;              // COOKED_ASSET_MAGIC
        uint32_t Version;            // COOKED_ASSET_VERSION
        AssetType Type;              // Asset type
        uint32_t Flags;              // Type-specific flags
        uint64_t SourceHash;         // Hash of source file for change detection
        uint64_t CookedTimestamp;    // Unix timestamp when cooked
        uint32_t DataOffset;         // Offset to actual data
        uint32_t DataSize;           // Size of data payload
        uint32_t MetadataOffset;     // Offset to metadata (JSON)
        uint32_t MetadataSize;       // Size of metadata
        std::array<uint8_t, 32> Reserved; // Future use
    };
    static_assert(sizeof(CookedAssetHeader) == 80, "CookedAssetHeader size check");

    // Texture-specific header (follows CookedAssetHeader)
    struct CookedTextureHeader {
        TextureFormat Format;
        TextureType Type;
        uint32_t Width;
        uint32_t Height;
        uint32_t Depth;              // For 3D textures
        uint32_t ArrayLayers;        // For array textures
        uint32_t MipLevels;
        uint32_t Flags;              // sRGB, generateMips, etc.
        std::array<uint32_t, 16> MipOffsets;  // Offset for each mip level
        std::array<uint32_t, 16> MipSizes;    // Size of each mip level
    };
    static_assert(sizeof(CookedTextureHeader) == 160, "CookedTextureHeader size check");

    // Mesh-specific header (follows CookedAssetHeader)
    struct CookedMeshHeader {
        uint32_t VertexCount;
        uint32_t IndexCount;
        uint32_t PrimitiveCount;
        uint32_t BoneCount;          // For skeletal meshes
        MeshCompression Compression;
        uint32_t VertexStride;
        uint32_t IndexStride;        // 2 or 4 bytes
        uint32_t Flags;              // Has tangents, colors, etc.
        float BoundsMin[3];          // AABB min
        float BoundsMax[3];          // AABB max
        float BoundingSphereRadius;
        uint32_t Padding;            // Align to 96 bytes
        uint32_t VertexDataOffset;
        uint32_t VertexDataSize;
        uint32_t IndexDataOffset;
        uint32_t IndexDataSize;
        uint32_t PrimitiveDataOffset;
        uint32_t PrimitiveDataSize;
        uint32_t BoneDataOffset;
        uint32_t BoneDataSize;
    };
    static_assert(sizeof(CookedMeshHeader) == 96, "CookedMeshHeader size check");

    // Shader-specific header
    struct CookedShaderHeader {
        uint32_t Stage;              // Vertex, Fragment, Compute, etc.
        uint32_t SpirvSize;
        uint32_t ReflectionOffset;   // Offset to reflection data
        uint32_t ReflectionSize;
        uint32_t EntryPointLength;
        uint32_t Reserved[3];
    };
    static_assert(sizeof(CookedShaderHeader) == 32, "CookedShaderHeader size check");

    // Asset reference - used for dependencies
    struct AssetRef {
        uint64_t AssetId;            // Unique asset identifier (hash of path)
        AssetType Type;
        uint32_t Reserved;
    };

    // Helper to compute asset ID from path
    inline uint64_t ComputeAssetId(const std::string& path) {
        // FNV-1a hash
        uint64_t hash = 14695981039346656037ULL;
        for (char c : path) {
            hash ^= static_cast<uint64_t>(c);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    // Get string name for asset type
    inline const char* GetAssetTypeName(AssetType type) {
        switch (type) {
            case AssetType::Texture: return "Texture";
            case AssetType::Mesh: return "Mesh";
            case AssetType::Material: return "Material";
            case AssetType::Shader: return "Shader";
            case AssetType::Audio: return "Audio";
            case AssetType::Animation: return "Animation";
            case AssetType::Prefab: return "Prefab";
            case AssetType::VisualScriptGraph: return "VisualScriptGraph";
            case AssetType::Timeline: return "Timeline";
            case AssetType::Level: return "Level";
            case AssetType::Font: return "Font";
            case AssetType::Script: return "Script";
            case AssetType::Scene: return "Scene";
            case AssetType::WorldPartitionCell: return "WorldPartitionCell";
            case AssetType::HierarchicalLOD: return "HierarchicalLOD";
            case AssetType::AddressablesCatalog: return "AddressablesCatalog";
            case AssetType::AssetBundle: return "AssetBundle";
            case AssetType::AssetBundlePatch: return "AssetBundlePatch";
            default: return "Unknown";
        }
    }

} // namespace Asset
} // namespace Core
