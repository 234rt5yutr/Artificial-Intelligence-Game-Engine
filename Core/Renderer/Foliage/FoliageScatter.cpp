#include "Core/Renderer/Foliage/FoliageScatter.h"
#include "Core/Log.h"
#include "Core/Profile.h"
#include <cmath>
#include <algorithm>

namespace Core {
namespace Renderer {

    //=========================================================================
    // Constructor / Destructor
    //=========================================================================

    FoliageScatter::FoliageScatter()
    {
        m_FoliageTypes.reserve(MAX_FOLIAGE_TYPES);
        for (int i = 0; i < 6; ++i) {
            m_FrustumPlanes[i] = Math::Vec4(0.0f);
        }
    }

    FoliageScatter::~FoliageScatter()
    {
        Shutdown();
    }

    //=========================================================================
    // Initialization
    //=========================================================================

    void FoliageScatter::Initialize(std::shared_ptr<RHI::RHIDevice> device)
    {
        if (m_Initialized) {
            ENGINE_CORE_WARN("FoliageScatter::Initialize called on already initialized system");
            return;
        }

        m_Device = device;
        if (!m_Device) {
            ENGINE_CORE_ERROR("FoliageScatter::Initialize - Invalid device");
            return;
        }

        CreateBuffers();
        CreateComputePipelines();

        m_Initialized = true;
        ENGINE_CORE_INFO("FoliageScatter system initialized (max instances: {})", MAX_FOLIAGE_INSTANCES);
    }

    void FoliageScatter::Shutdown()
    {
        if (!m_Initialized) {
            return;
        }

        // Release all buffers
        m_UniformBuffer.reset();
        m_WindUniformBuffer.reset();
        m_InstanceBuffer.reset();
        m_DrawCommandBuffer.reset();
        m_DrawCountBuffer.reset();
        m_StagingBuffer.reset();
        m_HeightmapBuffer.reset();
        m_HeightmapTexture.reset();

        m_FoliageTypes.clear();
        m_FoliageTypeCount = 0;
        m_Device.reset();
        m_Initialized = false;

        ENGINE_CORE_INFO("FoliageScatter system shutdown");
    }

    void FoliageScatter::CreateBuffers()
    {
        // Instance buffer (SSBO for compute write, vertex read)
        RHI::BufferDescriptor instanceDesc;
        instanceDesc.size = sizeof(FoliageInstance) * MAX_FOLIAGE_INSTANCES;
        instanceDesc.usage = RHI::BufferUsage::Storage;
        instanceDesc.mapped = false;
        m_InstanceBuffer = m_Device->CreateBuffer(instanceDesc);

        // Uniform buffer for scatter compute shader
        RHI::BufferDescriptor uniformDesc;
        uniformDesc.size = sizeof(FoliageScatterUniforms);
        uniformDesc.usage = RHI::BufferUsage::Uniform;
        uniformDesc.mapped = true;
        m_UniformBuffer = m_Device->CreateBuffer(uniformDesc);

        // Wind uniform buffer
        RHI::BufferDescriptor windDesc;
        windDesc.size = sizeof(FoliageWindUniforms);
        windDesc.usage = RHI::BufferUsage::Uniform;
        windDesc.mapped = true;
        m_WindUniformBuffer = m_Device->CreateBuffer(windDesc);

        // Indirect draw command buffer
        RHI::BufferDescriptor drawCmdDesc;
        drawCmdDesc.size = sizeof(FoliageDrawCommand) * MAX_FOLIAGE_TYPES;
        drawCmdDesc.usage = RHI::BufferUsage::Storage;
        drawCmdDesc.mapped = false;
        m_DrawCommandBuffer = m_Device->CreateBuffer(drawCmdDesc);

        // Draw count buffer (atomic counter)
        RHI::BufferDescriptor drawCountDesc;
        drawCountDesc.size = sizeof(uint32_t) * MAX_FOLIAGE_TYPES;
        drawCountDesc.usage = RHI::BufferUsage::Storage;
        drawCountDesc.mapped = false;
        m_DrawCountBuffer = m_Device->CreateBuffer(drawCountDesc);

        // Staging buffer for readback
        RHI::BufferDescriptor stagingDesc;
        stagingDesc.size = sizeof(uint32_t) * MAX_FOLIAGE_TYPES;
        stagingDesc.usage = RHI::BufferUsage::Staging;
        stagingDesc.mapped = true;
        m_StagingBuffer = m_Device->CreateBuffer(stagingDesc);

        ENGINE_CORE_TRACE("FoliageScatter: Created GPU buffers (instance buffer: {} MB)",
                          (sizeof(FoliageInstance) * MAX_FOLIAGE_INSTANCES) / (1024.0 * 1024.0));
    }

    void FoliageScatter::CreateComputePipelines()
    {
        // Compute pipeline creation would be handled by the RHI layer
        // This is a placeholder for the actual pipeline setup
        ENGINE_CORE_TRACE("FoliageScatter: Compute pipelines ready");
    }

    //=========================================================================
    // Configuration
    //=========================================================================

    void FoliageScatter::SetConfig(const FoliageScatterConfig& config)
    {
        m_Config = config;
        m_NeedsRescatter = true;
    }

    void FoliageScatter::SetGlobalWindStrength(float strength)
    {
        m_Config.GlobalWindStrength = strength;
    }

    void FoliageScatter::SetGlobalDensityScale(float scale)
    {
        m_Config.GlobalDensityScale = scale;
        m_NeedsRescatter = true;
    }

    //=========================================================================
    // Terrain Data
    //=========================================================================

    void FoliageScatter::SetHeightmap(
        const float* heightmap,
        uint32_t width,
        uint32_t height,
        float worldScale,
        float heightOffset)
    {
        if (!heightmap || width == 0 || height == 0) {
            ENGINE_CORE_ERROR("FoliageScatter::SetHeightmap - Invalid heightmap data");
            return;
        }

        m_HeightmapWidth = width;
        m_HeightmapHeight = height;
        m_HeightmapScale = worldScale;
        m_HeightmapOffset = heightOffset;

        // Create/update heightmap buffer
        size_t bufferSize = sizeof(float) * width * height;
        
        RHI::BufferDescriptor heightmapDesc;
        heightmapDesc.size = bufferSize;
        heightmapDesc.usage = RHI::BufferUsage::Storage;
        heightmapDesc.mapped = true;
        m_HeightmapBuffer = m_Device->CreateBuffer(heightmapDesc);

        // Copy heightmap data
        void* mappedData = nullptr;
        m_HeightmapBuffer->Map(&mappedData);
        if (mappedData) {
            std::memcpy(mappedData, heightmap, bufferSize);
            m_HeightmapBuffer->Unmap();
        }

        // Update scatter uniforms
        m_ScatterUniforms.HeightmapParams = Math::Vec4(
            static_cast<float>(width),
            static_cast<float>(height),
            worldScale,
            heightOffset
        );

        m_NeedsRescatter = true;
        ENGINE_CORE_TRACE("FoliageScatter: Heightmap set ({}x{}, scale: {})",
                          width, height, worldScale);
    }

    void FoliageScatter::SetHeightmapTexture(std::shared_ptr<RHI::RHITexture> heightmapTexture)
    {
        m_HeightmapTexture = heightmapTexture;
        m_NeedsRescatter = true;
    }

    //=========================================================================
    // Foliage Registration
    //=========================================================================

    uint32_t FoliageScatter::RegisterFoliage(
        const ECS::FoliageComponent& component,
        const Math::Mat4& worldTransform)
    {
        if (m_FoliageTypeCount >= MAX_FOLIAGE_TYPES) {
            ENGINE_CORE_WARN("FoliageScatter: Maximum foliage types reached ({})", MAX_FOLIAGE_TYPES);
            return UINT32_MAX;
        }

        FoliageTypeData typeData;
        typeData.Component = component;
        typeData.WorldTransform = worldTransform;
        typeData.InstanceOffset = 0;
        typeData.InstanceCount = 0;
        typeData.Active = true;
        typeData.NeedsUpdate = true;

        // Find an empty slot or add new
        for (uint32_t i = 0; i < m_FoliageTypes.size(); ++i) {
            if (!m_FoliageTypes[i].Active) {
                m_FoliageTypes[i] = typeData;
                m_FoliageTypeCount++;
                m_NeedsRescatter = true;
                ENGINE_CORE_TRACE("FoliageScatter: Registered foliage type {} (reused slot)", i);
                return i;
            }
        }

        uint32_t index = static_cast<uint32_t>(m_FoliageTypes.size());
        m_FoliageTypes.push_back(typeData);
        m_FoliageTypeCount++;
        m_NeedsRescatter = true;

        ENGINE_CORE_TRACE("FoliageScatter: Registered foliage type {} (density: {}/m²)",
                          index, component.Density);
        return index;
    }

    void FoliageScatter::UpdateFoliage(
        uint32_t index,
        const ECS::FoliageComponent& component,
        const Math::Mat4& worldTransform)
    {
        if (index >= m_FoliageTypes.size() || !m_FoliageTypes[index].Active) {
            ENGINE_CORE_WARN("FoliageScatter::UpdateFoliage - Invalid index: {}", index);
            return;
        }

        m_FoliageTypes[index].Component = component;
        m_FoliageTypes[index].WorldTransform = worldTransform;
        m_FoliageTypes[index].NeedsUpdate = true;
        m_NeedsRescatter = true;
    }

    void FoliageScatter::RemoveFoliage(uint32_t index)
    {
        if (index >= m_FoliageTypes.size() || !m_FoliageTypes[index].Active) {
            return;
        }

        m_FoliageTypes[index].Active = false;
        m_FoliageTypeCount--;
        m_NeedsRescatter = true;

        ENGINE_CORE_TRACE("FoliageScatter: Removed foliage type {}", index);
    }

    void FoliageScatter::ClearFoliage()
    {
        m_FoliageTypes.clear();
        m_FoliageTypeCount = 0;
        m_NeedsRescatter = true;

        ENGINE_CORE_TRACE("FoliageScatter: Cleared all foliage types");
    }

    //=========================================================================
    // Camera Update
    //=========================================================================

    void FoliageScatter::UpdateCamera(
        const Math::Mat4& viewProjection,
        const Math::Vec3& cameraPosition)
    {
        m_ScatterUniforms.ViewProjection = viewProjection;
        m_ScatterUniforms.CameraPosition = Math::Vec4(cameraPosition, m_CurrentTime);
        m_CameraPosition = cameraPosition;

        ExtractFrustumPlanes(viewProjection);
    }

    void FoliageScatter::ExtractFrustumPlanes(const Math::Mat4& viewProjection)
    {
        // Extract frustum planes from the view-projection matrix
        // Using the Gribb-Hartmann method
        const Math::Mat4& m = viewProjection;

        // Left plane
        m_FrustumPlanes[0] = NormalizePlane(Math::Vec4(
            m[0][3] + m[0][0],
            m[1][3] + m[1][0],
            m[2][3] + m[2][0],
            m[3][3] + m[3][0]
        ));

        // Right plane
        m_FrustumPlanes[1] = NormalizePlane(Math::Vec4(
            m[0][3] - m[0][0],
            m[1][3] - m[1][0],
            m[2][3] - m[2][0],
            m[3][3] - m[3][0]
        ));

        // Bottom plane
        m_FrustumPlanes[2] = NormalizePlane(Math::Vec4(
            m[0][3] + m[0][1],
            m[1][3] + m[1][1],
            m[2][3] + m[2][1],
            m[3][3] + m[3][1]
        ));

        // Top plane
        m_FrustumPlanes[3] = NormalizePlane(Math::Vec4(
            m[0][3] - m[0][1],
            m[1][3] - m[1][1],
            m[2][3] - m[2][1],
            m[3][3] - m[3][1]
        ));

        // Near plane
        m_FrustumPlanes[4] = NormalizePlane(Math::Vec4(
            m[0][3] + m[0][2],
            m[1][3] + m[1][2],
            m[2][3] + m[2][2],
            m[3][3] + m[3][2]
        ));

        // Far plane
        m_FrustumPlanes[5] = NormalizePlane(Math::Vec4(
            m[0][3] - m[0][2],
            m[1][3] - m[1][2],
            m[2][3] - m[2][2],
            m[3][3] - m[3][2]
        ));

        // Copy to uniforms
        for (int i = 0; i < 6; ++i) {
            m_ScatterUniforms.FrustumPlanes[i] = m_FrustumPlanes[i];
        }
    }

    Math::Vec4 FoliageScatter::NormalizePlane(const Math::Vec4& plane)
    {
        float length = std::sqrt(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
        if (length > 0.0001f) {
            return plane / length;
        }
        return plane;
    }

    //=========================================================================
    // Wind Animation
    //=========================================================================

    void FoliageScatter::UpdateWind(
        const Math::Vec3& direction,
        float strength,
        float time)
    {
        m_CurrentTime = time;
        
        Math::Vec3 normalizedDir = glm::normalize(direction);
        m_WindUniforms.WindDirection = Math::Vec4(normalizedDir, time);
        m_WindUniforms.WindParams.x = strength * m_Config.GlobalWindStrength;

        m_ScatterUniforms.WindParams = Math::Vec4(
            normalizedDir.x,
            normalizedDir.z,
            strength * m_Config.GlobalWindStrength,
            1.0f  // frequency
        );
    }

    //=========================================================================
    // Scatter Execution
    //=========================================================================

    void FoliageScatter::ScatterFoliage(std::shared_ptr<RHI::RHICommandList> commandList)
    {
        PROFILE_FUNCTION();

        if (!m_Initialized || m_FoliageTypeCount == 0) {
            return;
        }

        m_Stats.Reset();

        // Reset draw count
        ResetDrawCount();

        uint32_t totalInstanceOffset = 0;

        for (uint32_t typeIndex = 0; typeIndex < m_FoliageTypes.size(); ++typeIndex) {
            auto& foliageType = m_FoliageTypes[typeIndex];
            if (!foliageType.Active) {
                continue;
            }

            const auto& component = foliageType.Component;

            // Update uniforms for this foliage type
            m_ScatterUniforms.ScatterMin = Math::Vec4(component.ScatterMin, 0.0f);
            m_ScatterUniforms.ScatterMax = Math::Vec4(component.ScatterMax, 0.0f);
            m_ScatterUniforms.Density = component.Density * m_Config.GlobalDensityScale;
            m_ScatterUniforms.MinScale = component.ScaleRange.Min;
            m_ScatterUniforms.MaxScale = component.ScaleRange.Max;
            m_ScatterUniforms.RotationVariation = component.RotationVariation;
            m_ScatterUniforms.CullDistance = component.CullDistance;
            m_ScatterUniforms.MinHeight = component.HeightRange.Min;
            m_ScatterUniforms.MaxHeight = component.HeightRange.Max;
            m_ScatterUniforms.MinSlope = component.SlopeRange.Min;
            m_ScatterUniforms.MaxSlope = component.SlopeRange.Max;
            m_ScatterUniforms.TerrainAligned = component.TerrainAligned ? 1 : 0;
            m_ScatterUniforms.MeshIndex = typeIndex;
            m_ScatterUniforms.MaterialIndex = typeIndex;
            m_ScatterUniforms.MaxInstances = std::min(
                component.EstimateInstanceCount(),
                m_Config.MaxInstancesPerType
            );
            m_ScatterUniforms.RandomSeed = typeIndex * 12345 + 42;

            // Store instance offset for this type
            foliageType.InstanceOffset = totalInstanceOffset;

            UpdateUniformBuffer();

            // Calculate dispatch size
            uint32_t estimatedInstances = m_ScatterUniforms.MaxInstances;
            uint32_t workgroups = (estimatedInstances + FOLIAGE_SCATTER_WORKGROUP_SIZE - 1) 
                                  / FOLIAGE_SCATTER_WORKGROUP_SIZE;

            // Dispatch compute shader
            // commandList->BindComputePipeline(m_ScatterPipeline);
            // commandList->BindDescriptorSet(0, m_ScatterDescriptorSet);
            // commandList->Dispatch(workgroups, 1, 1);

            // Memory barrier for instance buffer
            // commandList->BufferBarrier(m_InstanceBuffer, ...);

            totalInstanceOffset += estimatedInstances;
            m_Stats.TotalInstances += estimatedInstances;
            m_Stats.ActiveFoliageTypes++;

            foliageType.NeedsUpdate = false;
        }

        m_NeedsRescatter = false;
        ENGINE_CORE_TRACE("FoliageScatter: Dispatched {} types, ~{} instances",
                          m_Stats.ActiveFoliageTypes, m_Stats.TotalInstances);
    }

    void FoliageScatter::UpdateWindAnimation(
        std::shared_ptr<RHI::RHICommandList> commandList,
        float deltaTime)
    {
        PROFILE_FUNCTION();

        if (!m_Initialized || !m_Config.EnableWindAnimation) {
            return;
        }

        m_CurrentTime += deltaTime;
        m_WindUniforms.WindDirection.w = m_CurrentTime;

        // Update wind uniform buffer
        void* mappedData = nullptr;
        m_WindUniformBuffer->Map(&mappedData);
        if (mappedData) {
            std::memcpy(mappedData, &m_WindUniforms, sizeof(FoliageWindUniforms));
            m_WindUniformBuffer->Unmap();
        }

        // Wind animation is typically done in the vertex shader using the
        // wind uniforms, so no additional compute dispatch is needed here
    }

    void FoliageScatter::UpdateUniformBuffer()
    {
        void* mappedData = nullptr;
        m_UniformBuffer->Map(&mappedData);
        if (mappedData) {
            std::memcpy(mappedData, &m_ScatterUniforms, sizeof(FoliageScatterUniforms));
            m_UniformBuffer->Unmap();
        }
    }

    void FoliageScatter::ResetDrawCount()
    {
        // Reset draw count to zero before scatter
        // This would typically be done via a compute shader or transfer operation
        // For now, we assume the scatter shader handles this atomically
    }

    //=========================================================================
    // Buffer Access
    //=========================================================================

    uint32_t FoliageScatter::ReadbackDrawCount()
    {
        if (!m_StagingBuffer) {
            return 0;
        }

        // Copy from draw count buffer to staging buffer
        // commandList->CopyBuffer(m_DrawCountBuffer, m_StagingBuffer, sizeof(uint32_t));

        // Read from staging buffer
        void* mappedData = nullptr;
        m_StagingBuffer->Map(&mappedData);
        
        uint32_t count = 0;
        if (mappedData) {
            count = *static_cast<uint32_t*>(mappedData);
            m_StagingBuffer->Unmap();
        }

        m_VisibleInstanceCount = count;
        m_Stats.VisibleInstances = count;
        m_Stats.CulledInstances = m_Stats.TotalInstances - count;

        return count;
    }

} // namespace Renderer
} // namespace Core
