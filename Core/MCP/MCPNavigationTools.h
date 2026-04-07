#pragma once

#include "MCPTool.h"
#include "MCPTypes.h"
#include "../Navigation/NavigationSystem.h"
#include "../Navigation/NavMeshManager.h"
#include "../ECS/Scene.h"
#include "../ECS/Components/Components.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace Core {
namespace MCP {

    using json = nlohmann::json;

    /// ========================================================================
    /// RebuildNavMesh Tool
    /// ========================================================================
    class RebuildNavMeshTool : public MCPTool {
    public:
        std::string GetName() const override { return "RebuildNavMesh"; }

        std::string GetDescription() const override {
            return "Rebuild the navigation mesh from the current scene geometry. "
                   "Can optionally specify configuration parameters.";
        }

        json GetInputSchema() const override {
            return {
                {"type", "object"},
                {"properties", {
                    {"cellSize", {
                        {"type", "number"},
                        {"description", "Cell size for voxelization (default: 0.3)"}
                    }},
                    {"cellHeight", {
                        {"type", "number"},
                        {"description", "Cell height for voxelization (default: 0.2)"}
                    }},
                    {"agentRadius", {
                        {"type", "number"},
                        {"description", "Agent radius (default: 0.5)"}
                    }},
                    {"agentHeight", {
                        {"type", "number"},
                        {"description", "Agent height (default: 2.0)"}
                    }},
                    {"agentMaxClimb", {
                        {"type", "number"},
                        {"description", "Maximum step height (default: 0.9)"}
                    }},
                    {"agentMaxSlope", {
                        {"type", "number"},
                        {"description", "Maximum slope angle in degrees (default: 45)"}
                    }}
                }},
                {"required", json::array()}
            };
        }

        MCPResult Execute(const json& params, ECS::Scene* scene) override {
            if (!scene) {
                return MCPResult::Error("No scene provided");
            }

            // Build config from params
            Navigation::NavMeshConfig config;

            if (params.contains("cellSize")) {
                config.CellSize = params["cellSize"].get<float>();
            }
            if (params.contains("cellHeight")) {
                config.CellHeight = params["cellHeight"].get<float>();
            }
            if (params.contains("agentRadius")) {
                config.AgentRadius = params["agentRadius"].get<float>();
            }
            if (params.contains("agentHeight")) {
                config.AgentHeight = params["agentHeight"].get<float>();
            }
            if (params.contains("agentMaxClimb")) {
                config.AgentMaxClimb = params["agentMaxClimb"].get<float>();
            }
            if (params.contains("agentMaxSlope")) {
                config.AgentMaxSlope = params["agentMaxSlope"].get<float>();
            }

            // Set config and build
            auto& navSystem = Navigation::NavigationSystem::Get();
            if (!navSystem.IsInitialized()) {
                if (!navSystem.Initialize(config)) {
                    return MCPResult::Error("Failed to initialize navigation system");
                }
            }

            auto result = navSystem.BuildNavMesh(scene);

            if (!result.success) {
                return MCPResult::Error("NavMesh build failed: " + result.errorMessage);
            }

            json resultData = {
                {"success", true},
                {"stats", {
                    {"polyCount", result.stats.polyCount},
                    {"vertexCount", result.stats.vertexCount},
                    {"tileCount", result.stats.tileCount},
                    {"memoryUsageBytes", result.stats.memoryUsageBytes}
                }},
                {"buildTimeMs", result.buildTimeMs}
            };

            return MCPResult::Success(resultData);
        }
    };

    /// ========================================================================
    /// CommandAgentMove Tool
    /// ========================================================================
    class CommandAgentMoveTool : public MCPTool {
    public:
        std::string GetName() const override { return "CommandAgentMove"; }

        std::string GetDescription() const override {
            return "Command a navigation agent to move to a target position. "
                   "The agent will pathfind and navigate to the destination.";
        }

        json GetInputSchema() const override {
            return {
                {"type", "object"},
                {"properties", {
                    {"entityId", {
                        {"type", "integer"},
                        {"description", "Entity ID of the agent to command"}
                    }},
                    {"entityName", {
                        {"type", "string"},
                        {"description", "Entity name of the agent to command (alternative to entityId)"}
                    }},
                    {"targetX", {
                        {"type", "number"},
                        {"description", "Target X position"}
                    }},
                    {"targetY", {
                        {"type", "number"},
                        {"description", "Target Y position"}
                    }},
                    {"targetZ", {
                        {"type", "number"},
                        {"description", "Target Z position"}
                    }},
                    {"maxSpeed", {
                        {"type", "number"},
                        {"description", "Optional maximum speed override"}
                    }}
                }},
                {"required", {"targetX", "targetY", "targetZ"}}
            };
        }

        MCPResult Execute(const json& params, ECS::Scene* scene) override {
            if (!scene) {
                return MCPResult::Error("No scene provided");
            }

            auto& registry = scene->GetRegistry();

            // Find entity
            entt::entity entity = entt::null;

            if (params.contains("entityId")) {
                entity = static_cast<entt::entity>(params["entityId"].get<uint32_t>());
            } else if (params.contains("entityName")) {
                std::string name = params["entityName"].get<std::string>();
                auto view = registry.view<ECS::TagComponent>();
                for (auto e : view) {
                    if (view.get<ECS::TagComponent>(e).Tag == name) {
                        entity = e;
                        break;
                    }
                }
            }

            if (entity == entt::null || !registry.valid(entity)) {
                return MCPResult::Error("Entity not found");
            }

            // Check for NavAgentComponent
            auto* agent = registry.try_get<ECS::NavAgentComponent>(entity);
            if (!agent) {
                return MCPResult::Error("Entity does not have NavAgentComponent");
            }

            // Get target position
            glm::vec3 target(
                params["targetX"].get<float>(),
                params["targetY"].get<float>(),
                params["targetZ"].get<float>()
            );

            // Apply max speed override if specified
            if (params.contains("maxSpeed")) {
                agent->MaxSpeed = params["maxSpeed"].get<float>();
            }

            // Set destination
            auto& navSystem = Navigation::NavigationSystem::Get();
            if (!navSystem.SetAgentDestination(entity, registry, target)) {
                return MCPResult::Error("Failed to set agent destination");
            }

            json resultData = {
                {"success", true},
                {"entityId", static_cast<uint32_t>(entity)},
                {"target", {target.x, target.y, target.z}},
                {"agentState", static_cast<int>(agent->State)}
            };

            return MCPResult::Success(resultData);
        }
    };

    /// ========================================================================
    /// SetPatrolRoute Tool
    /// ========================================================================
    class SetPatrolRouteTool : public MCPTool {
    public:
        std::string GetName() const override { return "SetPatrolRoute"; }

        std::string GetDescription() const override {
            return "Set a patrol route for a navigation agent. "
                   "The agent will automatically move between waypoints.";
        }

        json GetInputSchema() const override {
            return {
                {"type", "object"},
                {"properties", {
                    {"entityId", {
                        {"type", "integer"},
                        {"description", "Entity ID of the agent"}
                    }},
                    {"entityName", {
                        {"type", "string"},
                        {"description", "Entity name of the agent (alternative to entityId)"}
                    }},
                    {"waypoints", {
                        {"type", "array"},
                        {"items", {
                            {"type", "object"},
                            {"properties", {
                                {"x", {{"type", "number"}}},
                                {"y", {{"type", "number"}}},
                                {"z", {{"type", "number"}}},
                                {"waitTime", {
                                    {"type", "number"},
                                    {"description", "Wait time at this waypoint in seconds"}
                                }}
                            }},
                            {"required", {"x", "y", "z"}}
                        }},
                        {"description", "Array of patrol waypoints"}
                    }},
                    {"loop", {
                        {"type", "boolean"},
                        {"description", "Whether to loop the patrol route (default: true)"}
                    }},
                    {"startPatrolling", {
                        {"type", "boolean"},
                        {"description", "Whether to start patrolling immediately (default: true)"}
                    }}
                }},
                {"required", {"waypoints"}}
            };
        }

        MCPResult Execute(const json& params, ECS::Scene* scene) override {
            if (!scene) {
                return MCPResult::Error("No scene provided");
            }

            auto& registry = scene->GetRegistry();

            // Find entity
            entt::entity entity = entt::null;

            if (params.contains("entityId")) {
                entity = static_cast<entt::entity>(params["entityId"].get<uint32_t>());
            } else if (params.contains("entityName")) {
                std::string name = params["entityName"].get<std::string>();
                auto view = registry.view<ECS::TagComponent>();
                for (auto e : view) {
                    if (view.get<ECS::TagComponent>(e).Tag == name) {
                        entity = e;
                        break;
                    }
                }
            }

            if (entity == entt::null || !registry.valid(entity)) {
                return MCPResult::Error("Entity not found");
            }

            // Check for NavAgentComponent
            auto* agent = registry.try_get<ECS::NavAgentComponent>(entity);
            if (!agent) {
                return MCPResult::Error("Entity does not have NavAgentComponent");
            }

            // Build patrol route
            Navigation::PatrolRoute route;

            if (params.contains("loop")) {
                route.Loop = params["loop"].get<bool>();
            }

            const auto& waypoints = params["waypoints"];
            for (const auto& wp : waypoints) {
                Navigation::PatrolWaypoint waypoint;
                waypoint.Position = glm::vec3(
                    wp["x"].get<float>(),
                    wp["y"].get<float>(),
                    wp["z"].get<float>()
                );
                if (wp.contains("waitTime")) {
                    waypoint.WaitTime = wp["waitTime"].get<float>();
                }
                route.Waypoints.push_back(waypoint);
            }

            // Set the route
            auto& navSystem = Navigation::NavigationSystem::Get();
            navSystem.SetPatrolRoute(entity, registry, route);

            // Start patrolling if requested
            bool startPatrolling = params.value("startPatrolling", true);
            if (startPatrolling) {
                navSystem.StartPatrol(entity, registry);
            }

            json resultData = {
                {"success", true},
                {"entityId", static_cast<uint32_t>(entity)},
                {"waypointCount", route.Waypoints.size()},
                {"patrolling", startPatrolling}
            };

            return MCPResult::Success(resultData);
        }
    };

    /// ========================================================================
    /// QueryNavMesh Tool
    /// ========================================================================
    class QueryNavMeshTool : public MCPTool {
    public:
        std::string GetName() const override { return "QueryNavMesh"; }

        std::string GetDescription() const override {
            return "Query the navigation mesh for pathfinding or point validation.";
        }

        json GetInputSchema() const override {
            return {
                {"type", "object"},
                {"properties", {
                    {"queryType", {
                        {"type", "string"},
                        {"enum", {"findPath", "findNearestPoint", "isPointOnNavMesh"}},
                        {"description", "Type of query to perform"}
                    }},
                    {"startX", {{"type", "number"}}},
                    {"startY", {{"type", "number"}}},
                    {"startZ", {{"type", "number"}}},
                    {"endX", {{"type", "number"}}},
                    {"endY", {{"type", "number"}}},
                    {"endZ", {{"type", "number"}}}
                }},
                {"required", {"queryType", "startX", "startY", "startZ"}}
            };
        }

        MCPResult Execute(const json& params, ECS::Scene* scene) override {
            std::string queryType = params["queryType"].get<std::string>();

            glm::vec3 start(
                params["startX"].get<float>(),
                params["startY"].get<float>(),
                params["startZ"].get<float>()
            );

            auto& navSystem = Navigation::NavigationSystem::Get();

            if (queryType == "findPath") {
                if (!params.contains("endX") || !params.contains("endY") || !params.contains("endZ")) {
                    return MCPResult::Error("findPath requires end position");
                }

                glm::vec3 end(
                    params["endX"].get<float>(),
                    params["endY"].get<float>(),
                    params["endZ"].get<float>()
                );

                auto result = navSystem.FindPath(start, end);

                json waypointsJson = json::array();
                for (const auto& wp : result.waypoints) {
                    waypointsJson.push_back({wp.x, wp.y, wp.z});
                }

                json resultData = {
                    {"success", result.success},
                    {"waypoints", waypointsJson},
                    {"totalDistance", result.totalDistance},
                    {"isPartial", result.isPartial}
                };

                return MCPResult::Success(resultData);

            } else if (queryType == "findNearestPoint") {
                glm::vec3 nearest = navSystem.FindNearestPoint(start);

                json resultData = {
                    {"success", true},
                    {"nearestPoint", {nearest.x, nearest.y, nearest.z}},
                    {"distance", glm::length(nearest - start)}
                };

                return MCPResult::Success(resultData);

            } else if (queryType == "isPointOnNavMesh") {
                bool onMesh = navSystem.IsPointOnNavMesh(start);

                json resultData = {
                    {"success", true},
                    {"isOnNavMesh", onMesh}
                };

                return MCPResult::Success(resultData);
            }

            return MCPResult::Error("Unknown query type: " + queryType);
        }
    };

    /// ========================================================================
    /// AddNavMeshObstacle Tool
    /// ========================================================================
    class AddNavMeshObstacleTool : public MCPTool {
    public:
        std::string GetName() const override { return "AddNavMeshObstacle"; }

        std::string GetDescription() const override {
            return "Add a dynamic obstacle to the navigation mesh. "
                   "The obstacle will carve into the navmesh and affect pathfinding.";
        }

        json GetInputSchema() const override {
            return {
                {"type", "object"},
                {"properties", {
                    {"entityId", {
                        {"type", "integer"},
                        {"description", "Entity ID to add obstacle component to"}
                    }},
                    {"shape", {
                        {"type", "string"},
                        {"enum", {"box", "cylinder"}},
                        {"description", "Obstacle shape (default: box)"}
                    }},
                    {"sizeX", {{"type", "number"}, {"description", "Width (box) or radius (cylinder)"}}},
                    {"sizeY", {{"type", "number"}, {"description", "Height"}}},
                    {"sizeZ", {{"type", "number"}, {"description", "Depth (box only)"}}},
                    {"carves", {
                        {"type", "boolean"},
                        {"description", "Whether the obstacle carves into the navmesh (default: true)"}
                    }}
                }},
                {"required", {"entityId", "sizeX", "sizeY"}}
            };
        }

        MCPResult Execute(const json& params, ECS::Scene* scene) override {
            if (!scene) {
                return MCPResult::Error("No scene provided");
            }

            auto& registry = scene->GetRegistry();
            entt::entity entity = static_cast<entt::entity>(params["entityId"].get<uint32_t>());

            if (!registry.valid(entity)) {
                return MCPResult::Error("Invalid entity ID");
            }

            // Add NavMeshObstacleComponent
            auto& obstacle = registry.get_or_emplace<ECS::NavMeshObstacleComponent>(entity);

            std::string shape = params.value("shape", "box");
            if (shape == "cylinder") {
                obstacle.ObstacleShape = ECS::NavMeshObstacleComponent::Shape::Cylinder;
            } else {
                obstacle.ObstacleShape = ECS::NavMeshObstacleComponent::Shape::Box;
            }

            obstacle.Size.x = params["sizeX"].get<float>();
            obstacle.Size.y = params["sizeY"].get<float>();
            obstacle.Size.z = params.value("sizeZ", params["sizeX"].get<float>());
            obstacle.Height = obstacle.Size.y;
            obstacle.Carves = params.value("carves", true);

            // Register with navigation system
            auto& navSystem = Navigation::NavigationSystem::Get();
            bool registered = navSystem.RegisterObstacle(entity, registry);

            json resultData = {
                {"success", registered},
                {"entityId", static_cast<uint32_t>(entity)},
                {"obstacleHandle", obstacle.ObstacleHandle}
            };

            return MCPResult::Success(resultData);
        }
    };

    /// ========================================================================
    /// GetNavigationStats Tool
    /// ========================================================================
    class GetNavigationStatsTool : public MCPTool {
    public:
        std::string GetName() const override { return "GetNavigationStats"; }

        std::string GetDescription() const override {
            return "Get statistics about the navigation system, "
                   "including NavMesh data and active agents.";
        }

        json GetInputSchema() const override {
            return {
                {"type", "object"},
                {"properties", {}},
                {"required", json::array()}
            };
        }

        MCPResult Execute(const json& params, ECS::Scene* scene) override {
            auto& navManager = Navigation::NavMeshManager::Get();
            auto& navSystem = Navigation::NavigationSystem::Get();

            auto stats = navManager.GetStats();

            json resultData = {
                {"initialized", navSystem.IsInitialized()},
                {"hasNavMesh", navManager.HasNavMeshData()},
                {"navMeshStats", {
                    {"polyCount", stats.polyCount},
                    {"vertexCount", stats.vertexCount},
                    {"tileCount", stats.tileCount},
                    {"memoryUsageBytes", stats.memoryUsageBytes}
                }},
                {"debugDrawEnabled", navSystem.IsDebugDrawEnabled()}
            };

            if (navSystem.IsInitialized()) {
                auto& crowdManager = navSystem.GetCrowdManager();
                if (crowdManager.IsInitialized()) {
                    resultData["crowdStats"] = {
                        {"activeAgents", crowdManager.GetActiveAgentCount()},
                        {"maxAgents", crowdManager.GetMaxAgents()}
                    };
                }
            }

            return MCPResult::Success(resultData);
        }
    };

    /// ========================================================================
    /// Register All Navigation Tools
    /// ========================================================================
    inline void RegisterNavigationTools(MCPServer& server) {
        server.RegisterTool(std::make_unique<RebuildNavMeshTool>());
        server.RegisterTool(std::make_unique<CommandAgentMoveTool>());
        server.RegisterTool(std::make_unique<SetPatrolRouteTool>());
        server.RegisterTool(std::make_unique<QueryNavMeshTool>());
        server.RegisterTool(std::make_unique<AddNavMeshObstacleTool>());
        server.RegisterTool(std::make_unique<GetNavigationStatsTool>());
    }

    /// Factory function to create all navigation tools as a vector
    inline std::vector<MCPToolPtr> CreateNavigationTools() {
        std::vector<MCPToolPtr> tools;
        tools.push_back(std::make_unique<RebuildNavMeshTool>());
        tools.push_back(std::make_unique<CommandAgentMoveTool>());
        tools.push_back(std::make_unique<SetPatrolRouteTool>());
        tools.push_back(std::make_unique<QueryNavMeshTool>());
        tools.push_back(std::make_unique<AddNavMeshObstacleTool>());
        tools.push_back(std::make_unique<GetNavigationStatsTool>());
        return tools;
    }

} // namespace MCP
} // namespace Core
