#pragma once

#include "NavigationConfig.h"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

// Forward declarations
struct dtNavMesh;
struct dtNavMeshQuery;
struct dtCrowd;

namespace Core {
namespace Navigation {

    /// @brief Debug visualization for NavMesh system
    /// 
    /// Provides debug rendering data for:
    /// - NavMesh polygons and edges
    /// - Agent paths
    /// - Off-mesh connections
    /// - Crowd agents
    class NavMeshDebugDraw {
    public:
        /// @brief Debug vertex with position and color
        struct DebugVertex {
            glm::vec3 Position;
            glm::vec4 Color;
        };

        /// @brief Debug draw flags
        enum DrawFlags : uint32_t {
            DRAW_POLYGONS       = 1 << 0,  ///< Draw NavMesh polygons
            DRAW_MESH_BOUNDS    = 1 << 1,  ///< Draw tile boundaries
            DRAW_NEIGHBORS      = 1 << 2,  ///< Draw polygon neighbors
            DRAW_PORTALS        = 1 << 3,  ///< Draw tile portals
            DRAW_OFF_MESH       = 1 << 4,  ///< Draw off-mesh connections
            DRAW_DETAIL         = 1 << 5,  ///< Draw detail mesh
            DRAW_AGENTS         = 1 << 6,  ///< Draw crowd agents
            DRAW_PATHS          = 1 << 7,  ///< Draw agent paths
            DRAW_CORRIDORS      = 1 << 8,  ///< Draw agent corridors

            DRAW_ALL = 0xFFFFFFFF
        };

        /// @brief Area type colors
        struct AreaColors {
            glm::vec4 Ground   = { 0.2f, 0.7f, 0.3f, 0.6f };
            glm::vec4 Water    = { 0.2f, 0.4f, 0.8f, 0.6f };
            glm::vec4 Road     = { 0.5f, 0.5f, 0.5f, 0.6f };
            glm::vec4 Grass    = { 0.4f, 0.8f, 0.3f, 0.6f };
            glm::vec4 Door     = { 0.8f, 0.6f, 0.2f, 0.6f };
            glm::vec4 Jump     = { 0.8f, 0.2f, 0.8f, 0.6f };
            glm::vec4 Disabled = { 0.3f, 0.3f, 0.3f, 0.3f };
        };

    public:
        NavMeshDebugDraw();
        ~NavMeshDebugDraw() = default;

        /// @brief Clear all debug data
        void Clear();

        /// @brief Draw NavMesh
        /// @param navMesh NavMesh to draw
        /// @param flags Draw flags
        void DrawNavMesh(const dtNavMesh* navMesh, uint32_t flags = DRAW_POLYGONS);

        /// @brief Draw crowd agents
        /// @param crowd Crowd to draw
        /// @param flags Draw flags
        void DrawCrowd(const dtCrowd* crowd, uint32_t flags = DRAW_AGENTS);

        /// @brief Draw a path
        /// @param waypoints Path waypoints
        /// @param color Path color
        void DrawPath(const std::vector<glm::vec3>& waypoints, 
                      const glm::vec4& color = { 1.0f, 0.5f, 0.0f, 1.0f });

        /// @brief Draw an agent position
        /// @param position Agent position
        /// @param radius Agent radius
        /// @param height Agent height
        /// @param color Agent color
        void DrawAgent(const glm::vec3& position, float radius, float height,
                       const glm::vec4& color = { 0.0f, 0.7f, 1.0f, 0.8f });

        /// @brief Draw a sphere
        /// @param center Sphere center
        /// @param radius Sphere radius
        /// @param color Sphere color
        void DrawSphere(const glm::vec3& center, float radius,
                        const glm::vec4& color = { 1.0f, 1.0f, 0.0f, 0.5f });

        /// @brief Draw a cylinder
        /// @param base Base center
        /// @param radius Cylinder radius
        /// @param height Cylinder height
        /// @param color Cylinder color
        void DrawCylinder(const glm::vec3& base, float radius, float height,
                          const glm::vec4& color = { 0.0f, 1.0f, 0.0f, 0.5f });

        /// @brief Draw off-mesh connection
        /// @param start Start position
        /// @param end End position
        /// @param radius Connection radius
        /// @param bidirectional Whether bidirectional
        void DrawOffMeshConnection(const glm::vec3& start, const glm::vec3& end,
                                    float radius, bool bidirectional);

        // =====================================================================
        // Accessors for rendering
        // =====================================================================

        /// @brief Get triangle vertices (for filled rendering)
        const std::vector<DebugVertex>& GetTriangleVertices() const { return m_TriangleVertices; }

        /// @brief Get line vertices (for wireframe rendering)
        const std::vector<DebugVertex>& GetLineVertices() const { return m_LineVertices; }

        /// @brief Get point vertices (for point rendering)
        const std::vector<DebugVertex>& GetPointVertices() const { return m_PointVertices; }

        /// @brief Check if there's any data to draw
        bool HasData() const;

        // =====================================================================
        // Configuration
        // =====================================================================

        /// @brief Set area colors
        /// @param colors Area color configuration
        void SetAreaColors(const AreaColors& colors) { m_AreaColors = colors; }

        /// @brief Get area colors
        const AreaColors& GetAreaColors() const { return m_AreaColors; }

        /// @brief Set edge color
        /// @param color Edge color
        void SetEdgeColor(const glm::vec4& color) { m_EdgeColor = color; }

    private:
        /// @brief Get color for area type
        glm::vec4 GetAreaColor(uint8_t areaType) const;

        /// @brief Draw a triangle
        void AddTriangle(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                         const glm::vec4& color);

        /// @brief Draw a line
        void AddLine(const glm::vec3& v0, const glm::vec3& v1, const glm::vec4& color);

        /// @brief Draw a point
        void AddPoint(const glm::vec3& position, const glm::vec4& color);

        /// @brief Draw polygon edges
        void DrawPolygonEdges(const glm::vec3* verts, int numVerts, const glm::vec4& color);

        /// @brief Draw a circle approximation
        void DrawCircle(const glm::vec3& center, float radius, const glm::vec4& color, int segments = 16);

        /// @brief Draw an arrow
        void DrawArrow(const glm::vec3& start, const glm::vec3& end, float headSize, const glm::vec4& color);

    private:
        std::vector<DebugVertex> m_TriangleVertices;
        std::vector<DebugVertex> m_LineVertices;
        std::vector<DebugVertex> m_PointVertices;

        AreaColors m_AreaColors;
        glm::vec4 m_EdgeColor = { 0.1f, 0.1f, 0.1f, 0.8f };
    };

} // namespace Navigation
} // namespace Core
