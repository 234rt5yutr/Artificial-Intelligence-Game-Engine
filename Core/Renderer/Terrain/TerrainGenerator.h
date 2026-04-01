#pragma once

#include "Core/Renderer/Terrain/TerrainChunk.h"
#include "Core/ECS/Components/TerrainComponent.h"
#include "Core/JobSystem/JobSystem.h"
#include "Core/Math/Math.h"
#include <memory>
#include <functional>
#include <vector>
#include <string>
#include <cstdint>

namespace Core {
namespace Renderer {

    // Callback type for async generation completion
    using ChunkGenerationCallback = std::function<void(TerrainChunk& chunk, bool success)>;

    // Noise generation parameters
    struct NoiseParams {
        float Frequency = 0.01f;
        uint32_t Octaves = 6;
        float Persistence = 0.5f;
        float Lacunarity = 2.0f;
        uint32_t Seed = 12345;
        float Amplitude = 1.0f;
        Math::Vec2 Offset{0.0f, 0.0f};

        NoiseParams() = default;

        NoiseParams(const ECS::TerrainComponent& terrain)
            : Frequency(terrain.NoiseFrequency)
            , Octaves(terrain.NoiseOctaves)
            , Persistence(terrain.NoisePersistence)
            , Lacunarity(terrain.NoiseLacunarity)
            , Seed(terrain.Seed)
            , Amplitude(terrain.HeightScale) {}
    };

    class TerrainGenerator {
    public:
        TerrainGenerator();
        ~TerrainGenerator();

        // Non-copyable
        TerrainGenerator(const TerrainGenerator&) = delete;
        TerrainGenerator& operator=(const TerrainGenerator&) = delete;

        //---------------------------------------------------------------------
        // Synchronous Generation
        //---------------------------------------------------------------------

        // Generate chunk synchronously
        bool GenerateChunk(
            TerrainChunk& chunk,
            const ECS::TerrainComponent& terrainConfig,
            uint32_t lodLevel);

        // Generate from heightmap data
        bool GenerateFromHeightmap(
            TerrainChunk& chunk,
            const std::vector<float>& heightmap,
            uint32_t width,
            uint32_t height,
            const ECS::TerrainComponent& terrainConfig,
            uint32_t lodLevel);

        //---------------------------------------------------------------------
        // Asynchronous Generation
        //---------------------------------------------------------------------

        // Generate chunk asynchronously using JobSystem
        void GenerateChunkAsync(
            TerrainChunk& chunk,
            const ECS::TerrainComponent& terrainConfig,
            uint32_t lodLevel,
            ChunkGenerationCallback callback = nullptr);

        // Wait for all pending async generation jobs
        void WaitForAllJobs();

        // Check if any jobs are still running
        bool HasPendingJobs() const;

        //---------------------------------------------------------------------
        // Heightmap Loading
        //---------------------------------------------------------------------

        // Load heightmap from file (16-bit grayscale PNG/TGA recommended)
        bool LoadHeightmap(
            const std::string& filepath,
            std::vector<float>& outData,
            uint32_t& outWidth,
            uint32_t& outHeight);

        // Extract chunk heightmap from larger heightmap
        void ExtractChunkHeightmap(
            const std::vector<float>& fullHeightmap,
            uint32_t fullWidth,
            uint32_t fullHeight,
            const Math::IVec2& chunkCoord,
            uint32_t chunkSize,
            std::vector<float>& outChunkHeightmap);

        //---------------------------------------------------------------------
        // Noise Generation
        //---------------------------------------------------------------------

        // Generate height value using Perlin noise
        float GeneratePerlinNoise(float x, float z, const NoiseParams& params);

        // Generate height value using Simplex noise (faster alternative)
        float GenerateSimplexNoise(float x, float z, const NoiseParams& params);

        // Generate fractal noise (multiple octaves)
        float GenerateFractalNoise(float x, float z, const NoiseParams& params);

        // Generate biome-specific noise
        float GenerateBiomeNoise(
            float x,
            float z,
            ECS::BiomeType biome,
            const NoiseParams& params);

        //---------------------------------------------------------------------
        // LOD Generation
        //---------------------------------------------------------------------

        // Generate LOD indices for multiple detail levels
        void GenerateLODIndices(
            TerrainChunk& chunk,
            uint32_t chunkSize,
            uint32_t maxLODLevels);

        // Calculate step size for LOD level
        static uint32_t GetLODStepSize(uint32_t lodLevel) {
            return 1u << lodLevel;
        }

        //---------------------------------------------------------------------
        // Normal Calculation
        //---------------------------------------------------------------------

        // Calculate normals from height data
        void CalculateNormals(
            TerrainChunk& chunk,
            uint32_t chunkSize,
            float horizontalScale,
            float heightScale);

        // Calculate single normal from height differences
        Math::Vec3 CalculateNormal(
            float hL, float hR,
            float hD, float hU,
            float scale);

        //---------------------------------------------------------------------
        // Geomorphing Support
        //---------------------------------------------------------------------

        // Calculate morph factor for smooth LOD transitions
        float CalculateMorphFactor(
            float distanceToCamera,
            uint32_t currentLOD,
            const float* lodDistances);

        // Apply morph factors to vertices
        void ApplyMorphFactors(
            TerrainChunk& chunk,
            float distanceToCamera,
            uint32_t targetLOD,
            const float* lodDistances);

    private:
        // Internal generation helpers
        void GenerateVertices(
            TerrainChunk& chunk,
            const ECS::TerrainComponent& terrainConfig,
            uint32_t lodLevel);

        void GenerateProceduralHeights(
            TerrainChunk& chunk,
            const ECS::TerrainComponent& terrainConfig);

        void GenerateIndices(
            TerrainChunk& chunk,
            uint32_t chunkSize,
            uint32_t lodLevel);

        void CalculateBoundingBox(TerrainChunk& chunk);

        // Noise implementation helpers
        float Fade(float t);
        float Lerp(float a, float b, float t);
        float Grad(int hash, float x, float y);
        float Grad3D(int hash, float x, float y, float z);
        int FastFloor(float x);

        // Permutation table for noise
        void InitPermutationTable(uint32_t seed);
        std::vector<int> m_Permutation;

        // Job context for async operations
        JobSystem::Context m_JobContext;
    };

} // namespace Renderer
} // namespace Core
