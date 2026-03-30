#pragma once

#include "Core/Math/Math.h"
#include "Core/RHI/RHIBuffer.h"
#include <memory>
#include <vector>

namespace Core {
namespace Renderer {

    // Forward+ Constants
    constexpr uint32_t TILE_SIZE = 16;
    constexpr uint32_t NUM_Z_SLICES = 32;
    constexpr uint32_t MAX_LIGHTS_PER_CLUSTER = 256;
    constexpr uint32_t MAX_LIGHTS = 10000;

    // GPU-aligned structures
    struct alignas(16) PointLight {
        glm::vec4 positionAndRadius; // xyz = position, w = radius
        glm::vec4 colorAndIntensity; // xyz = color, w = intensity
    };

    struct alignas(16) Cluster {
        glm::vec4 minPoint;
        glm::vec4 maxPoint;
    };

    struct alignas(8) LightGrid {
        uint32_t offset;
        uint32_t count;
    };

    // Forward+ Light Data Manager
    class ForwardPlusLightData {
    public:
        ForwardPlusLightData() = default;
        ~ForwardPlusLightData() = default;

        void Initialize(std::shared_ptr<RHI::RHIDevice> device, uint32_t screenWidth, uint32_t screenHeight);
        void Shutdown();

        // Getters for buffers (to be implemented via RHI Device when fully wired)
        std::shared_ptr<RHI::RHIBuffer> GetLightBuffer() const { return m_LightBuffer; }
        std::shared_ptr<RHI::RHIBuffer> GetClusterBuffer() const { return m_ClusterBuffer; }
        std::shared_ptr<RHI::RHIBuffer> GetLightGridBuffer() const { return m_LightGridBuffer; }
        std::shared_ptr<RHI::RHIBuffer> GetLightIndexListBuffer() const { return m_LightIndexListBuffer; }

    private:
        uint32_t m_GridSizeX = 0;
        uint32_t m_GridSizeY = 0;
        uint32_t m_GridSizeZ = NUM_Z_SLICES;
        uint32_t m_TotalClusters = 0;

        // The exact structures of GPU buffers mapped to the abstract RHIBuffer
        std::shared_ptr<RHI::RHIBuffer> m_LightBuffer;
        std::shared_ptr<RHI::RHIBuffer> m_ClusterBuffer;
        std::shared_ptr<RHI::RHIBuffer> m_LightGridBuffer;
        std::shared_ptr<RHI::RHIBuffer> m_LightIndexListBuffer;
    };

} // namespace Renderer
} // namespace Core
