#include "NavMeshDebugDraw.h"
#include <DetourNavMesh.h>
#include <DetourCrowd.h>
#include <cmath>

namespace Core {
namespace Navigation {

    NavMeshDebugDraw::NavMeshDebugDraw() {
    }

    void NavMeshDebugDraw::Clear() {
        m_TriangleVertices.clear();
        m_LineVertices.clear();
        m_PointVertices.clear();
    }

    bool NavMeshDebugDraw::HasData() const {
        return !m_TriangleVertices.empty() || !m_LineVertices.empty() || !m_PointVertices.empty();
    }

    glm::vec4 NavMeshDebugDraw::GetAreaColor(uint8_t areaType) const {
        switch (areaType) {
            case AREA_GROUND:   return m_AreaColors.Ground;
            case AREA_WATER:    return m_AreaColors.Water;
            case AREA_ROAD:     return m_AreaColors.Road;
            case AREA_GRASS:    return m_AreaColors.Grass;
            case AREA_DOOR:     return m_AreaColors.Door;
            case AREA_JUMP:     return m_AreaColors.Jump;
            case AREA_DISABLED: return m_AreaColors.Disabled;
            default:            return m_AreaColors.Ground;
        }
    }

    void NavMeshDebugDraw::AddTriangle(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                                        const glm::vec4& color) {
        m_TriangleVertices.push_back({ v0, color });
        m_TriangleVertices.push_back({ v1, color });
        m_TriangleVertices.push_back({ v2, color });
    }

    void NavMeshDebugDraw::AddLine(const glm::vec3& v0, const glm::vec3& v1, const glm::vec4& color) {
        m_LineVertices.push_back({ v0, color });
        m_LineVertices.push_back({ v1, color });
    }

    void NavMeshDebugDraw::AddPoint(const glm::vec3& position, const glm::vec4& color) {
        m_PointVertices.push_back({ position, color });
    }

    void NavMeshDebugDraw::DrawPolygonEdges(const glm::vec3* verts, int numVerts, const glm::vec4& color) {
        for (int i = 0; i < numVerts; ++i) {
            int j = (i + 1) % numVerts;
            AddLine(verts[i], verts[j], color);
        }
    }

    void NavMeshDebugDraw::DrawCircle(const glm::vec3& center, float radius, const glm::vec4& color, int segments) {
        for (int i = 0; i < segments; ++i) {
            float a0 = static_cast<float>(i) / segments * 2.0f * 3.14159f;
            float a1 = static_cast<float>(i + 1) / segments * 2.0f * 3.14159f;

            glm::vec3 p0 = center + glm::vec3(std::cos(a0) * radius, 0.0f, std::sin(a0) * radius);
            glm::vec3 p1 = center + glm::vec3(std::cos(a1) * radius, 0.0f, std::sin(a1) * radius);

            AddLine(p0, p1, color);
        }
    }

    void NavMeshDebugDraw::DrawArrow(const glm::vec3& start, const glm::vec3& end, float headSize, const glm::vec4& color) {
        AddLine(start, end, color);

        // Arrow head
        glm::vec3 dir = glm::normalize(end - start);
        glm::vec3 up(0, 1, 0);
        glm::vec3 right = glm::normalize(glm::cross(dir, up));
        if (glm::length(right) < 0.001f) {
            right = glm::vec3(1, 0, 0);
        }

        glm::vec3 p1 = end - dir * headSize + right * headSize * 0.5f;
        glm::vec3 p2 = end - dir * headSize - right * headSize * 0.5f;

        AddLine(end, p1, color);
        AddLine(end, p2, color);
    }

    void NavMeshDebugDraw::DrawNavMesh(const dtNavMesh* navMesh, uint32_t flags) {
        if (!navMesh) return;

        // Iterate through all tiles
        for (int i = 0; i < navMesh->getMaxTiles(); ++i) {
            const dtMeshTile* tile = navMesh->getTile(i);
            if (!tile || !tile->header) continue;

            const float* verts = tile->verts;
            const dtPoly* polys = tile->polys;

            // Draw polygons
            if (flags & DRAW_POLYGONS) {
                for (int j = 0; j < tile->header->polyCount; ++j) {
                    const dtPoly& poly = polys[j];
                    if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) continue;

                    glm::vec4 polyColor = GetAreaColor(poly.getArea());

                    // Get polygon vertices
                    std::vector<glm::vec3> polyVerts(poly.vertCount);
                    for (int k = 0; k < poly.vertCount; ++k) {
                        const float* v = &verts[poly.verts[k] * 3];
                        polyVerts[k] = glm::vec3(v[0], v[1] + 0.01f, v[2]); // Slight offset to prevent z-fighting
                    }

                    // Triangulate polygon (simple fan)
                    for (int k = 2; k < poly.vertCount; ++k) {
                        AddTriangle(polyVerts[0], polyVerts[k - 1], polyVerts[k], polyColor);
                    }

                    // Draw edges
                    DrawPolygonEdges(polyVerts.data(), poly.vertCount, m_EdgeColor);
                }
            }

            // Draw tile bounds
            if (flags & DRAW_MESH_BOUNDS) {
                glm::vec4 boundsColor(1.0f, 0.0f, 0.0f, 0.8f);
                glm::vec3 bmin(tile->header->bmin[0], tile->header->bmin[1], tile->header->bmin[2]);
                glm::vec3 bmax(tile->header->bmax[0], tile->header->bmax[1], tile->header->bmax[2]);

                // Draw bounding box edges
                AddLine({ bmin.x, bmin.y, bmin.z }, { bmax.x, bmin.y, bmin.z }, boundsColor);
                AddLine({ bmax.x, bmin.y, bmin.z }, { bmax.x, bmin.y, bmax.z }, boundsColor);
                AddLine({ bmax.x, bmin.y, bmax.z }, { bmin.x, bmin.y, bmax.z }, boundsColor);
                AddLine({ bmin.x, bmin.y, bmax.z }, { bmin.x, bmin.y, bmin.z }, boundsColor);

                AddLine({ bmin.x, bmax.y, bmin.z }, { bmax.x, bmax.y, bmin.z }, boundsColor);
                AddLine({ bmax.x, bmax.y, bmin.z }, { bmax.x, bmax.y, bmax.z }, boundsColor);
                AddLine({ bmax.x, bmax.y, bmax.z }, { bmin.x, bmax.y, bmax.z }, boundsColor);
                AddLine({ bmin.x, bmax.y, bmax.z }, { bmin.x, bmax.y, bmin.z }, boundsColor);

                AddLine({ bmin.x, bmin.y, bmin.z }, { bmin.x, bmax.y, bmin.z }, boundsColor);
                AddLine({ bmax.x, bmin.y, bmin.z }, { bmax.x, bmax.y, bmin.z }, boundsColor);
                AddLine({ bmax.x, bmin.y, bmax.z }, { bmax.x, bmax.y, bmax.z }, boundsColor);
                AddLine({ bmin.x, bmin.y, bmax.z }, { bmin.x, bmax.y, bmax.z }, boundsColor);
            }

            // Draw off-mesh connections
            if (flags & DRAW_OFF_MESH) {
                for (int j = 0; j < tile->header->polyCount; ++j) {
                    const dtPoly& poly = polys[j];
                    if (poly.getType() != DT_POLYTYPE_OFFMESH_CONNECTION) continue;

                    const dtOffMeshConnection* con = &tile->offMeshCons[j - tile->header->offMeshBase];
                    if (!con) continue;

                    glm::vec3 start(con->pos[0], con->pos[1], con->pos[2]);
                    glm::vec3 end(con->pos[3], con->pos[4], con->pos[5]);

                    DrawOffMeshConnection(start, end, con->rad, 
                                          (con->flags & DT_OFFMESH_CON_BIDIR) != 0);
                }
            }
        }
    }

    void NavMeshDebugDraw::DrawCrowd(const dtCrowd* crowd, uint32_t flags) {
        if (!crowd) return;

        if (flags & DRAW_AGENTS) {
            int agentCount = crowd->getAgentCount();
            for (int i = 0; i < agentCount; ++i) {
                const dtCrowdAgent* agent = crowd->getAgent(i);
                if (!agent->active) continue;

                glm::vec3 pos(agent->npos[0], agent->npos[1], agent->npos[2]);
                float radius = agent->params.radius;
                float height = agent->params.height;

                // Color based on agent state
                glm::vec4 color;
                switch (agent->state) {
                    case DT_CROWDAGENT_STATE_WALKING:
                        color = { 0.0f, 1.0f, 0.0f, 0.8f };
                        break;
                    case DT_CROWDAGENT_STATE_OFFMESH:
                        color = { 0.8f, 0.0f, 0.8f, 0.8f };
                        break;
                    default:
                        color = { 0.5f, 0.5f, 0.5f, 0.8f };
                        break;
                }

                DrawAgent(pos, radius, height, color);

                // Draw velocity
                if (flags & DRAW_PATHS) {
                    glm::vec3 vel(agent->vel[0], agent->vel[1], agent->vel[2]);
                    if (glm::length(vel) > 0.01f) {
                        DrawArrow(pos, pos + vel, 0.2f, { 1.0f, 0.5f, 0.0f, 1.0f });
                    }
                }

                // Draw target
                if (flags & DRAW_PATHS && agent->targetState == DT_CROWDAGENT_TARGET_VALID) {
                    glm::vec3 target(agent->targetPos[0], agent->targetPos[1], agent->targetPos[2]);
                    AddLine(pos, target, { 1.0f, 1.0f, 0.0f, 0.5f });
                    DrawCircle(target, 0.3f, { 1.0f, 1.0f, 0.0f, 0.8f });
                }

                // Draw corridor
                if (flags & DRAW_CORRIDORS) {
                    const dtPathCorridor& corridor = agent->corridor;
                    int ncorners = agent->ncorners;
                    const float* corners = agent->cornerVerts;

                    glm::vec4 corridorColor(0.0f, 0.5f, 1.0f, 0.5f);
                    for (int c = 0; c < ncorners - 1; ++c) {
                        glm::vec3 c0(corners[c * 3], corners[c * 3 + 1] + 0.1f, corners[c * 3 + 2]);
                        glm::vec3 c1(corners[(c + 1) * 3], corners[(c + 1) * 3 + 1] + 0.1f, corners[(c + 1) * 3 + 2]);
                        AddLine(c0, c1, corridorColor);
                    }
                }
            }
        }
    }

    void NavMeshDebugDraw::DrawPath(const std::vector<glm::vec3>& waypoints, const glm::vec4& color) {
        if (waypoints.size() < 2) return;

        for (size_t i = 0; i < waypoints.size() - 1; ++i) {
            glm::vec3 p0 = waypoints[i];
            glm::vec3 p1 = waypoints[i + 1];
            p0.y += 0.1f; // Slight offset
            p1.y += 0.1f;

            AddLine(p0, p1, color);
        }

        // Draw points at waypoints
        for (const auto& wp : waypoints) {
            AddPoint({ wp.x, wp.y + 0.1f, wp.z }, color);
        }
    }

    void NavMeshDebugDraw::DrawAgent(const glm::vec3& position, float radius, float height,
                                      const glm::vec4& color) {
        DrawCylinder(position, radius, height, color);
        DrawCircle(position, radius, color);
    }

    void NavMeshDebugDraw::DrawSphere(const glm::vec3& center, float radius, const glm::vec4& color) {
        // Draw approximation using circles
        DrawCircle(center, radius, color);
        
        // XZ plane
        for (int i = 0; i < 8; ++i) {
            float a0 = static_cast<float>(i) / 8 * 2.0f * 3.14159f;
            float a1 = static_cast<float>(i + 1) / 8 * 2.0f * 3.14159f;

            glm::vec3 p0 = center + glm::vec3(std::cos(a0) * radius, 0.0f, std::sin(a0) * radius);
            glm::vec3 p1 = center + glm::vec3(std::cos(a1) * radius, 0.0f, std::sin(a1) * radius);
            AddLine(p0, p1, color);

            // XY plane
            p0 = center + glm::vec3(std::cos(a0) * radius, std::sin(a0) * radius, 0.0f);
            p1 = center + glm::vec3(std::cos(a1) * radius, std::sin(a1) * radius, 0.0f);
            AddLine(p0, p1, color);

            // YZ plane
            p0 = center + glm::vec3(0.0f, std::cos(a0) * radius, std::sin(a0) * radius);
            p1 = center + glm::vec3(0.0f, std::cos(a1) * radius, std::sin(a1) * radius);
            AddLine(p0, p1, color);
        }
    }

    void NavMeshDebugDraw::DrawCylinder(const glm::vec3& base, float radius, float height, const glm::vec4& color) {
        glm::vec3 top = base + glm::vec3(0, height, 0);

        // Draw circles at top and bottom
        DrawCircle(base, radius, color);
        DrawCircle(top, radius, color);

        // Draw vertical lines
        for (int i = 0; i < 4; ++i) {
            float a = static_cast<float>(i) / 4 * 2.0f * 3.14159f;
            glm::vec3 p0 = base + glm::vec3(std::cos(a) * radius, 0.0f, std::sin(a) * radius);
            glm::vec3 p1 = top + glm::vec3(std::cos(a) * radius, 0.0f, std::sin(a) * radius);
            AddLine(p0, p1, color);
        }
    }

    void NavMeshDebugDraw::DrawOffMeshConnection(const glm::vec3& start, const glm::vec3& end,
                                                  float radius, bool bidirectional) {
        glm::vec4 color(0.8f, 0.2f, 0.8f, 0.8f);

        // Draw connection line
        if (bidirectional) {
            AddLine(start, end, color);
        } else {
            DrawArrow(start, end, 0.3f, color);
        }

        // Draw connection points
        DrawCircle(start, radius, color);
        DrawCircle(end, radius, color);

        // Draw vertical bars at endpoints
        AddLine(start, start + glm::vec3(0, 1.0f, 0), color);
        AddLine(end, end + glm::vec3(0, 1.0f, 0), color);
    }

} // namespace Navigation
} // namespace Core
