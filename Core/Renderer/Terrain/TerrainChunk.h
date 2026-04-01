#pragma once

#include "Core/Math/Math.h"
#include "Core/RHI/RHIBuffer.h"
#include <vector>
#include <memory>
#include <atomic>
#include <cstdint>

namespace Core {
namespace Renderer {

    // Terrain vertex structure
    struct TerrainVertex {
        Math::Vec3 Position;
        Math::Vec3 Normal;
        Math::Vec2 TexCoord;
        float MorphFactor;  // For geomorphing between LOD levels

        TerrainVertex()
            : Position(0.0f)
            , Normal(0.0f, 1.0f, 0.0f)
            , TexCoord(0.0f)
            , MorphFactor(0.0f) {}

        TerrainVertex(const Math::Vec3& pos, const Math::Vec3& norm, const Math::Vec2& uv)
            : Position(pos)
            , Normal(norm)
            , TexCoord(uv)
            , MorphFactor(0.0f) {}
    };

    // Axis-aligned bounding box for terrain chunk
    struct TerrainAABB {
        Math::Vec3 Min;
        Math::Vec3 Max;

        TerrainAABB() : Min(0.0f), Max(0.0f) {}
        
        TerrainAABB(const Math::Vec3& min, const Math::Vec3& max)
            : Min(min), Max(max) {}

        Math::Vec3 GetCenter() const {
            return (Min + Max) * 0.5f;
        }

        Math::Vec3 GetExtents() const {
            return (Max - Min) * 0.5f;
        }

        float GetRadius() const {
            return glm::length(GetExtents());
        }

        bool Contains(const Math::Vec3& point) const {
            return point.x >= Min.x && point.x <= Max.x &&
                   point.y >= Min.y && point.y <= Max.y &&
                   point.z >= Min.z && point.z <= Max.z;
        }

        void Expand(const Math::Vec3& point) {
            Min = glm::min(Min, point);
            Max = glm::max(Max, point);
        }
    };

    // Chunk state enumeration
    enum class ChunkState : uint8_t {
        Unloaded = 0,       // Chunk data not loaded
        Generating = 1,     // Currently being generated (async)
        Generated = 2,      // CPU data ready, GPU buffers not uploaded
        Uploading = 3,      // Uploading to GPU
        Ready = 4,          // Ready for rendering
        PendingUnload = 5   // Marked for unload
    };

    // Terrain chunk data structure
    struct TerrainChunk {
        // Chunk identification
        Math::IVec2 ChunkCoord;             // Chunk grid coordinates (x, z)
        Math::Vec3 WorldPosition;           // World position of chunk origin

        // LOD state
        uint32_t CurrentLOD = 0;            // Current LOD level (0 = highest detail)
        uint32_t TargetLOD = 0;             // Target LOD level (for transitions)
        float LODTransition = 0.0f;         // Transition blend factor [0, 1]

        // Geometry data (CPU-side)
        std::vector<TerrainVertex> Vertices;
        std::vector<uint32_t> Indices;
        
        // Per-LOD index counts for rendering different detail levels
        std::vector<uint32_t> LODIndexCounts;
        std::vector<uint32_t> LODIndexOffsets;

        // Height data (kept for collision/physics)
        std::vector<float> HeightmapData;
        uint32_t HeightmapWidth = 0;
        uint32_t HeightmapHeight = 0;

        // Bounding volume
        TerrainAABB BoundingBox;
        float MinHeight = 0.0f;
        float MaxHeight = 0.0f;

        // GPU resources
        std::shared_ptr<RHI::RHIBuffer> VertexBuffer;
        std::shared_ptr<RHI::RHIBuffer> IndexBuffer;

        // State flags
        std::atomic<ChunkState> State{ChunkState::Unloaded};
        std::atomic<bool> IsVisible{false};
        bool NeedsUpdate = false;
        bool AsyncGeneration = true;        // Use async generation

        // Generation metadata
        uint32_t Seed = 0;
        float DistanceToCamera = 0.0f;

        TerrainChunk() = default;

        TerrainChunk(const Math::IVec2& coord, const Math::Vec3& worldPos)
            : ChunkCoord(coord)
            , WorldPosition(worldPos) {}

        // Check if chunk is ready for rendering
        bool IsReady() const {
            return State.load() == ChunkState::Ready;
        }

        // Check if chunk is being generated
        bool IsGenerating() const {
            return State.load() == ChunkState::Generating;
        }

        // Check if chunk needs geometry uploaded to GPU
        bool NeedsUpload() const {
            return State.load() == ChunkState::Generated;
        }

        // Get vertex count
        size_t GetVertexCount() const {
            return Vertices.size();
        }

        // Get index count for current LOD
        uint32_t GetIndexCount() const {
            if (CurrentLOD < LODIndexCounts.size()) {
                return LODIndexCounts[CurrentLOD];
            }
            return static_cast<uint32_t>(Indices.size());
        }

        // Get index offset for current LOD
        uint32_t GetIndexOffset() const {
            if (CurrentLOD < LODIndexOffsets.size()) {
                return LODIndexOffsets[CurrentLOD];
            }
            return 0;
        }

        // Sample height at local (x, z) position within chunk
        float SampleHeight(float localX, float localZ, uint32_t chunkSize) const {
            if (HeightmapData.empty() || HeightmapWidth == 0 || HeightmapHeight == 0) {
                return 0.0f;
            }

            // Normalize to [0, 1]
            float u = localX / static_cast<float>(chunkSize - 1);
            float v = localZ / static_cast<float>(chunkSize - 1);

            // Clamp
            u = glm::clamp(u, 0.0f, 1.0f);
            v = glm::clamp(v, 0.0f, 1.0f);

            // Bilinear interpolation
            float fx = u * (HeightmapWidth - 1);
            float fz = v * (HeightmapHeight - 1);

            int x0 = static_cast<int>(fx);
            int z0 = static_cast<int>(fz);
            int x1 = glm::min(x0 + 1, static_cast<int>(HeightmapWidth - 1));
            int z1 = glm::min(z0 + 1, static_cast<int>(HeightmapHeight - 1));

            float fracX = fx - x0;
            float fracZ = fz - z0;

            float h00 = HeightmapData[z0 * HeightmapWidth + x0];
            float h10 = HeightmapData[z0 * HeightmapWidth + x1];
            float h01 = HeightmapData[z1 * HeightmapWidth + x0];
            float h11 = HeightmapData[z1 * HeightmapWidth + x1];

            float h0 = glm::mix(h00, h10, fracX);
            float h1 = glm::mix(h01, h11, fracX);

            return glm::mix(h0, h1, fracZ);
        }

        // Clear CPU-side geometry data to save memory (keep heightmap for physics)
        void ClearCPUGeometry() {
            Vertices.clear();
            Vertices.shrink_to_fit();
            Indices.clear();
            Indices.shrink_to_fit();
        }

        // Full reset
        void Reset() {
            Vertices.clear();
            Indices.clear();
            HeightmapData.clear();
            LODIndexCounts.clear();
            LODIndexOffsets.clear();
            VertexBuffer.reset();
            IndexBuffer.reset();
            State = ChunkState::Unloaded;
            IsVisible = false;
            CurrentLOD = 0;
            TargetLOD = 0;
            LODTransition = 0.0f;
        }
    };

    // Chunk key for hash maps
    struct ChunkKey {
        int32_t X;
        int32_t Z;

        ChunkKey() : X(0), Z(0) {}
        ChunkKey(int32_t x, int32_t z) : X(x), Z(z) {}
        ChunkKey(const Math::IVec2& coord) : X(coord.x), Z(coord.y) {}

        bool operator==(const ChunkKey& other) const {
            return X == other.X && Z == other.Z;
        }

        bool operator!=(const ChunkKey& other) const {
            return !(*this == other);
        }
    };

} // namespace Renderer
} // namespace Core

// Hash function for ChunkKey
namespace std {
    template<>
    struct hash<Core::Renderer::ChunkKey> {
        size_t operator()(const Core::Renderer::ChunkKey& key) const {
            // Combine x and z using FNV-1a style hashing
            size_t h = 14695981039346656037ull;
            h ^= static_cast<size_t>(key.X);
            h *= 1099511628211ull;
            h ^= static_cast<size_t>(key.Z);
            h *= 1099511628211ull;
            return h;
        }
    };
}
