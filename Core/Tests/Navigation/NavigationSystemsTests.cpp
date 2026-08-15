// NavigationSystemsTests: Unit tests for Recast / Detour NavMesh generation,
// NavMeshManager pathfinding queries, and CrowdManager agent simulations.

#include "Core/Navigation/NavigationConfig.h"
#include "Core/Navigation/NavMeshBuilder.h"
#include "Core/Navigation/NavMeshManager.h"
#include "Core/Navigation/CrowdManager.h"
#include "Core/Log.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <glm/glm.hpp>

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n",                 \
                         #expr, __FILE__, __LINE__);                           \
            std::abort();                                                      \
        }                                                                      \
    } while (false)

int main() {
    using namespace Core::Navigation;

    Engine::Log::Init();

    // 1. Initialize NavMeshManager
    NavMeshConfig config = NavMeshConfig::Default();
    config.CellSize = 0.2f;
    config.CellHeight = 0.1f;
    config.AgentHeight = 2.0f;
    config.AgentRadius = 0.5f;
    config.AgentMaxClimb = 0.4f;
    config.AgentMaxSlope = 45.0f;

    auto& navManager = NavMeshManager::Get();
    CHECK(navManager.Initialize(config));
    CHECK(navManager.IsInitialized());

    // 2. Build NavMesh from a 40x40 meter walkable ground plane
    {
        // 2 quads / 4 triangles forming a flat ground plane from (-20, 0, -20) to (20, 0, 20)
        ColliderData ground;
        ground.Transform = glm::mat4(1.0f);
        ground.AreaType = AREA_GROUND;
        ground.Vertices = {
            glm::vec3(-20.0f, 0.0f, -20.0f),
            glm::vec3( 20.0f, 0.0f, -20.0f),
            glm::vec3( 20.0f, 0.0f,  20.0f),
            glm::vec3(-20.0f, 0.0f,  20.0f)
        };
        // 2 triangles with CCW winding facing upward (+Y): (0, 2, 1) and (0, 3, 2)
        ground.Indices = { 0, 2, 1, 0, 3, 2 };

        std::vector<ColliderData> colliders = { ground };
        NavMeshBuildResult buildRes = navManager.BuildFromColliders(colliders);
        CHECK(buildRes.Success);
        CHECK(navManager.HasNavMeshData());
        CHECK(buildRes.PolygonCount > 0);
    }

    // 3. Pathfinding Query Execution
    {
        glm::vec3 startPos(-10.0f, 0.0f, -10.0f);
        glm::vec3 targetPos(10.0f, 0.0f, 10.0f);

        PathQueryResult pathResult = navManager.FindPath(startPos, targetPos);
        CHECK(pathResult.Found);
        CHECK(!pathResult.Path.empty());
        CHECK(pathResult.PathLength > 0.0f);

        // Verify start and end points are near queried coordinates
        const glm::vec3& startWaypt = pathResult.Path.front();
        const glm::vec3& endWaypt = pathResult.Path.back();
        CHECK(std::fabs(startWaypt.x - startPos.x) < 1.0f);
        CHECK(std::fabs(startWaypt.z - startPos.z) < 1.0f);
        CHECK(std::fabs(endWaypt.x - targetPos.x) < 1.0f);
        CHECK(std::fabs(endWaypt.z - targetPos.z) < 1.0f);
    }

    // 4. Nearest Point and Raycast Queries
    {
        glm::vec3 testPoint(5.0f, 2.0f, 5.0f); // Elevated in Y within vertical search extents (4m)
        glm::vec3 nearest = navManager.FindNearestPoint(testPoint);
        // The nearest walkable surface point should have Y close to 0
        CHECK(std::fabs(nearest.y - 0.0f) < 0.5f);
        CHECK(std::fabs(nearest.x - 5.0f) < 1.0f);
        CHECK(std::fabs(nearest.z - 5.0f) < 1.0f);

        // Raycast across flat surface
        glm::vec3 hitPoint{0.0f};
        glm::vec3 hitNormal{0.0f};
        bool hit = navManager.Raycast(glm::vec3(-5.0f, 0.0f, 0.0f), glm::vec3(5.0f, 0.0f, 0.0f), hitPoint, hitNormal);
        // Clear unobstructed path across ground does not hit an obstacle boundary
        (void)hit;
    }

    // 5. Crowd Simulation with DetourCrowd Agents
    {
        CrowdManager crowdManager;
        CrowdConfig crowdConfig;
        crowdConfig.MaxAgents = 16;
        crowdConfig.MaxAgentRadius = 0.6f;

        CHECK(crowdManager.Initialize(navManager.GetNavMesh(), crowdConfig));
        CHECK(crowdManager.IsInitialized());

        CrowdAgentConfig agentConfig;
        agentConfig.Radius = 0.5f;
        agentConfig.Height = 2.0f;
        agentConfig.MaxSpeed = 4.0f;
        agentConfig.MaxAcceleration = 8.0f;

        // Add two agents
        AgentResult a1 = crowdManager.AddAgent(glm::vec3(-8.0f, 0.0f, 0.0f), agentConfig);
        CHECK(a1.Success);
        CHECK(a1.AgentIndex >= 0);

        AgentResult a2 = crowdManager.AddAgent(glm::vec3(8.0f, 0.0f, 0.0f), agentConfig);
        CHECK(a2.Success);
        CHECK(a2.AgentIndex >= 0);

        // Command agent 1 to move right and agent 2 to move left
        CHECK(crowdManager.SetAgentTarget(a1.AgentIndex, glm::vec3(8.0f, 0.0f, 0.0f)));
        CHECK(crowdManager.SetAgentTarget(a2.AgentIndex, glm::vec3(-8.0f, 0.0f, 0.0f)));

        // Simulate 30 frames of 1/30s (1.0 second total)
        for (int i = 0; i < 30; ++i) {
            crowdManager.Update(0.033f);
        }

        // Verify agents moved from initial positions
        glm::vec3 p1 = crowdManager.GetAgentPosition(a1.AgentIndex);
        glm::vec3 p2 = crowdManager.GetAgentPosition(a2.AgentIndex);

        CHECK(p1.x > -8.0f); // Moving in +x direction
        CHECK(p2.x < 8.0f);  // Moving in -x direction

        crowdManager.RemoveAgent(a1.AgentIndex);
        crowdManager.RemoveAgent(a2.AgentIndex);
        crowdManager.Shutdown();
    }

    // 6. Shutdown
    navManager.Shutdown();
    CHECK(!navManager.IsInitialized());

    return 0;
}
