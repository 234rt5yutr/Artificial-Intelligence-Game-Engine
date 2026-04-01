#pragma once

#include "Core/Math/Math.h"
#include <string>
#include <vector>
#include <cstdint>

namespace Core {
namespace ECS {

    // Biome types for terrain generation
    enum class BiomeType : uint8_t {
        Desert = 0,
        Forest = 1,
        Mountain = 2,
        Plains = 3,
        Tundra = 4
    };

    // Material layer for terrain texturing based on height/slope
    struct TerrainMaterialLayer {
        std::string DiffuseTexturePath;
        std::string NormalTexturePath;
        float MinHeight = 0.0f;         // Minimum height for this layer
        float MaxHeight = 100.0f;       // Maximum height for this layer
        float MinSlope = 0.0f;          // Minimum slope angle (degrees)
        float MaxSlope = 90.0f;         // Maximum slope angle (degrees)
        float BlendSharpness = 1.0f;    // How sharp the blend transition is
        float TilingScale = 1.0f;       // UV tiling scale
        uint32_t MaterialIndex = 0;     // Index into material array

        TerrainMaterialLayer() = default;
        
        TerrainMaterialLayer(const std::string& diffuse, float minH, float maxH)
            : DiffuseTexturePath(diffuse)
            , MinHeight(minH)
            , MaxHeight(maxH) {}
    };

    struct TerrainComponent {
        // Chunk configuration
        uint32_t ChunkSize = 64;            // Vertices per chunk edge (power of 2 recommended)
        float HeightScale = 100.0f;         // Maximum terrain height
        float HorizontalScale = 1.0f;       // Scale factor for horizontal dimensions

        // LOD configuration
        uint32_t LODLevels = 4;             // Number of LOD levels (0 = highest detail)
        float ViewDistance = 1000.0f;       // Maximum view distance for terrain
        float LODDistances[4] = {           // Distance thresholds for each LOD level
            100.0f,   // LOD 0 -> 1
            250.0f,   // LOD 1 -> 2
            500.0f,   // LOD 2 -> 3
            1000.0f   // LOD 3 -> cull
        };

        // Heightmap source
        std::string HeightmapPath;          // Path to heightmap image (16-bit grayscale recommended)
        bool GenerateProcedurally = false;  // Use noise-based generation instead of heightmap

        // Procedural generation settings
        BiomeType Biome = BiomeType::Plains;
        float NoiseFrequency = 0.01f;       // Base noise frequency
        uint32_t NoiseOctaves = 6;          // Number of noise octaves
        float NoisePersistence = 0.5f;      // Amplitude multiplier per octave
        float NoiseLacunarity = 2.0f;       // Frequency multiplier per octave
        uint32_t Seed = 12345;              // Random seed for generation

        // Material layers (sorted by priority, lower index = higher priority)
        std::vector<TerrainMaterialLayer> MaterialLayers;

        // Rendering flags
        bool CastShadows = true;
        bool ReceiveShadows = true;
        bool Visible = true;
        bool EnableTessellation = false;    // GPU tessellation for additional detail
        bool EnableGeomorphing = true;      // Smooth LOD transitions

        // Runtime state (managed by TerrainSystem)
        bool IsInitialized = false;

        TerrainComponent() = default;

        // Factory method for heightmap-based terrain
        static TerrainComponent CreateFromHeightmap(
            const std::string& heightmapPath,
            float heightScale = 100.0f,
            uint32_t chunkSize = 64)
        {
            TerrainComponent terrain;
            terrain.HeightmapPath = heightmapPath;
            terrain.HeightScale = heightScale;
            terrain.ChunkSize = chunkSize;
            terrain.GenerateProcedurally = false;
            return terrain;
        }

        // Factory method for procedural terrain
        static TerrainComponent CreateProcedural(
            BiomeType biome,
            float heightScale = 100.0f,
            uint32_t chunkSize = 64,
            uint32_t seed = 12345)
        {
            TerrainComponent terrain;
            terrain.Biome = biome;
            terrain.HeightScale = heightScale;
            terrain.ChunkSize = chunkSize;
            terrain.GenerateProcedurally = true;
            terrain.Seed = seed;
            
            // Biome-specific defaults
            switch (biome) {
                case BiomeType::Desert:
                    terrain.NoiseFrequency = 0.005f;
                    terrain.NoiseOctaves = 4;
                    terrain.NoisePersistence = 0.4f;
                    break;
                case BiomeType::Forest:
                    terrain.NoiseFrequency = 0.01f;
                    terrain.NoiseOctaves = 6;
                    terrain.NoisePersistence = 0.5f;
                    break;
                case BiomeType::Mountain:
                    terrain.NoiseFrequency = 0.008f;
                    terrain.NoiseOctaves = 8;
                    terrain.NoisePersistence = 0.6f;
                    break;
                case BiomeType::Plains:
                    terrain.NoiseFrequency = 0.003f;
                    terrain.NoiseOctaves = 3;
                    terrain.NoisePersistence = 0.3f;
                    break;
                case BiomeType::Tundra:
                    terrain.NoiseFrequency = 0.007f;
                    terrain.NoiseOctaves = 5;
                    terrain.NoisePersistence = 0.45f;
                    break;
            }
            
            return terrain;
        }

        // Add a material layer
        void AddMaterialLayer(const TerrainMaterialLayer& layer) {
            MaterialLayers.push_back(layer);
        }

        // Get LOD level for a given distance
        uint32_t GetLODForDistance(float distance) const {
            for (uint32_t i = 0; i < LODLevels; ++i) {
                if (distance < LODDistances[i]) {
                    return i;
                }
            }
            return LODLevels - 1;
        }
    };

    // Helper to convert BiomeType to string
    inline const char* BiomeTypeToString(BiomeType biome) {
        switch (biome) {
            case BiomeType::Desert: return "Desert";
            case BiomeType::Forest: return "Forest";
            case BiomeType::Mountain: return "Mountain";
            case BiomeType::Plains: return "Plains";
            case BiomeType::Tundra: return "Tundra";
            default: return "Unknown";
        }
    }

} // namespace ECS
} // namespace Core
