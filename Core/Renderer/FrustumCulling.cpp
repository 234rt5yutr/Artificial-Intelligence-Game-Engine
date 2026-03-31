#include "Core/Renderer/FrustumCulling.h"
#include "Core/Log.h"
#include "Core/Profile.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

// Use engine core log macros
#define LOG_INFO    ENGINE_CORE_INFO
#define LOG_WARN    ENGINE_CORE_WARN
#define LOG_DEBUG   ENGINE_CORE_TRACE
#define LOG_ERROR   ENGINE_CORE_ERROR

namespace Core {
namespace Renderer {

    //=========================================================================
    // Constructor / Destructor
    //=========================================================================

    FrustumCullingSystem::FrustumCullingSystem()
    {
        m_Objects.reserve(MAX_CULLED_OBJECTS);
        m_VisibleObjectsCPU.reserve(MAX_CULLED_OBJECTS);
        m_DrawCommandsCPU.reserve(MAX_CULLED_OBJECTS);
        
        for (int i = 0; i < 6; i++) {
            m_FrustumPlanes[i] = Math::Vec4(0.0f);
        }
    }

    FrustumCullingSystem::~FrustumCullingSystem()
    {
        Shutdown();
    }

    //=========================================================================
    // Initialization
    //=========================================================================

    void FrustumCullingSystem::Initialize(std::shared_ptr<RHI::RHIDevice> device)
    {
        if (m_Initialized) {
            LOG_WARN("FrustumCullingSystem already initialized");
            return;
        }

        m_Device = device;
        CreateBuffers();
        m_Initialized = true;

        LOG_INFO("FrustumCullingSystem initialized (max objects: {})", MAX_CULLED_OBJECTS);
    }

    void FrustumCullingSystem::Shutdown()
    {
        if (!m_Initialized) {
            return;
        }

        m_UniformBuffer.reset();
        m_ObjectDataBuffer.reset();
        m_DrawCommandBuffer.reset();
        m_DrawCountBuffer.reset();
        m_VisibleIndicesBuffer.reset();
        m_StagingBuffer.reset();
        m_Device.reset();

        m_Objects.clear();
        m_VisibleObjectsCPU.clear();
        m_DrawCommandsCPU.clear();
        m_ObjectCount = 0;
        m_Initialized = false;

        LOG_INFO("FrustumCullingSystem shutdown");
    }

    void FrustumCullingSystem::CreateBuffers()
    {
        if (!m_Device) {
            LOG_ERROR("Cannot create buffers: no device");
            return;
        }

        // Uniform buffer for culling parameters
        RHI::BufferDescriptor uniformDesc;
        uniformDesc.size = sizeof(CullingUniforms);
        uniformDesc.usage = RHI::BufferUsage::Uniform;
        uniformDesc.mapped = true;
        m_UniformBuffer = m_Device->CreateBuffer(uniformDesc);

        // Object data buffer (storage buffer)
        RHI::BufferDescriptor objectDataDesc;
        objectDataDesc.size = sizeof(ObjectData) * MAX_CULLED_OBJECTS;
        objectDataDesc.usage = RHI::BufferUsage::Storage;
        objectDataDesc.mapped = true;
        m_ObjectDataBuffer = m_Device->CreateBuffer(objectDataDesc);

        // Indirect draw command buffer
        RHI::BufferDescriptor drawCommandDesc;
        drawCommandDesc.size = sizeof(DrawIndexedIndirectCommand) * MAX_CULLED_OBJECTS;
        drawCommandDesc.usage = RHI::BufferUsage::Storage;
        drawCommandDesc.mapped = false;
        m_DrawCommandBuffer = m_Device->CreateBuffer(drawCommandDesc);

        // Draw count buffer (atomic counter)
        RHI::BufferDescriptor drawCountDesc;
        drawCountDesc.size = sizeof(uint32_t);
        drawCountDesc.usage = RHI::BufferUsage::Storage;
        drawCountDesc.mapped = true;
        m_DrawCountBuffer = m_Device->CreateBuffer(drawCountDesc);

        // Visible indices buffer
        RHI::BufferDescriptor visibleIndicesDesc;
        visibleIndicesDesc.size = sizeof(uint32_t) * MAX_CULLED_OBJECTS;
        visibleIndicesDesc.usage = RHI::BufferUsage::Storage;
        visibleIndicesDesc.mapped = false;
        m_VisibleIndicesBuffer = m_Device->CreateBuffer(visibleIndicesDesc);

        // Staging buffer for readback
        RHI::BufferDescriptor stagingDesc;
        stagingDesc.size = sizeof(uint32_t);
        stagingDesc.usage = RHI::BufferUsage::Staging;
        stagingDesc.mapped = true;
        m_StagingBuffer = m_Device->CreateBuffer(stagingDesc);

        LOG_DEBUG("Created frustum culling GPU buffers");
    }

    //=========================================================================
    // Configuration
    //=========================================================================

    void FrustumCullingSystem::SetConfig(const FrustumCullingConfig& config)
    {
        m_Config = config;
        m_Uniforms.EnableFrustumCull = config.EnableFrustumCulling ? 1 : 0;
        m_Uniforms.EnableDistanceCull = config.EnableDistanceCulling ? 1 : 0;
        m_Uniforms.MaxDrawDistance = config.MaxDrawDistance;
    }

    void FrustumCullingSystem::SetMaxDrawDistance(float distance)
    {
        m_Config.MaxDrawDistance = distance;
        m_Uniforms.MaxDrawDistance = distance;
    }

    void FrustumCullingSystem::EnableFrustumCulling(bool enable)
    {
        m_Config.EnableFrustumCulling = enable;
        m_Uniforms.EnableFrustumCull = enable ? 1 : 0;
    }

    void FrustumCullingSystem::EnableDistanceCulling(bool enable)
    {
        m_Config.EnableDistanceCulling = enable;
        m_Uniforms.EnableDistanceCull = enable ? 1 : 0;
    }

    //=========================================================================
    // Object Registration
    //=========================================================================

    uint32_t FrustumCullingSystem::RegisterObject(const ObjectData& objectData)
    {
        if (m_ObjectCount >= MAX_CULLED_OBJECTS) {
            LOG_WARN("Maximum object count reached ({})", MAX_CULLED_OBJECTS);
            return UINT32_MAX;
        }

        uint32_t index = m_ObjectCount;
        
        if (index < m_Objects.size()) {
            m_Objects[index] = objectData;
        } else {
            m_Objects.push_back(objectData);
        }
        
        m_ObjectCount++;
        m_ObjectBufferDirty = true;
        
        return index;
    }

    void FrustumCullingSystem::RegisterObjects(const std::vector<ObjectData>& objects)
    {
        for (const auto& obj : objects) {
            RegisterObject(obj);
        }
    }

    void FrustumCullingSystem::UpdateObject(uint32_t objectIndex, const ObjectData& objectData)
    {
        if (objectIndex >= m_ObjectCount) {
            LOG_WARN("Invalid object index: {}", objectIndex);
            return;
        }

        m_Objects[objectIndex] = objectData;
        m_ObjectBufferDirty = true;
    }

    void FrustumCullingSystem::ClearObjects()
    {
        m_ObjectCount = 0;
        m_ObjectBufferDirty = true;
        m_Stats.Reset();
    }

    //=========================================================================
    // Scene Integration
    //=========================================================================

    void FrustumCullingSystem::GatherObjectsFromScene(ECS::Scene& scene)
    {
        PROFILE_FUNCTION();

        ClearObjects();

        auto& registry = scene.GetRegistry();
        auto view = registry.view<ECS::TransformComponent, ECS::MeshComponent>();

        uint32_t instanceId = 0;
        for (auto entity : view) {
            auto& transform = view.get<ECS::TransformComponent>(entity);
            auto& mesh = view.get<ECS::MeshComponent>(entity);

            if (!mesh.IsValid() || !mesh.Visible) {
                continue;
            }

            ObjectData objData;
            objData.WorldMatrix = transform.WorldMatrix;
            objData.MeshIndex = 0;  // TODO: mesh registry index
            objData.MaterialIndex = mesh.MaterialIndex;
            objData.InstanceId = instanceId++;
            
            // Set flags
            objData.Flags = 0;
            if (mesh.Visible) objData.Flags |= 1;
            if (mesh.CastShadows) objData.Flags |= 2;

            // Compute bounding sphere from mesh if available
            if (mesh.MeshData && !mesh.MeshData->vertices.empty()) {
                std::vector<Math::Vec3> positions;
                positions.reserve(mesh.MeshData->vertices.size());
                for (const auto& vertex : mesh.MeshData->vertices) {
                    positions.push_back(vertex.position);
                }
                
                BoundingSphere sphere = ComputeBoundingSphere(positions);
                objData.BoundingSphere = Math::Vec4(sphere.Center, sphere.Radius);
                
                // Set index/vertex data from first primitive
                if (!mesh.MeshData->primitives.empty()) {
                    objData.FirstIndex = mesh.MeshData->primitives[0].firstIndex;
                    objData.IndexCount = mesh.MeshData->primitives[0].indexCount;
                }
            } else {
                // Default unit sphere
                objData.BoundingSphere = Math::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
            }

            RegisterObject(objData);
        }

        m_Stats.TotalObjects = m_ObjectCount;
        LOG_DEBUG("Gathered {} objects from scene for culling", m_ObjectCount);
    }

    BoundingSphere FrustumCullingSystem::ComputeBoundingSphere(const std::vector<Math::Vec3>& vertices)
    {
        if (vertices.empty()) {
            return BoundingSphere();
        }

        // Compute center as average of all points
        Math::Vec3 center(0.0f);
        for (const auto& v : vertices) {
            center += v;
        }
        center /= static_cast<float>(vertices.size());

        // Find maximum distance from center
        float maxDistSq = 0.0f;
        for (const auto& v : vertices) {
            float distSq = glm::dot(v - center, v - center);
            maxDistSq = std::max(maxDistSq, distSq);
        }

        return BoundingSphere(center, std::sqrt(maxDistSq));
    }

    AABB FrustumCullingSystem::ComputeAABB(const std::vector<Math::Vec3>& vertices)
    {
        if (vertices.empty()) {
            return AABB();
        }

        Math::Vec3 minPoint(std::numeric_limits<float>::max());
        Math::Vec3 maxPoint(std::numeric_limits<float>::lowest());

        for (const auto& v : vertices) {
            minPoint = glm::min(minPoint, v);
            maxPoint = glm::max(maxPoint, v);
        }

        Math::Vec3 center = (minPoint + maxPoint) * 0.5f;
        Math::Vec3 extents = (maxPoint - minPoint) * 0.5f;

        return AABB(center, extents);
    }

    //=========================================================================
    // Culling Execution
    //=========================================================================

    Math::Vec4 FrustumCullingSystem::NormalizePlane(const Math::Vec4& plane)
    {
        float length = glm::length(Math::Vec3(plane));
        if (length > 0.0f) {
            return plane / length;
        }
        return plane;
    }

    void FrustumCullingSystem::ExtractFrustumPlanes(const Math::Mat4& viewProjection)
    {
        // Extract frustum planes from view-projection matrix using Gribb-Hartmann method
        // Row-major extraction for GLM column-major matrices
        
        const Math::Mat4& m = viewProjection;

        // Left plane: row3 + row0
        m_FrustumPlanes[static_cast<uint32_t>(FrustumPlane::Left)] = NormalizePlane(Math::Vec4(
            m[0][3] + m[0][0],
            m[1][3] + m[1][0],
            m[2][3] + m[2][0],
            m[3][3] + m[3][0]
        ));

        // Right plane: row3 - row0
        m_FrustumPlanes[static_cast<uint32_t>(FrustumPlane::Right)] = NormalizePlane(Math::Vec4(
            m[0][3] - m[0][0],
            m[1][3] - m[1][0],
            m[2][3] - m[2][0],
            m[3][3] - m[3][0]
        ));

        // Bottom plane: row3 + row1
        m_FrustumPlanes[static_cast<uint32_t>(FrustumPlane::Bottom)] = NormalizePlane(Math::Vec4(
            m[0][3] + m[0][1],
            m[1][3] + m[1][1],
            m[2][3] + m[2][1],
            m[3][3] + m[3][1]
        ));

        // Top plane: row3 - row1
        m_FrustumPlanes[static_cast<uint32_t>(FrustumPlane::Top)] = NormalizePlane(Math::Vec4(
            m[0][3] - m[0][1],
            m[1][3] - m[1][1],
            m[2][3] - m[2][1],
            m[3][3] - m[3][1]
        ));

        // Near plane: row3 + row2 (for reverse-Z: row2)
        m_FrustumPlanes[static_cast<uint32_t>(FrustumPlane::Near)] = NormalizePlane(Math::Vec4(
            m[0][3] + m[0][2],
            m[1][3] + m[1][2],
            m[2][3] + m[2][2],
            m[3][3] + m[3][2]
        ));

        // Far plane: row3 - row2
        m_FrustumPlanes[static_cast<uint32_t>(FrustumPlane::Far)] = NormalizePlane(Math::Vec4(
            m[0][3] - m[0][2],
            m[1][3] - m[1][2],
            m[2][3] - m[2][2],
            m[3][3] - m[3][2]
        ));

        // Copy to uniforms
        for (int i = 0; i < 6; i++) {
            m_Uniforms.FrustumPlanes[i] = m_FrustumPlanes[i];
        }
    }

    void FrustumCullingSystem::UpdateCamera(const Math::Mat4& viewProjection, const Math::Vec3& cameraPosition)
    {
        ExtractFrustumPlanes(viewProjection);
        m_Uniforms.ViewProjection = viewProjection;
        m_Uniforms.CameraPosition = Math::Vec4(cameraPosition, 0.0f);
    }

    void FrustumCullingSystem::UpdateObjectBuffer()
    {
        if (!m_ObjectDataBuffer || !m_ObjectBufferDirty || m_ObjectCount == 0) {
            return;
        }

        void* data = nullptr;
        m_ObjectDataBuffer->Map(&data);
        if (data) {
            std::memcpy(data, m_Objects.data(), sizeof(ObjectData) * m_ObjectCount);
            m_ObjectDataBuffer->Unmap();
        }

        m_ObjectBufferDirty = false;
    }

    void FrustumCullingSystem::UpdateUniformBuffer()
    {
        if (!m_UniformBuffer) {
            return;
        }

        m_Uniforms.ObjectCount = m_ObjectCount;

        void* data = nullptr;
        m_UniformBuffer->Map(&data);
        if (data) {
            std::memcpy(data, &m_Uniforms, sizeof(CullingUniforms));
            m_UniformBuffer->Unmap();
        }
    }

    void FrustumCullingSystem::ResetDrawCount()
    {
        if (!m_DrawCountBuffer) {
            return;
        }

        void* data = nullptr;
        m_DrawCountBuffer->Map(&data);
        if (data) {
            uint32_t zero = 0;
            std::memcpy(data, &zero, sizeof(uint32_t));
            m_DrawCountBuffer->Unmap();
        }
    }

    void FrustumCullingSystem::ExecuteCulling(std::shared_ptr<RHI::RHICommandList> commandList)
    {
        PROFILE_FUNCTION();

        if (!m_Initialized || m_ObjectCount == 0) {
            return;
        }

        auto startTime = std::chrono::high_resolution_clock::now();

        // Update GPU buffers
        UpdateObjectBuffer();
        UpdateUniformBuffer();
        ResetDrawCount();

        // Dispatch compute shader
        uint32_t groupCount = (m_ObjectCount + FRUSTUM_CULL_WORKGROUP_SIZE - 1) / FRUSTUM_CULL_WORKGROUP_SIZE;
        commandList->Dispatch(groupCount, 1, 1);

        auto endTime = std::chrono::high_resolution_clock::now();
        m_Stats.CullTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    }

    void FrustumCullingSystem::ExecuteCullingCPU()
    {
        PROFILE_FUNCTION();

        if (m_ObjectCount == 0) {
            return;
        }

        auto startTime = std::chrono::high_resolution_clock::now();

        m_VisibleObjectsCPU.clear();
        m_DrawCommandsCPU.clear();

        for (uint32_t i = 0; i < m_ObjectCount; i++) {
            const ObjectData& obj = m_Objects[i];

            // Skip invisible objects
            if ((obj.Flags & 1) == 0) {
                continue;
            }

            // Transform bounding sphere to world space
            Math::Vec4 localSphere = obj.BoundingSphere;
            Math::Vec3 worldCenter = Math::Vec3(obj.WorldMatrix * Math::Vec4(localSphere.x, localSphere.y, localSphere.z, 1.0f));
            
            // Scale radius by maximum scale component
            Math::Vec3 scale(
                glm::length(Math::Vec3(obj.WorldMatrix[0])),
                glm::length(Math::Vec3(obj.WorldMatrix[1])),
                glm::length(Math::Vec3(obj.WorldMatrix[2]))
            );
            float maxScale = std::max({scale.x, scale.y, scale.z});
            float worldRadius = localSphere.w * maxScale;

            bool visible = true;

            // Frustum culling
            if (m_Config.EnableFrustumCulling) {
                visible = TestSphereAgainstFrustum(worldCenter, worldRadius);
            }

            // Distance culling
            if (visible && m_Config.EnableDistanceCulling) {
                Math::Vec3 cameraPos(m_Uniforms.CameraPosition);
                float distance = glm::length(worldCenter - cameraPos);
                visible = (distance <= m_Config.MaxDrawDistance);
            }

            if (visible) {
                m_VisibleObjectsCPU.push_back(i);

                DrawIndexedIndirectCommand cmd;
                cmd.IndexCount = obj.IndexCount;
                cmd.InstanceCount = 1;
                cmd.FirstIndex = obj.FirstIndex;
                cmd.VertexOffset = static_cast<int32_t>(obj.VertexOffset);
                cmd.FirstInstance = obj.InstanceId;
                m_DrawCommandsCPU.push_back(cmd);
            }
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        m_Stats.CullTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

        m_Stats.TotalObjects = m_ObjectCount;
        m_Stats.VisibleObjects = static_cast<uint32_t>(m_VisibleObjectsCPU.size());
        m_Stats.Calculate();
    }

    bool FrustumCullingSystem::TestSphereAgainstFrustum(const Math::Vec3& center, float radius) const
    {
        for (int i = 0; i < 6; i++) {
            float distance = glm::dot(Math::Vec3(m_FrustumPlanes[i]), center) + m_FrustumPlanes[i].w;
            if (distance < -radius) {
                return false;
            }
        }
        return true;
    }

    bool FrustumCullingSystem::TestAABBAgainstFrustum(const Math::Vec3& center, const Math::Vec3& extents) const
    {
        for (int i = 0; i < 6; i++) {
            Math::Vec3 planeNormal = Math::Vec3(m_FrustumPlanes[i]);
            float planeDistance = m_FrustumPlanes[i].w;

            // Calculate projection interval radius of AABB onto plane normal
            float r = glm::dot(extents, glm::abs(planeNormal));
            float s = glm::dot(planeNormal, center) + planeDistance;

            if (s < -r) {
                return false;
            }
        }
        return true;
    }

    //=========================================================================
    // Results
    //=========================================================================

    uint32_t FrustumCullingSystem::ReadbackDrawCount()
    {
        if (!m_DrawCountBuffer) {
            return 0;
        }

        // Note: In a real implementation, this would involve a GPU->CPU copy
        // and proper synchronization. For now, we read directly if mapped.
        void* data = nullptr;
        m_DrawCountBuffer->Map(&data);
        uint32_t count = 0;
        if (data) {
            count = *static_cast<uint32_t*>(data);
            m_DrawCountBuffer->Unmap();
        }

        m_Stats.VisibleObjects = count;
        m_Stats.TotalObjects = m_ObjectCount;
        m_Stats.Calculate();

        return count;
    }

} // namespace Renderer
} // namespace Core
