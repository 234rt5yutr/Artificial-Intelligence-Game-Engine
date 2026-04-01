#include "Core/Renderer/Terrain/TerrainGenerator.h"
#include "Core/Log.h"
#include "Core/Profile.h"
#include <cmath>
#include <algorithm>
#include <random>

namespace Core {
namespace Renderer {

    TerrainGenerator::TerrainGenerator() {
        // Initialize default permutation table
        InitPermutationTable(12345);
    }

    TerrainGenerator::~TerrainGenerator() {
        WaitForAllJobs();
    }

    //=========================================================================
    // Permutation Table Initialization
    //=========================================================================

    void TerrainGenerator::InitPermutationTable(uint32_t seed) {
        m_Permutation.resize(512);
        
        // Initialize with values 0-255
        std::vector<int> base(256);
        for (int i = 0; i < 256; ++i) {
            base[i] = i;
        }
        
        // Shuffle using seed
        std::mt19937 rng(seed);
        std::shuffle(base.begin(), base.end(), rng);
        
        // Duplicate for wrapping
        for (int i = 0; i < 256; ++i) {
            m_Permutation[i] = base[i];
            m_Permutation[256 + i] = base[i];
        }
    }

    //=========================================================================
    // Synchronous Generation
    //=========================================================================

    bool TerrainGenerator::GenerateChunk(
        TerrainChunk& chunk,
        const ECS::TerrainComponent& terrainConfig,
        uint32_t lodLevel)
    {
        PROFILE_FUNCTION();

        chunk.State = ChunkState::Generating;

        try {
            // Generate heights procedurally or use existing heightmap
            if (terrainConfig.GenerateProcedurally) {
                GenerateProceduralHeights(chunk, terrainConfig);
            }

            // Generate vertices with LOD
            GenerateVertices(chunk, terrainConfig, lodLevel);

            // Generate indices for all LOD levels
            GenerateLODIndices(chunk, terrainConfig.ChunkSize, terrainConfig.LODLevels);

            // Calculate normals
            CalculateNormals(chunk, terrainConfig.ChunkSize, 
                           terrainConfig.HorizontalScale, terrainConfig.HeightScale);

            // Calculate bounding box
            CalculateBoundingBox(chunk);

            chunk.CurrentLOD = lodLevel;
            chunk.State = ChunkState::Generated;

            ENGINE_CORE_TRACE("Generated terrain chunk ({}, {}) at LOD {}",
                chunk.ChunkCoord.x, chunk.ChunkCoord.y, lodLevel);

            return true;
        }
        catch (const std::exception& e) {
            ENGINE_CORE_ERROR("Failed to generate terrain chunk: {}", e.what());
            chunk.State = ChunkState::Unloaded;
            return false;
        }
    }

    bool TerrainGenerator::GenerateFromHeightmap(
        TerrainChunk& chunk,
        const std::vector<float>& heightmap,
        uint32_t width,
        uint32_t height,
        const ECS::TerrainComponent& terrainConfig,
        uint32_t lodLevel)
    {
        PROFILE_FUNCTION();

        chunk.State = ChunkState::Generating;

        // Copy heightmap data
        chunk.HeightmapData = heightmap;
        chunk.HeightmapWidth = width;
        chunk.HeightmapHeight = height;

        // Generate vertices and indices
        GenerateVertices(chunk, terrainConfig, lodLevel);
        GenerateLODIndices(chunk, terrainConfig.ChunkSize, terrainConfig.LODLevels);
        CalculateNormals(chunk, terrainConfig.ChunkSize, 
                        terrainConfig.HorizontalScale, terrainConfig.HeightScale);
        CalculateBoundingBox(chunk);

        chunk.CurrentLOD = lodLevel;
        chunk.State = ChunkState::Generated;

        return true;
    }

    //=========================================================================
    // Asynchronous Generation
    //=========================================================================

    void TerrainGenerator::GenerateChunkAsync(
        TerrainChunk& chunk,
        const ECS::TerrainComponent& terrainConfig,
        uint32_t lodLevel,
        ChunkGenerationCallback callback)
    {
        chunk.State = ChunkState::Generating;
        chunk.AsyncGeneration = true;

        // Capture by value for thread safety
        ECS::TerrainComponent config = terrainConfig;
        
        JobSystem::Execute(m_JobContext, [this, &chunk, config, lodLevel, callback]() {
            bool success = GenerateChunk(chunk, config, lodLevel);
            
            if (callback) {
                callback(chunk, success);
            }
        });
    }

    void TerrainGenerator::WaitForAllJobs() {
        JobSystem::Wait(m_JobContext);
    }

    bool TerrainGenerator::HasPendingJobs() const {
        return JobSystem::IsBusy(m_JobContext);
    }

    //=========================================================================
    // Heightmap Loading
    //=========================================================================

    bool TerrainGenerator::LoadHeightmap(
        const std::string& filepath,
        std::vector<float>& outData,
        uint32_t& outWidth,
        uint32_t& outHeight)
    {
        PROFILE_FUNCTION();

        // TODO: Implement actual image loading using stb_image or similar
        // For now, generate placeholder data
        ENGINE_CORE_WARN("Heightmap loading not fully implemented, using generated data");
        
        outWidth = 256;
        outHeight = 256;
        outData.resize(outWidth * outHeight);

        // Generate simple noise as placeholder
        NoiseParams params;
        params.Frequency = 0.02f;
        params.Octaves = 4;
        
        for (uint32_t z = 0; z < outHeight; ++z) {
            for (uint32_t x = 0; x < outWidth; ++x) {
                float fx = static_cast<float>(x);
                float fz = static_cast<float>(z);
                outData[z * outWidth + x] = GenerateFractalNoise(fx, fz, params);
            }
        }

        return true;
    }

    void TerrainGenerator::ExtractChunkHeightmap(
        const std::vector<float>& fullHeightmap,
        uint32_t fullWidth,
        uint32_t fullHeight,
        const Math::IVec2& chunkCoord,
        uint32_t chunkSize,
        std::vector<float>& outChunkHeightmap)
    {
        outChunkHeightmap.resize(chunkSize * chunkSize);

        uint32_t startX = chunkCoord.x * (chunkSize - 1);
        uint32_t startZ = chunkCoord.y * (chunkSize - 1);

        for (uint32_t z = 0; z < chunkSize; ++z) {
            for (uint32_t x = 0; x < chunkSize; ++x) {
                uint32_t srcX = glm::min(startX + x, fullWidth - 1);
                uint32_t srcZ = glm::min(startZ + z, fullHeight - 1);
                outChunkHeightmap[z * chunkSize + x] = fullHeightmap[srcZ * fullWidth + srcX];
            }
        }
    }

    //=========================================================================
    // Noise Generation
    //=========================================================================

    float TerrainGenerator::Fade(float t) {
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }

    float TerrainGenerator::Lerp(float a, float b, float t) {
        return a + t * (b - a);
    }

    float TerrainGenerator::Grad(int hash, float x, float y) {
        int h = hash & 7;
        float u = h < 4 ? x : y;
        float v = h < 4 ? y : x;
        return ((h & 1) ? -u : u) + ((h & 2) ? -2.0f * v : 2.0f * v);
    }

    float TerrainGenerator::Grad3D(int hash, float x, float y, float z) {
        int h = hash & 15;
        float u = h < 8 ? x : y;
        float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    }

    int TerrainGenerator::FastFloor(float x) {
        return x > 0 ? static_cast<int>(x) : static_cast<int>(x) - 1;
    }

    float TerrainGenerator::GeneratePerlinNoise(float x, float z, const NoiseParams& params) {
        // Apply frequency and offset
        x = x * params.Frequency + params.Offset.x;
        z = z * params.Frequency + params.Offset.y;

        // Grid cell coordinates
        int X = FastFloor(x) & 255;
        int Z = FastFloor(z) & 255;

        // Relative position within cell
        x -= FastFloor(x);
        z -= FastFloor(z);

        // Fade curves
        float u = Fade(x);
        float v = Fade(z);

        // Hash coordinates of 4 corners
        int A = m_Permutation[X] + Z;
        int AA = m_Permutation[A];
        int AB = m_Permutation[A + 1];
        int B = m_Permutation[X + 1] + Z;
        int BA = m_Permutation[B];
        int BB = m_Permutation[B + 1];

        // Blend results from 4 corners
        float res = Lerp(
            Lerp(Grad(m_Permutation[AA], x, z), Grad(m_Permutation[BA], x - 1, z), u),
            Lerp(Grad(m_Permutation[AB], x, z - 1), Grad(m_Permutation[BB], x - 1, z - 1), u),
            v
        );

        // Normalize to [0, 1]
        return (res + 1.0f) * 0.5f;
    }

    float TerrainGenerator::GenerateSimplexNoise(float x, float z, const NoiseParams& params) {
        // Simplex noise implementation (2D)
        const float F2 = 0.5f * (std::sqrt(3.0f) - 1.0f);
        const float G2 = (3.0f - std::sqrt(3.0f)) / 6.0f;

        x = x * params.Frequency + params.Offset.x;
        z = z * params.Frequency + params.Offset.y;

        // Skew input space to grid
        float s = (x + z) * F2;
        int i = FastFloor(x + s);
        int j = FastFloor(z + s);

        float t = (i + j) * G2;
        float X0 = i - t;
        float Y0 = j - t;
        float x0 = x - X0;
        float y0 = z - Y0;

        // Determine which simplex we're in
        int i1, j1;
        if (x0 > y0) { i1 = 1; j1 = 0; }
        else { i1 = 0; j1 = 1; }

        float x1 = x0 - i1 + G2;
        float y1 = y0 - j1 + G2;
        float x2 = x0 - 1.0f + 2.0f * G2;
        float y2 = y0 - 1.0f + 2.0f * G2;

        int ii = i & 255;
        int jj = j & 255;

        // Calculate contributions from three corners
        float n0, n1, n2;

        float t0 = 0.5f - x0 * x0 - y0 * y0;
        if (t0 < 0) n0 = 0.0f;
        else {
            t0 *= t0;
            n0 = t0 * t0 * Grad(m_Permutation[ii + m_Permutation[jj]], x0, y0);
        }

        float t1 = 0.5f - x1 * x1 - y1 * y1;
        if (t1 < 0) n1 = 0.0f;
        else {
            t1 *= t1;
            n1 = t1 * t1 * Grad(m_Permutation[ii + i1 + m_Permutation[jj + j1]], x1, y1);
        }

        float t2 = 0.5f - x2 * x2 - y2 * y2;
        if (t2 < 0) n2 = 0.0f;
        else {
            t2 *= t2;
            n2 = t2 * t2 * Grad(m_Permutation[ii + 1 + m_Permutation[jj + 1]], x2, y2);
        }

        // Sum and scale to [0, 1]
        return (70.0f * (n0 + n1 + n2) + 1.0f) * 0.5f;
    }

    float TerrainGenerator::GenerateFractalNoise(float x, float z, const NoiseParams& params) {
        float total = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        float maxValue = 0.0f;

        NoiseParams octaveParams = params;

        for (uint32_t i = 0; i < params.Octaves; ++i) {
            octaveParams.Frequency = params.Frequency * frequency;
            total += GeneratePerlinNoise(x, z, octaveParams) * amplitude;
            
            maxValue += amplitude;
            amplitude *= params.Persistence;
            frequency *= params.Lacunarity;
        }

        return (total / maxValue) * params.Amplitude;
    }

    float TerrainGenerator::GenerateBiomeNoise(
        float x,
        float z,
        ECS::BiomeType biome,
        const NoiseParams& params)
    {
        float baseHeight = GenerateFractalNoise(x, z, params);

        switch (biome) {
            case ECS::BiomeType::Desert: {
                // Dunes - smooth rolling hills
                NoiseParams duneParams = params;
                duneParams.Frequency *= 2.0f;
                duneParams.Octaves = 2;
                float dunes = GenerateFractalNoise(x * 0.5f, z, duneParams);
                return baseHeight * 0.6f + dunes * 0.4f;
            }

            case ECS::BiomeType::Forest: {
                // Gentle hills with some variation
                return baseHeight;
            }

            case ECS::BiomeType::Mountain: {
                // Sharp peaks using power function
                float ridge = std::abs(baseHeight - 0.5f) * 2.0f;
                ridge = 1.0f - ridge;
                ridge = std::pow(ridge, 2.0f);
                
                NoiseParams detailParams = params;
                detailParams.Frequency *= 4.0f;
                detailParams.Octaves = 3;
                float detail = GenerateFractalNoise(x, z, detailParams) * 0.2f;
                
                return ridge * 0.8f + baseHeight * 0.2f + detail;
            }

            case ECS::BiomeType::Plains: {
                // Very flat with subtle variation
                return baseHeight * 0.3f + 0.1f;
            }

            case ECS::BiomeType::Tundra: {
                // Frozen terrain with subtle features
                NoiseParams frostParams = params;
                frostParams.Frequency *= 0.5f;
                float frost = GenerateFractalNoise(x + 1000.0f, z + 1000.0f, frostParams);
                return baseHeight * 0.5f + frost * 0.3f;
            }

            default:
                return baseHeight;
        }
    }

    //=========================================================================
    // Internal Generation Helpers
    //=========================================================================

    void TerrainGenerator::GenerateProceduralHeights(
        TerrainChunk& chunk,
        const ECS::TerrainComponent& terrainConfig)
    {
        uint32_t size = terrainConfig.ChunkSize;
        chunk.HeightmapData.resize(size * size);
        chunk.HeightmapWidth = size;
        chunk.HeightmapHeight = size;

        // Reinitialize permutation table with terrain seed
        InitPermutationTable(terrainConfig.Seed);

        NoiseParams params(terrainConfig);

        // World offset for this chunk
        float worldOffsetX = chunk.ChunkCoord.x * (size - 1) * terrainConfig.HorizontalScale;
        float worldOffsetZ = chunk.ChunkCoord.y * (size - 1) * terrainConfig.HorizontalScale;

        chunk.MinHeight = std::numeric_limits<float>::max();
        chunk.MaxHeight = std::numeric_limits<float>::lowest();

        for (uint32_t z = 0; z < size; ++z) {
            for (uint32_t x = 0; x < size; ++x) {
                float worldX = worldOffsetX + x * terrainConfig.HorizontalScale;
                float worldZ = worldOffsetZ + z * terrainConfig.HorizontalScale;

                float height = GenerateBiomeNoise(worldX, worldZ, terrainConfig.Biome, params);
                
                chunk.HeightmapData[z * size + x] = height;
                chunk.MinHeight = std::min(chunk.MinHeight, height);
                chunk.MaxHeight = std::max(chunk.MaxHeight, height);
            }
        }
    }

    void TerrainGenerator::GenerateVertices(
        TerrainChunk& chunk,
        const ECS::TerrainComponent& terrainConfig,
        uint32_t lodLevel)
    {
        uint32_t size = terrainConfig.ChunkSize;
        uint32_t step = GetLODStepSize(lodLevel);
        uint32_t vertexCount = ((size - 1) / step + 1);

        chunk.Vertices.clear();
        chunk.Vertices.reserve(vertexCount * vertexCount);

        float worldOffsetX = chunk.WorldPosition.x;
        float worldOffsetZ = chunk.WorldPosition.z;

        for (uint32_t z = 0; z < size; z += step) {
            for (uint32_t x = 0; x < size; x += step) {
                TerrainVertex vertex;

                // Position
                float height = 0.0f;
                if (!chunk.HeightmapData.empty()) {
                    height = chunk.HeightmapData[z * size + x] * terrainConfig.HeightScale;
                }

                vertex.Position = Math::Vec3(
                    worldOffsetX + x * terrainConfig.HorizontalScale,
                    height,
                    worldOffsetZ + z * terrainConfig.HorizontalScale
                );

                // UV coordinates
                vertex.TexCoord = Math::Vec2(
                    static_cast<float>(x) / static_cast<float>(size - 1),
                    static_cast<float>(z) / static_cast<float>(size - 1)
                );

                // Normal will be calculated later
                vertex.Normal = Math::Vec3(0.0f, 1.0f, 0.0f);
                vertex.MorphFactor = 0.0f;

                chunk.Vertices.push_back(vertex);
            }
        }
    }

    void TerrainGenerator::GenerateIndices(
        TerrainChunk& chunk,
        uint32_t chunkSize,
        uint32_t lodLevel)
    {
        uint32_t step = GetLODStepSize(lodLevel);
        uint32_t verticesPerRow = (chunkSize - 1) / step + 1;
        uint32_t quadCount = verticesPerRow - 1;

        chunk.Indices.clear();
        chunk.Indices.reserve(quadCount * quadCount * 6);

        for (uint32_t z = 0; z < quadCount; ++z) {
            for (uint32_t x = 0; x < quadCount; ++x) {
                uint32_t topLeft = z * verticesPerRow + x;
                uint32_t topRight = topLeft + 1;
                uint32_t bottomLeft = (z + 1) * verticesPerRow + x;
                uint32_t bottomRight = bottomLeft + 1;

                // First triangle
                chunk.Indices.push_back(topLeft);
                chunk.Indices.push_back(bottomLeft);
                chunk.Indices.push_back(topRight);

                // Second triangle
                chunk.Indices.push_back(topRight);
                chunk.Indices.push_back(bottomLeft);
                chunk.Indices.push_back(bottomRight);
            }
        }
    }

    //=========================================================================
    // LOD Generation
    //=========================================================================

    void TerrainGenerator::GenerateLODIndices(
        TerrainChunk& chunk,
        uint32_t chunkSize,
        uint32_t maxLODLevels)
    {
        chunk.LODIndexCounts.clear();
        chunk.LODIndexOffsets.clear();
        chunk.Indices.clear();

        uint32_t currentOffset = 0;

        for (uint32_t lod = 0; lod < maxLODLevels; ++lod) {
            uint32_t step = GetLODStepSize(lod);
            
            // Check if this LOD is valid for chunk size
            if (step >= chunkSize - 1) {
                break;
            }

            uint32_t verticesPerRow = (chunkSize - 1) / step + 1;
            uint32_t quadCount = verticesPerRow - 1;
            uint32_t indexCount = quadCount * quadCount * 6;

            chunk.LODIndexOffsets.push_back(currentOffset);
            chunk.LODIndexCounts.push_back(indexCount);

            // Generate indices for this LOD
            for (uint32_t z = 0; z < quadCount; ++z) {
                for (uint32_t x = 0; x < quadCount; ++x) {
                    uint32_t topLeft = z * verticesPerRow + x;
                    uint32_t topRight = topLeft + 1;
                    uint32_t bottomLeft = (z + 1) * verticesPerRow + x;
                    uint32_t bottomRight = bottomLeft + 1;

                    chunk.Indices.push_back(topLeft);
                    chunk.Indices.push_back(bottomLeft);
                    chunk.Indices.push_back(topRight);

                    chunk.Indices.push_back(topRight);
                    chunk.Indices.push_back(bottomLeft);
                    chunk.Indices.push_back(bottomRight);
                }
            }

            currentOffset += indexCount;
        }
    }

    //=========================================================================
    // Normal Calculation
    //=========================================================================

    void TerrainGenerator::CalculateNormals(
        TerrainChunk& chunk,
        uint32_t chunkSize,
        float horizontalScale,
        float heightScale)
    {
        if (chunk.HeightmapData.empty() || chunk.Vertices.empty()) {
            return;
        }

        uint32_t lodStep = 1;
        if (!chunk.LODIndexCounts.empty()) {
            lodStep = GetLODStepSize(chunk.CurrentLOD);
        }

        uint32_t verticesPerRow = (chunkSize - 1) / lodStep + 1;

        for (size_t i = 0; i < chunk.Vertices.size(); ++i) {
            uint32_t vx = i % verticesPerRow;
            uint32_t vz = i / verticesPerRow;

            // Map back to heightmap coordinates
            uint32_t hx = vx * lodStep;
            uint32_t hz = vz * lodStep;

            // Sample neighboring heights
            auto getHeight = [&](int x, int z) -> float {
                x = glm::clamp(x, 0, static_cast<int>(chunkSize - 1));
                z = glm::clamp(z, 0, static_cast<int>(chunkSize - 1));
                return chunk.HeightmapData[z * chunkSize + x] * heightScale;
            };

            float hL = getHeight(hx - 1, hz);
            float hR = getHeight(hx + 1, hz);
            float hD = getHeight(hx, hz - 1);
            float hU = getHeight(hx, hz + 1);

            chunk.Vertices[i].Normal = CalculateNormal(hL, hR, hD, hU, horizontalScale * 2.0f);
        }
    }

    Math::Vec3 TerrainGenerator::CalculateNormal(
        float hL, float hR,
        float hD, float hU,
        float scale)
    {
        Math::Vec3 normal(
            hL - hR,
            scale,
            hD - hU
        );
        return glm::normalize(normal);
    }

    //=========================================================================
    // Bounding Box Calculation
    //=========================================================================

    void TerrainGenerator::CalculateBoundingBox(TerrainChunk& chunk) {
        if (chunk.Vertices.empty()) {
            chunk.BoundingBox = TerrainAABB();
            return;
        }

        Math::Vec3 minPos = chunk.Vertices[0].Position;
        Math::Vec3 maxPos = chunk.Vertices[0].Position;

        for (const auto& vertex : chunk.Vertices) {
            minPos = glm::min(minPos, vertex.Position);
            maxPos = glm::max(maxPos, vertex.Position);
        }

        chunk.BoundingBox = TerrainAABB(minPos, maxPos);
    }

    //=========================================================================
    // Geomorphing Support
    //=========================================================================

    float TerrainGenerator::CalculateMorphFactor(
        float distanceToCamera,
        uint32_t currentLOD,
        const float* lodDistances)
    {
        if (currentLOD == 0) {
            return 0.0f;
        }

        float lodStart = lodDistances[currentLOD - 1];
        float lodEnd = lodDistances[currentLOD];
        float range = lodEnd - lodStart;

        if (range <= 0.0f) {
            return 0.0f;
        }

        float t = (distanceToCamera - lodStart) / range;
        return glm::clamp(t, 0.0f, 1.0f);
    }

    void TerrainGenerator::ApplyMorphFactors(
        TerrainChunk& chunk,
        float distanceToCamera,
        uint32_t targetLOD,
        const float* lodDistances)
    {
        float morphFactor = CalculateMorphFactor(distanceToCamera, targetLOD, lodDistances);
        
        for (auto& vertex : chunk.Vertices) {
            vertex.MorphFactor = morphFactor;
        }

        chunk.LODTransition = morphFactor;
    }

} // namespace Renderer
} // namespace Core
