#pragma once

#include "Core/Renderer/Foliage/FoliageInstance.h"
#include "Core/ECS/Components/FoliageComponent.h"
#include "Core/RHI/RHIBuffer.h"
#include "Core/RHI/RHIDevice.h"
#include "Core/RHI/RHICommandList.h"
#include "Core/Math/Math.h"
#include <memory>
#include <vector>
#include <array>
#include <cstdint>

namespace Core {
namespace Renderer {

    //=========================================================================
    // Constants
    //=========================================================================
    
    constexpr uint32_t MAX_FOLIAGE_INSTANCES = 500000;
    constexpr uint32_t FOLIAGE_SCATTER_WORKGROUP_SIZE = 64;
    constexpr uint32_t MAX_FOLIAGE_TYPES = 32;

    //=========================================================================
    // Foliage Scatter Configuration
    //=========================================================================

    struct FoliageScatterConfig {
        bool EnableFrustumCulling = true;
        bool EnableDistanceCulling = true;
        bool EnableTerrainAlignment = true;
        bool EnableWindAnimation = true;
        float GlobalWindStrength = 1.0f;
        float GlobalDensityScale = 1.0f;
        uint32_t MaxInstancesPerType = 100000;
    };

    //=========================================================================
    // Foliage Statistics
    //=========================================================================

    struct FoliageStats {
        uint32_t TotalInstances = 0;
        uint32_t VisibleInstances = 0;
        uint32_t CulledInstances = 0;
        uint32_t ActiveFoliageTypes = 0;
        double ScatterTimeMs = 0.0;
        double RenderTimeMs = 0.0;

        void Reset() {
            TotalInstances = 0;
            VisibleInstances = 0;
            CulledInstances = 0;
            ActiveFoliageTypes = 0;
            ScatterTimeMs = 0.0;
            RenderTimeMs = 0.0;
        }
    };

    //=========================================================================
    // Foliage Scatter System
    //=========================================================================

    /**
     * @brief GPU-driven foliage scattering and instancing system
     * 
     * Uses compute shaders to:
     * 1. Scatter foliage instances based on terrain and placement rules
     * 2. Perform frustum culling
     * 3. Update wind animation parameters
     * 4. Generate indirect draw commands
     */
    class FoliageScatter {
    public:
        FoliageScatter();
        ~FoliageScatter();

        // Non-copyable
        FoliageScatter(const FoliageScatter&) = delete;
        FoliageScatter& operator=(const FoliageScatter&) = delete;

        //---------------------------------------------------------------------
        // Initialization
        //---------------------------------------------------------------------

        /**
         * @brief Initialize the foliage scatter system
         * @param device The RHI device for resource creation
         */
        void Initialize(std::shared_ptr<RHI::RHIDevice> device);

        /**
         * @brief Shutdown and release resources
         */
        void Shutdown();

        /**
         * @brief Check if system is initialized
         */
        bool IsInitialized() const { return m_Initialized; }

        //---------------------------------------------------------------------
        // Configuration
        //---------------------------------------------------------------------

        void SetConfig(const FoliageScatterConfig& config);
        const FoliageScatterConfig& GetConfig() const { return m_Config; }

        void SetGlobalWindStrength(float strength);
        void SetGlobalDensityScale(float scale);

        //---------------------------------------------------------------------
        // Terrain Data
        //---------------------------------------------------------------------

        /**
         * @brief Set the heightmap texture for terrain-based placement
         * @param heightmap Pointer to heightmap data (R32F format)
         * @param width Heightmap width in pixels
         * @param height Heightmap height in pixels
         * @param worldScale World units per pixel
         * @param heightOffset Base height offset
         */
        void SetHeightmap(
            const float* heightmap,
            uint32_t width,
            uint32_t height,
            float worldScale,
            float heightOffset = 0.0f
        );

        /**
         * @brief Set heightmap from RHI texture
         */
        void SetHeightmapTexture(std::shared_ptr<RHI::RHITexture> heightmapTexture);

        //---------------------------------------------------------------------
        // Foliage Registration
        //---------------------------------------------------------------------

        /**
         * @brief Register a foliage type for scattering
         * @param component The foliage component defining scatter parameters
         * @param worldTransform World transform for the scatter region
         * @return Index for the registered foliage type
         */
        uint32_t RegisterFoliage(
            const ECS::FoliageComponent& component,
            const Math::Mat4& worldTransform
        );

        /**
         * @brief Update a registered foliage type
         */
        void UpdateFoliage(
            uint32_t index,
            const ECS::FoliageComponent& component,
            const Math::Mat4& worldTransform
        );

        /**
         * @brief Remove a foliage type
         */
        void RemoveFoliage(uint32_t index);

        /**
         * @brief Clear all registered foliage
         */
        void ClearFoliage();

        /**
         * @brief Get current foliage type count
         */
        uint32_t GetFoliageTypeCount() const { return m_FoliageTypeCount; }

        //---------------------------------------------------------------------
        // Camera Update
        //---------------------------------------------------------------------

        /**
         * @brief Update camera data for culling and LOD
         * @param viewProjection Combined view-projection matrix
         * @param cameraPosition Camera world position
         */
        void UpdateCamera(
            const Math::Mat4& viewProjection,
            const Math::Vec3& cameraPosition
        );

        /**
         * @brief Extract frustum planes from view-projection matrix
         */
        void ExtractFrustumPlanes(const Math::Mat4& viewProjection);

        //---------------------------------------------------------------------
        // Wind Animation
        //---------------------------------------------------------------------

        /**
         * @brief Update wind parameters
         * @param direction Wind direction (normalized)
         * @param strength Wind strength
         * @param time Current time for animation
         */
        void UpdateWind(
            const Math::Vec3& direction,
            float strength,
            float time
        );

        //---------------------------------------------------------------------
        // Scatter Execution
        //---------------------------------------------------------------------

        /**
         * @brief Execute GPU foliage scattering
         * 
         * Dispatches compute shader to:
         * 1. Generate instances based on density/placement rules
         * 2. Apply terrain alignment
         * 3. Perform frustum culling
         * 4. Write to instance SSBO
         * 
         * @param commandList Command list for dispatch
         */
        void ScatterFoliage(std::shared_ptr<RHI::RHICommandList> commandList);

        /**
         * @brief Update wind animation in instance data
         * @param commandList Command list for dispatch
         * @param deltaTime Time since last update
         */
        void UpdateWindAnimation(
            std::shared_ptr<RHI::RHICommandList> commandList,
            float deltaTime
        );

        //---------------------------------------------------------------------
        // Buffer Access (for rendering)
        //---------------------------------------------------------------------

        /**
         * @brief Get instance buffer for vertex shader
         */
        std::shared_ptr<RHI::RHIBuffer> GetInstanceBuffer() const { return m_InstanceBuffer; }

        /**
         * @brief Get indirect draw command buffer
         */
        std::shared_ptr<RHI::RHIBuffer> GetDrawCommandBuffer() const { return m_DrawCommandBuffer; }

        /**
         * @brief Get draw count buffer (for indirect count)
         */
        std::shared_ptr<RHI::RHIBuffer> GetDrawCountBuffer() const { return m_DrawCountBuffer; }

        /**
         * @brief Read back current draw count (blocking)
         */
        uint32_t ReadbackDrawCount();

        /**
         * @brief Get current visible instance count (cached)
         */
        uint32_t GetVisibleInstanceCount() const { return m_VisibleInstanceCount; }

        //---------------------------------------------------------------------
        // Statistics
        //---------------------------------------------------------------------

        const FoliageStats& GetStats() const { return m_Stats; }

        //---------------------------------------------------------------------
        // Debug
        //---------------------------------------------------------------------

        /**
         * @brief Get frustum planes for debug visualization
         */
        const std::array<Math::Vec4, 6>& GetFrustumPlanes() const { return m_FrustumPlanes; }

    private:
        void CreateBuffers();
        void CreateComputePipelines();
        void UpdateUniformBuffer();
        void ResetDrawCount();
        Math::Vec4 NormalizePlane(const Math::Vec4& plane);

        // Per-foliage-type data for scatter
        struct FoliageTypeData {
            ECS::FoliageComponent Component;
            Math::Mat4 WorldTransform;
            uint32_t InstanceOffset;
            uint32_t InstanceCount;
            bool Active;
            bool NeedsUpdate;
        };

    private:
        std::shared_ptr<RHI::RHIDevice> m_Device;
        FoliageScatterConfig m_Config;
        FoliageStats m_Stats;
        bool m_Initialized = false;

        // Foliage type data
        std::vector<FoliageTypeData> m_FoliageTypes;
        uint32_t m_FoliageTypeCount = 0;

        // GPU Buffers
        std::shared_ptr<RHI::RHIBuffer> m_UniformBuffer;
        std::shared_ptr<RHI::RHIBuffer> m_WindUniformBuffer;
        std::shared_ptr<RHI::RHIBuffer> m_InstanceBuffer;
        std::shared_ptr<RHI::RHIBuffer> m_DrawCommandBuffer;
        std::shared_ptr<RHI::RHIBuffer> m_DrawCountBuffer;
        std::shared_ptr<RHI::RHIBuffer> m_StagingBuffer;

        // Heightmap
        std::shared_ptr<RHI::RHIBuffer> m_HeightmapBuffer;
        std::shared_ptr<RHI::RHITexture> m_HeightmapTexture;
        uint32_t m_HeightmapWidth = 0;
        uint32_t m_HeightmapHeight = 0;
        float m_HeightmapScale = 1.0f;
        float m_HeightmapOffset = 0.0f;

        // Camera and culling data
        FoliageScatterUniforms m_ScatterUniforms;
        FoliageWindUniforms m_WindUniforms;
        std::array<Math::Vec4, 6> m_FrustumPlanes;
        Math::Vec3 m_CameraPosition;
        float m_CurrentTime = 0.0f;

        // Cached state
        uint32_t m_VisibleInstanceCount = 0;
        bool m_NeedsRescatter = true;
    };

} // namespace Renderer
} // namespace Core
