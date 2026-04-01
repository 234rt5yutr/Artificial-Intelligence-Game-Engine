#pragma once

#include "Core/Math/Math.h"
#include <cstdint>

namespace Core {
namespace Renderer {

    /**
     * @brief GPU-aligned foliage instance data for SSBO storage
     * 
     * This structure is written by the compute shader and read by the
     * vertex shader for instanced rendering. Total size: 112 bytes.
     */
    struct alignas(16) FoliageInstance {
        Math::Mat4 Transform;       // 64 bytes - world transform matrix
        Math::Vec4 Color;           // 16 bytes - xyz=color tint, w=alpha
        Math::Vec4 WindParams;      // 16 bytes - xyz=wind offset, w=wind phase
        uint32_t MeshIndex;         // 4 bytes  - index into mesh array
        uint32_t MaterialIndex;     // 4 bytes  - index into material array
        float Scale;                // 4 bytes  - uniform scale factor
        float Padding;              // 4 bytes  - alignment padding

        FoliageInstance()
            : Transform(1.0f)
            , Color(1.0f, 1.0f, 1.0f, 1.0f)
            , WindParams(0.0f, 0.0f, 0.0f, 0.0f)
            , MeshIndex(0)
            , MaterialIndex(0)
            , Scale(1.0f)
            , Padding(0.0f)
        {}
    };
    static_assert(sizeof(FoliageInstance) == 112, "FoliageInstance must be 112 bytes");
    static_assert(alignof(FoliageInstance) == 16, "FoliageInstance must be 16-byte aligned");

    /**
     * @brief Indirect draw command for instanced rendering
     * Matches VkDrawIndexedIndirectCommand
     */
    struct FoliageDrawCommand {
        uint32_t IndexCount;
        uint32_t InstanceCount;
        uint32_t FirstIndex;
        int32_t  VertexOffset;
        uint32_t FirstInstance;
    };
    static_assert(sizeof(FoliageDrawCommand) == 20, "FoliageDrawCommand must be 20 bytes");

    /**
     * @brief Uniform data for foliage scatter compute shader
     */
    struct alignas(16) FoliageScatterUniforms {
        Math::Mat4 ViewProjection;      // 64 bytes - for frustum culling
        Math::Vec4 CameraPosition;      // 16 bytes - xyz=pos, w=time
        Math::Vec4 FrustumPlanes[6];    // 96 bytes - xyz=normal, w=distance
        Math::Vec4 ScatterMin;          // 16 bytes - xyz=min bounds
        Math::Vec4 ScatterMax;          // 16 bytes - xyz=max bounds
        Math::Vec4 HeightmapParams;     // 16 bytes - x=width, y=height, z=scale, w=offset
        Math::Vec4 WindParams;          // 16 bytes - xy=direction, z=strength, w=frequency
        float Density;                  // 4 bytes
        float MinScale;                 // 4 bytes
        float MaxScale;                 // 4 bytes
        float RotationVariation;        // 4 bytes
        float CullDistance;             // 4 bytes
        float MinHeight;                // 4 bytes
        float MaxHeight;                // 4 bytes
        float MinSlope;                 // 4 bytes
        float MaxSlope;                 // 4 bytes
        uint32_t TerrainAligned;        // 4 bytes
        uint32_t MeshIndex;             // 4 bytes
        uint32_t MaterialIndex;         // 4 bytes
        uint32_t MaxInstances;          // 4 bytes
        uint32_t RandomSeed;            // 4 bytes
        float Padding[2];               // 8 bytes

        FoliageScatterUniforms()
            : ViewProjection(1.0f)
            , CameraPosition(0.0f)
            , ScatterMin(-50.0f, 0.0f, -50.0f, 0.0f)
            , ScatterMax(50.0f, 0.0f, 50.0f, 0.0f)
            , HeightmapParams(256.0f, 256.0f, 1.0f, 0.0f)
            , WindParams(1.0f, 0.0f, 0.5f, 1.0f)
            , Density(10.0f)
            , MinScale(0.8f)
            , MaxScale(1.2f)
            , RotationVariation(360.0f)
            , CullDistance(100.0f)
            , MinHeight(0.0f)
            , MaxHeight(1000.0f)
            , MinSlope(0.0f)
            , MaxSlope(45.0f)
            , TerrainAligned(1)
            , MeshIndex(0)
            , MaterialIndex(0)
            , MaxInstances(100000)
            , RandomSeed(42)
        {
            for (int i = 0; i < 6; ++i) {
                FrustumPlanes[i] = Math::Vec4(0.0f);
            }
            Padding[0] = 0.0f;
            Padding[1] = 0.0f;
        }
    };
    static_assert(sizeof(FoliageScatterUniforms) == 320, "FoliageScatterUniforms must be 320 bytes");

    /**
     * @brief Wind animation parameters (updated per-frame)
     */
    struct alignas(16) FoliageWindUniforms {
        Math::Vec4 WindDirection;   // 16 bytes - xyz=direction, w=global time
        Math::Vec4 WindParams;      // 16 bytes - x=strength, y=frequency, z=gustiness, w=turbulence
        Math::Vec4 Padding[2];      // 32 bytes

        FoliageWindUniforms()
            : WindDirection(1.0f, 0.0f, 0.5f, 0.0f)
            , WindParams(0.5f, 1.0f, 0.3f, 0.1f)
        {
            Padding[0] = Math::Vec4(0.0f);
            Padding[1] = Math::Vec4(0.0f);
        }
    };
    static_assert(sizeof(FoliageWindUniforms) == 64, "FoliageWindUniforms must be 64 bytes");

    /**
     * @brief Per-frame rendering uniforms for foliage vertex/fragment shaders
     */
    struct alignas(16) FoliageRenderUniforms {
        Math::Mat4 View;            // 64 bytes
        Math::Mat4 Projection;      // 64 bytes
        Math::Vec4 CameraPosition;  // 16 bytes - xyz=pos, w=time
        Math::Vec4 LightDirection;  // 16 bytes - xyz=dir, w=intensity
        Math::Vec4 LightColor;      // 16 bytes - xyz=color, w=ambient
        Math::Vec4 WindDirection;   // 16 bytes - xyz=dir, w=time
        float WindStrength;         // 4 bytes
        float WindFrequency;        // 4 bytes
        float AlphaCutoff;          // 4 bytes
        float Padding;              // 4 bytes

        FoliageRenderUniforms()
            : View(1.0f)
            , Projection(1.0f)
            , CameraPosition(0.0f)
            , LightDirection(0.0f, -1.0f, 0.0f, 1.0f)
            , LightColor(1.0f, 1.0f, 1.0f, 0.1f)
            , WindDirection(1.0f, 0.0f, 0.5f, 0.0f)
            , WindStrength(0.5f)
            , WindFrequency(1.0f)
            , AlphaCutoff(0.5f)
            , Padding(0.0f)
        {}
    };
    static_assert(sizeof(FoliageRenderUniforms) == 208, "FoliageRenderUniforms must be 208 bytes");

} // namespace Renderer
} // namespace Core
