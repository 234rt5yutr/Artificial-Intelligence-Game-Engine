#pragma once

#include "NavigationConfig.h"
#include <vector>
#include <memory>
#include <cstdint>
#include <glm/glm.hpp>

// NavMeshContext derives from rcContext and overrides rcLogCategory/rcTimerLabel
// methods, so the real Recast declarations are required here - a forward
// declaration cannot serve as a base class.
#include <Recast.h>

// Forward declarations for types only used behind pointers.
class dtNavMesh;

namespace Core {

namespace ECS {
    class Scene;
}

namespace Navigation {

    /// @brief Geometry input data for NavMesh generation
    struct NavMeshInputGeometry {
        std::vector<float> Vertices;        ///< Vertex positions (x, y, z triplets)
        std::vector<int32_t> Triangles;     ///< Triangle indices
        std::vector<uint8_t> AreaTypes;     ///< Area type per triangle
        glm::vec3 BoundsMin{0, 0, 0};       ///< Minimum bounds
        glm::vec3 BoundsMax{0, 0, 0};       ///< Maximum bounds
    };

    /// @brief Collider data for NavMesh input
    struct ColliderData {
        std::vector<glm::vec3> Vertices;
        std::vector<uint32_t> Indices;
        glm::mat4 Transform{1.0f};
        uint8_t AreaType = AREA_GROUND;
    };

    /// @brief Custom Recast context for logging
    class NavMeshContext : public rcContext {
    public:
        NavMeshContext(bool logEnabled = true);
        virtual ~NavMeshContext() = default;

    protected:
        virtual void doResetLog() override {}
        virtual void doLog(const rcLogCategory category, const char* msg, const int len) override;
        virtual void doResetTimers() override {}
        virtual void doStartTimer(const rcTimerLabel label) override;
        virtual void doStopTimer(const rcTimerLabel label) override;
        virtual int doGetAccumulatedTime(const rcTimerLabel label) const override;

    private:
        bool m_LogEnabled;
        std::vector<int64_t> m_TimerStarts;
        std::vector<int64_t> m_TimerAccum;
    };

    /// @brief Builds navigation meshes from collision geometry
    /// 
    /// NavMeshBuilder handles:
    /// - Geometry collection from scene colliders
    /// - Conversion to Recast input format
    /// - Heightfield voxelization
    /// - Region building and contour tracing
    /// - NavMesh polygon generation
    class NavMeshBuilder {
    public:
        NavMeshBuilder();
        ~NavMeshBuilder();

        // =====================================================================
        // Geometry Collection
        // =====================================================================

        /// @brief Collect geometry from scene colliders
        /// @param scene Scene to collect from
        /// @param areaFilter Optional area type filter
        /// @return Collected geometry
        NavMeshInputGeometry CollectGeometry(ECS::Scene* scene, 
                                              uint8_t areaFilter = 0xFF);

        /// @brief Collect geometry from collider data array
        /// @param colliders Array of collider data
        /// @return Collected geometry
        NavMeshInputGeometry CollectGeometry(const std::vector<ColliderData>& colliders);

        /// @brief Add geometry manually
        /// @param geometry Geometry to add
        void AddGeometry(const NavMeshInputGeometry& geometry);

        /// @brief Replace the collected geometry with raw Recast-format arrays
        /// @param vertices Vertex positions as x,y,z triplets
        /// @param triangles Triangle indices (3 per triangle)
        /// @param areaTypes Area type per triangle
        void SetGeometry(const std::vector<float>& vertices,
                         const std::vector<int32_t>& triangles,
                         const std::vector<uint8_t>& areaTypes);

        /// @brief Clear all collected geometry
        void ClearGeometry();

        // =====================================================================
        // NavMesh Building
        // =====================================================================

        /// @brief Store a build configuration for the argument-less Build()
        /// @param config Build configuration retained for later builds
        void SetConfig(const NavMeshConfig& config) { m_Config = config; }

        /// @brief Configuration most recently passed to SetConfig/Build
        const NavMeshConfig& GetConfig() const { return m_Config; }

        /// @brief Build NavMesh from collected geometry
        /// @param config Build configuration
        /// @return Build result
        NavMeshBuildResult Build(const NavMeshConfig& config);

        /// @brief Build using the configuration set by SetConfig()
        /// @return Build result
        NavMeshBuildResult Build() { return Build(m_Config); }

        /// @brief Build single tile at specified coordinates
        /// @param config Build configuration
        /// @param tileX Tile X coordinate
        /// @param tileZ Tile Z coordinate
        /// @return Build result
        NavMeshBuildResult BuildTile(const NavMeshConfig& config,
                                      int32_t tileX, int32_t tileZ);

        /// @brief Build a single tile using the configuration set by SetConfig()
        NavMeshBuildResult BuildTile(int32_t tileX, int32_t tileZ) {
            return BuildTile(m_Config, tileX, tileZ);
        }

        /// @brief Get the built NavMesh (transfers ownership)
        /// @return NavMesh pointer (caller takes ownership)
        dtNavMesh* GetNavMesh();

        /// @brief Check if NavMesh has been built
        bool HasNavMesh() const { return m_NavMesh != nullptr; }

        // =====================================================================
        // Utility
        // =====================================================================

        /// @brief Calculate tile coordinates for a world position
        void GetTileCoords(const NavMeshConfig& config,
                          const glm::vec3& position,
                          int32_t& outTileX, int32_t& outTileZ) const;

        /// @brief Get the bounds of the collected geometry
        void GetBounds(glm::vec3& outMin, glm::vec3& outMax) const;

        /// @brief Calculate number of tiles needed for geometry bounds
        void GetTileCount(const NavMeshConfig& config,
                         int32_t& outCountX, int32_t& outCountZ) const;

    private:
        /// @brief Build heightfield from geometry
        bool BuildHeightfield(const NavMeshConfig& config,
                             const glm::vec3& boundsMin,
                             const glm::vec3& boundsMax);

        /// @brief Build compact heightfield
        bool BuildCompactHeightfield(const NavMeshConfig& config);

        /// @brief Build regions
        bool BuildRegions(const NavMeshConfig& config);

        /// @brief Build contour set
        bool BuildContours(const NavMeshConfig& config);

        /// @brief Build polygon mesh
        bool BuildPolyMesh(const NavMeshConfig& config);

        /// @brief Build detail mesh
        bool BuildDetailMesh(const NavMeshConfig& config);

        /// @brief Create Detour NavMesh from poly mesh
        bool CreateDetourNavMesh(const NavMeshConfig& config);

        /// @brief Create tiled Detour NavMesh
        bool CreateTiledNavMesh(const NavMeshConfig& config,
                               int32_t tileCountX, int32_t tileCountZ);

        /// @brief Build single tile data
        unsigned char* BuildTileData(const NavMeshConfig& config,
                                     int32_t tileX, int32_t tileZ,
                                     const glm::vec3& tileBoundsMin,
                                     const glm::vec3& tileBoundsMax,
                                     int& dataSize);

        /// @brief Cleanup intermediate build data
        void CleanupIntermediateData();

    private:
        std::unique_ptr<NavMeshContext> m_Context;
        NavMeshInputGeometry m_InputGeometry;
        // Retained by SetConfig() so callers can use the argument-less Build().
        NavMeshConfig m_Config{};

        // Intermediate build data
        rcHeightfield* m_Heightfield = nullptr;
        rcCompactHeightfield* m_CompactHeightfield = nullptr;
        rcContourSet* m_ContourSet = nullptr;
        rcPolyMesh* m_PolyMesh = nullptr;
        rcPolyMeshDetail* m_DetailMesh = nullptr;

        // Result
        dtNavMesh* m_NavMesh = nullptr;
    };

} // namespace Navigation
} // namespace Core
