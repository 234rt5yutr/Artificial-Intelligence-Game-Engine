#include "ForwardPlus.h"
#include "Core/RHI/RHIDevice.h"
#include "Core/Log.h"
#include <cmath>

namespace Core {
namespace Renderer {

    void ForwardPlusLightData::Initialize(std::shared_ptr<RHI::RHIDevice> device, uint32_t screenWidth, uint32_t screenHeight) {
        CORE_LOG_INFO("Initializing Forward+ Light Data Grid Definitions & Buffers.");
        
        m_GridSizeX = static_cast<uint32_t>(std::ceil(static_cast<float>(screenWidth) / TILE_SIZE));
        m_GridSizeY = static_cast<uint32_t>(std::ceil(static_cast<float>(screenHeight) / TILE_SIZE));
        m_TotalClusters = m_GridSizeX * m_GridSizeY * m_GridSizeZ;

        // 1. Light Buffer: Holds the actual light structs (PointLights or others).
        RHI::BufferDescriptor lightDesc{};
        lightDesc.size = MAX_LIGHTS * sizeof(PointLight);
        lightDesc.usage = RHI::BufferUsage::Storage;
        lightDesc.mapped = false;
        m_LightBuffer = device->CreateBuffer(lightDesc);

        // 2. Clusters Buffer: Holds the min/max extents of the frustum slices (AABB/Frustum definition).
        RHI::BufferDescriptor clusterDesc{};
        clusterDesc.size = m_TotalClusters * sizeof(Cluster);
        clusterDesc.usage = RHI::BufferUsage::Storage;
        clusterDesc.mapped = false;
        m_ClusterBuffer = device->CreateBuffer(clusterDesc);

        // 3. Light Grid Buffer: A buffer mapping each cluster to an offset/count in the Light Index List.
        RHI::BufferDescriptor gridDesc{};
        gridDesc.size = m_TotalClusters * sizeof(LightGrid);
        gridDesc.usage = RHI::BufferUsage::Storage;
        gridDesc.mapped = false;
        m_LightGridBuffer = device->CreateBuffer(gridDesc);

        // 4. Light Index List GPU Buffer: An array of global light indices.
        // Dimensioned by arbitrary heuristic (total clusters * max average overlapping lights).
        RHI::BufferDescriptor indexListDesc{};
        indexListDesc.size = m_TotalClusters * MAX_LIGHTS_PER_CLUSTER * sizeof(uint32_t);
        indexListDesc.usage = RHI::BufferUsage::Storage;
        indexListDesc.mapped = false;
        m_LightIndexListBuffer = device->CreateBuffer(indexListDesc);

        CORE_LOG_INFO("Forward+ Lighting GPU Buffers created. Total clusters: {}", m_TotalClusters);
    }

    void ForwardPlusLightData::Shutdown() {
        m_LightBuffer.reset();
        m_ClusterBuffer.reset();
        m_LightGridBuffer.reset();
        m_LightIndexListBuffer.reset();
    }

} // namespace Renderer
} // namespace Core
