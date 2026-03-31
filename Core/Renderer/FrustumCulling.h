#pragma once

#include "Core/Math/Math.h"
#include "Core/RHI/RHIBuffer.h"
#include "Core/RHI/RHIDevice.h"
#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/MeshComponent.h"
#include "Core/ECS/Components/CameraComponent.h"
#include <memory>
#include <vector>
#include <array>
#include <cstdint>

namespace Core {
namespace Renderer {

    //=========================================================================
    // Constants
    //=========================================================================
    
    constexpr uint32_t MAX_CULLED_OBJECTS = 100000;
    constexpr uint32_t FRUSTUM_CULL_WORKGROUP_SIZE = 64;

    //=========================================================================
    // GPU-aligned Structures (must match shader)
    //=========================================================================

    // Axis-Aligned Bounding Box
    struct alignas(16) AABB {
        Math::Vec4 Center;   // xyz = center, w = padding
        Math::Vec4 Extents;  // xyz = half-extents, w = padding

        AABB() : Center(0.0f), Extents(0.0f) {}
        AABB(const Math::Vec3& center, const Math::Vec3& extents)
            : Center(center.x, center.y, center.z, 0.0f)
            , Extents(extents.x, extents.y, extents.z, 0.0f) {}
    };

    // Bounding Sphere
    struct alignas(16) BoundingSphere {
        Math::Vec3 Center;
        float Radius;

        BoundingSphere() : Center(0.0f), Radius(0.0f) {}
        BoundingSphere(const Math::Vec3& center, float radius)
            : Center(center), Radius(radius) {}
    };

    // Per-object data for GPU culling
    struct alignas(16) ObjectData {
        Math::Mat4 WorldMatrix;
        Math::Vec4 BoundingSphere;  // xyz = local center, w = radius
        uint32_t MeshIndex;
        uint32_t MaterialIndex;
        uint32_t FirstIndex;
        uint32_t IndexCount;
        uint32_t VertexOffset;
        uint32_t InstanceId;
        uint32_t Flags;             // Bit 0: visible, Bit 1: cast shadows
        uint32_t Padding;

        ObjectData()
            : WorldMatrix(1.0f)
            , BoundingSphere(0.0f, 0.0f, 0.0f, 1.0f)
            , MeshIndex(0), MaterialIndex(0)
            , FirstIndex(0), IndexCount(0)
            , VertexOffset(0), InstanceId(0)
            , Flags(1), Padding(0) {}
    };
    static_assert(sizeof(ObjectData) == 112, "ObjectData must be 112 bytes");

    // Indirect draw command (matches VkDrawIndexedIndirectCommand)
    struct DrawIndexedIndirectCommand {
        uint32_t IndexCount;
        uint32_t InstanceCount;
        uint32_t FirstIndex;
        int32_t  VertexOffset;
        uint32_t FirstInstance;
    };
    static_assert(sizeof(DrawIndexedIndirectCommand) == 20, "DrawIndexedIndirectCommand must be 20 bytes");

    // Uniform data for culling shader
    struct alignas(16) CullingUniforms {
        Math::Mat4 ViewProjection;
        Math::Vec4 FrustumPlanes[6];  // xyz = normal, w = distance
        Math::Vec4 CameraPosition;    // xyz = camera pos, w = unused
        uint32_t ObjectCount;
        uint32_t EnableFrustumCull;
        uint32_t EnableDistanceCull;
        float MaxDrawDistance;

        CullingUniforms()
            : ViewProjection(1.0f)
            , CameraPosition(0.0f)
            , ObjectCount(0)
            , EnableFrustumCull(1)
            , EnableDistanceCull(1)
            , MaxDrawDistance(1000.0f)
        {
            for (int i = 0; i < 6; i++) {
                FrustumPlanes[i] = Math::Vec4(0.0f);
            }
        }
    };
    static_assert(sizeof(CullingUniforms) == 192, "CullingUniforms must be 192 bytes");

    //=========================================================================
    // Frustum Plane Indices
    //=========================================================================
    
    enum class FrustumPlane : uint32_t {
        Left = 0,
        Right = 1,
        Bottom = 2,
        Top = 3,
        Near = 4,
        Far = 5,
        Count = 6
    };

    //=========================================================================
    // Culling Statistics
    //=========================================================================
    
    struct CullingStats {
        uint32_t TotalObjects;
        uint32_t VisibleObjects;
        uint32_t CulledObjects;
        float CullPercentage;
        double CullTimeMs;

        CullingStats()
            : TotalObjects(0), VisibleObjects(0), CulledObjects(0)
            , CullPercentage(0.0f), CullTimeMs(0.0) {}

        void Reset() {
            TotalObjects = 0;
            VisibleObjects = 0;
            CulledObjects = 0;
            CullPercentage = 0.0f;
            CullTimeMs = 0.0;
        }

        void Calculate() {
            CulledObjects = TotalObjects - VisibleObjects;
            if (TotalObjects > 0) {
                CullPercentage = (static_cast<float>(CulledObjects) / static_cast<float>(TotalObjects)) * 100.0f;
            }
        }
    };

    //=========================================================================
    // Configuration
    //=========================================================================
    
    struct FrustumCullingConfig {
        bool EnableFrustumCulling = true;
        bool EnableDistanceCulling = true;
        float MaxDrawDistance = 1000.0f;
        bool UseBoundingSpheres = true;   // If false, use AABBs
        bool EnableAsyncReadback = false; // Async GPU readback of stats
    };

    //=========================================================================
    // Frustum Culling System
    //=========================================================================

    class FrustumCullingSystem {
    public:
        FrustumCullingSystem();
        ~FrustumCullingSystem();

        // Non-copyable
        FrustumCullingSystem(const FrustumCullingSystem&) = delete;
        FrustumCullingSystem& operator=(const FrustumCullingSystem&) = delete;

        //---------------------------------------------------------------------
        // Initialization
        //---------------------------------------------------------------------
        
        void Initialize(std::shared_ptr<RHI::RHIDevice> device);
        void Shutdown();

        //---------------------------------------------------------------------
        // Configuration
        //---------------------------------------------------------------------
        
        void SetConfig(const FrustumCullingConfig& config);
        const FrustumCullingConfig& GetConfig() const { return m_Config; }

        void SetMaxDrawDistance(float distance);
        void EnableFrustumCulling(bool enable);
        void EnableDistanceCulling(bool enable);

        //---------------------------------------------------------------------
        // Object Registration
        //---------------------------------------------------------------------
        
        // Register an object for GPU culling
        uint32_t RegisterObject(const ObjectData& objectData);
        
        // Batch register multiple objects
        void RegisterObjects(const std::vector<ObjectData>& objects);
        
        // Update an existing object's data
        void UpdateObject(uint32_t objectIndex, const ObjectData& objectData);
        
        // Clear all registered objects
        void ClearObjects();

        // Get current object count
        uint32_t GetObjectCount() const { return m_ObjectCount; }

        //---------------------------------------------------------------------
        // Scene Integration
        //---------------------------------------------------------------------
        
        // Gather objects from ECS scene
        void GatherObjectsFromScene(ECS::Scene& scene);
        
        // Compute bounding sphere from mesh vertices
        static BoundingSphere ComputeBoundingSphere(const std::vector<Math::Vec3>& vertices);
        
        // Compute AABB from mesh vertices
        static AABB ComputeAABB(const std::vector<Math::Vec3>& vertices);

        //---------------------------------------------------------------------
        // Culling Execution
        //---------------------------------------------------------------------
        
        // Extract frustum planes from view-projection matrix
        void ExtractFrustumPlanes(const Math::Mat4& viewProjection);
        
        // Update camera data for culling
        void UpdateCamera(const Math::Mat4& viewProjection, const Math::Vec3& cameraPosition);
        
        // Execute GPU frustum culling (dispatch compute shader)
        void ExecuteCulling(std::shared_ptr<RHI::RHICommandList> commandList);
        
        // CPU-side frustum culling (fallback/debug)
        void ExecuteCullingCPU();

        //---------------------------------------------------------------------
        // Results
        //---------------------------------------------------------------------
        
        // Get the indirect draw command buffer (for vkCmdDrawIndexedIndirect)
        std::shared_ptr<RHI::RHIBuffer> GetDrawCommandBuffer() const { return m_DrawCommandBuffer; }
        
        // Get the draw count buffer (for vkCmdDrawIndexedIndirectCount)
        std::shared_ptr<RHI::RHIBuffer> GetDrawCountBuffer() const { return m_DrawCountBuffer; }
        
        // Get visible object indices
        std::shared_ptr<RHI::RHIBuffer> GetVisibleIndicesBuffer() const { return m_VisibleIndicesBuffer; }
        
        // Read back draw count from GPU (blocking)
        uint32_t ReadbackDrawCount();
        
        // Get culling statistics
        const CullingStats& GetStats() const { return m_Stats; }

        //---------------------------------------------------------------------
        // Debug
        //---------------------------------------------------------------------
        
        // Get frustum planes for debug visualization
        const std::array<Math::Vec4, 6>& GetFrustumPlanes() const { return m_FrustumPlanes; }
        
        // Get visible object list after CPU culling
        const std::vector<uint32_t>& GetVisibleObjectsCPU() const { return m_VisibleObjectsCPU; }

    private:
        void CreateBuffers();
        void UpdateObjectBuffer();
        void UpdateUniformBuffer();
        void ResetDrawCount();

        // Frustum plane extraction helpers
        static Math::Vec4 NormalizePlane(const Math::Vec4& plane);

        // CPU culling helpers
        bool TestSphereAgainstFrustum(const Math::Vec3& center, float radius) const;
        bool TestAABBAgainstFrustum(const Math::Vec3& center, const Math::Vec3& extents) const;

    private:
        std::shared_ptr<RHI::RHIDevice> m_Device;
        FrustumCullingConfig m_Config;
        CullingStats m_Stats;
        bool m_Initialized = false;

        // Object data
        std::vector<ObjectData> m_Objects;
        uint32_t m_ObjectCount = 0;
        bool m_ObjectBufferDirty = true;

        // GPU Buffers
        std::shared_ptr<RHI::RHIBuffer> m_UniformBuffer;
        std::shared_ptr<RHI::RHIBuffer> m_ObjectDataBuffer;
        std::shared_ptr<RHI::RHIBuffer> m_DrawCommandBuffer;
        std::shared_ptr<RHI::RHIBuffer> m_DrawCountBuffer;
        std::shared_ptr<RHI::RHIBuffer> m_VisibleIndicesBuffer;
        std::shared_ptr<RHI::RHIBuffer> m_StagingBuffer;  // For readback

        // Culling uniforms
        CullingUniforms m_Uniforms;
        std::array<Math::Vec4, 6> m_FrustumPlanes;

        // CPU culling results (for fallback/debug)
        std::vector<uint32_t> m_VisibleObjectsCPU;
        std::vector<DrawIndexedIndirectCommand> m_DrawCommandsCPU;
    };

} // namespace Renderer
} // namespace Core
