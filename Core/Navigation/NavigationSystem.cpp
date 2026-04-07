#include "NavigationSystem.h"
#include "../ECS/Scene.h"
#include "../ECS/Components/Components.h"
#include <spdlog/spdlog.h>

namespace Core {
namespace Navigation {

    NavigationSystem& NavigationSystem::Get() {
        static NavigationSystem instance;
        return instance;
    }

    bool NavigationSystem::Initialize(const NavMeshConfig& config, const CrowdConfig& crowdConfig) {
        if (m_Initialized) {
            Shutdown();
        }

        m_Config = config;
        m_CrowdConfig = crowdConfig;

        // Initialize NavMesh manager
        if (!NavMeshManager::Get().Initialize(config)) {
            spdlog::error("[NavigationSystem] Failed to initialize NavMeshManager");
            return false;
        }

        // Create crowd manager (will be initialized after NavMesh is built)
        m_CrowdManager = std::make_unique<CrowdManager>();

        // Create TileCache manager (optional, for dynamic obstacles)
        m_TileCacheManager = std::make_unique<TileCacheManager>();

        m_Initialized = true;
        spdlog::info("[NavigationSystem] Initialized");
        return true;
    }

    void NavigationSystem::Shutdown() {
        m_TileCacheManager.reset();
        m_CrowdManager.reset();
        NavMeshManager::Get().Shutdown();
        m_CurrentScene = nullptr;
        m_Initialized = false;
    }

    void NavigationSystem::Update(float deltaTime) {
        if (!m_Initialized) return;

        // Process TileCache updates
        if (m_TileCacheManager && m_TileCacheManager->IsInitialized()) {
            m_TileCacheManager->ProcessUpdates(4);  // Max 4 tiles per frame
        }

        // Rebuild dirty NavMesh tiles
        if (NavMeshManager::Get().HasDirtyTiles()) {
            NavMeshManager::Get().RebuildDirtyTiles();
        }

        // Update crowd simulation
        if (m_CrowdManager && m_CrowdManager->IsInitialized()) {
            m_CrowdManager->Update(deltaTime);
        }

        // Update debug visualization
        if (m_DebugDrawEnabled) {
            UpdateDebugDraw();
        }
    }

    NavMeshBuildResult NavigationSystem::BuildNavMesh(ECS::Scene* scene) {
        NavMeshBuildResult result;

        if (!m_Initialized) {
            result.success = false;
            result.errorMessage = "Navigation system not initialized";
            return result;
        }

        m_CurrentScene = scene;

        // Build NavMesh from scene
        result = NavMeshManager::Get().BuildFromScene(scene);
        if (!result.success) {
            return result;
        }

        // Initialize crowd manager with built NavMesh
        dtNavMesh* navMesh = NavMeshManager::Get().GetNavMesh();
        if (navMesh && !m_CrowdManager->IsInitialized()) {
            if (!m_CrowdManager->Initialize(navMesh, m_CrowdConfig)) {
                spdlog::error("[NavigationSystem] Failed to initialize CrowdManager");
            }
        }

        spdlog::info("[NavigationSystem] NavMesh built successfully");
        return result;
    }

    bool NavigationSystem::RegisterAgent(entt::entity entity, entt::registry& registry) {
        if (!m_CrowdManager || !m_CrowdManager->IsInitialized()) {
            spdlog::warn("[NavigationSystem] CrowdManager not ready, cannot register agent");
            return false;
        }

        // Get required components
        auto* transform = registry.try_get<ECS::TransformComponent>(entity);
        auto* agent = registry.try_get<ECS::NavAgentComponent>(entity);

        if (!transform || !agent) {
            spdlog::warn("[NavigationSystem] Entity missing TransformComponent or NavAgentComponent");
            return false;
        }

        // Create crowd agent config
        CrowdAgentConfig config;
        config.Radius = agent->Radius;
        config.Height = agent->Height;
        config.MaxSpeed = agent->MaxSpeed;
        config.MaxAcceleration = agent->MaxAcceleration;
        config.SeparationWeight = agent->SeparationWeight;

        // Map avoidance quality to type
        switch (agent->AvoidanceQuality) {
            case AvoidanceQuality::None:   config.ObstacleAvoidanceType = 0; break;
            case AvoidanceQuality::Low:    config.ObstacleAvoidanceType = 1; break;
            case AvoidanceQuality::Medium: config.ObstacleAvoidanceType = 2; break;
            case AvoidanceQuality::High:   config.ObstacleAvoidanceType = 3; break;
        }

        // Add agent to crowd
        AgentResult result = m_CrowdManager->AddAgent(transform->Position, config);
        if (!result.Success) {
            spdlog::error("[NavigationSystem] Failed to add agent: {}", result.ErrorMessage);
            return false;
        }

        agent->CrowdAgentIndex = result.AgentIndex;
        agent->LastPosition = transform->Position;
        agent->State = AgentState::Idle;

        spdlog::debug("[NavigationSystem] Registered agent with index {}", result.AgentIndex);
        return true;
    }

    void NavigationSystem::UnregisterAgent(entt::entity entity, entt::registry& registry) {
        auto* agent = registry.try_get<ECS::NavAgentComponent>(entity);
        if (!agent || agent->CrowdAgentIndex < 0) return;

        if (m_CrowdManager && m_CrowdManager->IsInitialized()) {
            m_CrowdManager->RemoveAgent(agent->CrowdAgentIndex);
        }

        agent->CrowdAgentIndex = -1;
        agent->Reset();
    }

    bool NavigationSystem::SetAgentDestination(entt::entity entity, entt::registry& registry,
                                                const glm::vec3& destination) {
        auto* agent = registry.try_get<ECS::NavAgentComponent>(entity);
        if (!agent || agent->CrowdAgentIndex < 0) return false;

        if (!m_CrowdManager || !m_CrowdManager->IsInitialized()) return false;

        // Request path through crowd
        if (!m_CrowdManager->SetAgentTarget(agent->CrowdAgentIndex, destination)) {
            if (agent->OnPathFailed) {
                agent->OnPathFailed("Failed to set target");
            }
            return false;
        }

        agent->TargetPosition = destination;
        agent->HasTarget = true;
        agent->IsMoving = true;
        agent->State = AgentState::Moving;
        agent->IsPatrolling = false;

        return true;
    }

    void NavigationSystem::StopAgent(entt::entity entity, entt::registry& registry) {
        auto* agent = registry.try_get<ECS::NavAgentComponent>(entity);
        if (!agent || agent->CrowdAgentIndex < 0) return;

        if (m_CrowdManager && m_CrowdManager->IsInitialized()) {
            m_CrowdManager->ResetAgentTarget(agent->CrowdAgentIndex);
        }

        agent->HasTarget = false;
        agent->IsMoving = false;
        agent->State = AgentState::Idle;
        agent->CurrentPath.clear();
    }

    void NavigationSystem::SetPatrolRoute(entt::entity entity, entt::registry& registry,
                                           const PatrolRoute& route) {
        auto* agent = registry.try_get<ECS::NavAgentComponent>(entity);
        if (!agent) return;

        agent->PatrolRoute = route;
        agent->PatrolWaypointIndex = 0;
    }

    void NavigationSystem::StartPatrol(entt::entity entity, entt::registry& registry) {
        auto* agent = registry.try_get<ECS::NavAgentComponent>(entity);
        if (!agent || agent->PatrolRoute.Waypoints.empty()) return;

        agent->IsPatrolling = true;
        agent->PatrolWaypointIndex = 0;
        agent->PatrolWaitTimer = 0.0f;

        // Set destination to first waypoint
        const auto& waypoint = agent->PatrolRoute.Waypoints[0];
        SetAgentDestination(entity, registry, waypoint.Position);
    }

    void NavigationSystem::StopPatrol(entt::entity entity, entt::registry& registry) {
        auto* agent = registry.try_get<ECS::NavAgentComponent>(entity);
        if (!agent) return;

        agent->IsPatrolling = false;
        StopAgent(entity, registry);
    }

    PathQueryResult NavigationSystem::FindPath(const glm::vec3& start, const glm::vec3& end) {
        return NavMeshManager::Get().FindPath(start, end);
    }

    glm::vec3 NavigationSystem::FindNearestPoint(const glm::vec3& position) {
        return NavMeshManager::Get().FindNearestPoint(position);
    }

    bool NavigationSystem::IsPointOnNavMesh(const glm::vec3& position) {
        return NavMeshManager::Get().IsPointOnNavMesh(position);
    }

    bool NavigationSystem::RegisterObstacle(entt::entity entity, entt::registry& registry) {
        if (!m_TileCacheManager || !m_TileCacheManager->IsInitialized()) {
            return false;
        }

        auto* transform = registry.try_get<ECS::TransformComponent>(entity);
        auto* obstacle = registry.try_get<ECS::NavMeshObstacleComponent>(entity);

        if (!transform || !obstacle) return false;

        ObstacleResult result;
        if (obstacle->ObstacleShape == ECS::NavMeshObstacleComponent::Shape::Box) {
            result = m_TileCacheManager->AddBoxObstacle(
                transform->Position,
                obstacle->Size,
                0.0f  // TODO: Get rotation from transform
            );
        } else {
            result = m_TileCacheManager->AddCylinderObstacle(
                transform->Position,
                obstacle->Size.x,  // Radius
                obstacle->Height
            );
        }

        if (result.Success) {
            obstacle->ObstacleHandle = result.ObstacleRef;
            obstacle->LastPosition = transform->Position;
            return true;
        }

        return false;
    }

    void NavigationSystem::UnregisterObstacle(entt::entity entity, entt::registry& registry) {
        auto* obstacle = registry.try_get<ECS::NavMeshObstacleComponent>(entity);
        if (!obstacle || obstacle->ObstacleHandle == 0) return;

        if (m_TileCacheManager && m_TileCacheManager->IsInitialized()) {
            m_TileCacheManager->RemoveObstacle(obstacle->ObstacleHandle);
        }

        obstacle->ObstacleHandle = 0;
    }

    void NavigationSystem::UpdateObstacles(entt::registry& registry) {
        if (!m_TileCacheManager || !m_TileCacheManager->IsInitialized()) return;

        auto view = registry.view<ECS::TransformComponent, ECS::NavMeshObstacleComponent>();

        for (auto entity : view) {
            auto& transform = view.get<ECS::TransformComponent>(entity);
            auto& obstacle = view.get<ECS::NavMeshObstacleComponent>(entity);

            if (obstacle.ObstacleHandle == 0) continue;

            // Check if obstacle has moved
            float distMoved = glm::length(transform.Position - obstacle.LastPosition);
            if (distMoved > obstacle.MoveThreshold) {
                // Update obstacle position
                m_TileCacheManager->UpdateObstaclePosition(
                    obstacle.ObstacleHandle, transform.Position);
                obstacle.LastPosition = transform.Position;
            }
        }
    }

    uint32_t NavigationSystem::RegisterOffMeshLink(entt::entity entity, entt::registry& registry) {
        auto* transform = registry.try_get<ECS::TransformComponent>(entity);
        auto* link = registry.try_get<ECS::OffMeshLinkComponent>(entity);

        if (!transform || !link || !link->Enabled) return 0;

        glm::vec3 start = transform->Position + link->StartOffset;
        
        return NavMeshManager::Get().AddOffMeshConnection(
            start,
            link->EndPosition,
            link->Radius,
            link->Bidirectional,
            link->AreaType
        );
    }

    void NavigationSystem::UnregisterOffMeshLink(uint32_t connectionId) {
        NavMeshManager::Get().RemoveOffMeshConnection(connectionId);
    }

    void NavigationSystem::UpdateDebugDraw() {
        m_DebugDraw.Clear();

        auto* navMesh = NavMeshManager::Get().GetNavMesh();
        if (navMesh) {
            m_DebugDraw.DrawNavMesh(navMesh,
                NavMeshDebugDraw::DRAW_POLYGONS |
                NavMeshDebugDraw::DRAW_MESH_BOUNDS |
                NavMeshDebugDraw::DRAW_OFF_MESH);
        }

        if (m_CrowdManager && m_CrowdManager->IsInitialized()) {
            m_DebugDraw.DrawCrowd(m_CrowdManager->GetCrowd(),
                NavMeshDebugDraw::DRAW_AGENTS |
                NavMeshDebugDraw::DRAW_PATHS);
        }
    }

    void NavigationSystem::UpdateAgents(float deltaTime, entt::registry& registry) {
        if (!m_CrowdManager || !m_CrowdManager->IsInitialized()) return;

        auto view = registry.view<ECS::TransformComponent, ECS::NavAgentComponent>();

        for (auto entity : view) {
            auto& transform = view.get<ECS::TransformComponent>(entity);
            auto& agent = view.get<ECS::NavAgentComponent>(entity);

            if (agent.CrowdAgentIndex < 0) continue;

            // Get crowd agent state
            glm::vec3 crowdPos = m_CrowdManager->GetAgentPosition(agent.CrowdAgentIndex);
            glm::vec3 crowdVel = m_CrowdManager->GetAgentVelocity(agent.CrowdAgentIndex);

            // Update velocity
            agent.Velocity = crowdVel;
            agent.DesiredVelocity = m_CrowdManager->GetAgentDesiredVelocity(agent.CrowdAgentIndex);

            // Check if reached target
            if (agent.HasTarget && m_CrowdManager->HasAgentReachedTarget(
                    agent.CrowdAgentIndex, agent.StoppingDistance)) {
                agent.HasTarget = false;
                agent.IsMoving = false;
                agent.State = AgentState::Idle;

                if (agent.OnPathComplete) {
                    agent.OnPathComplete();
                }
            }

            // Stuck detection
            float moveDist = glm::length(crowdPos - agent.LastPosition);
            if (agent.IsMoving && moveDist < 0.01f * deltaTime) {
                agent.StuckTime += deltaTime;
                if (agent.StuckTime > agent.StuckThreshold) {
                    agent.State = AgentState::Stuck;
                    if (agent.OnStuck) {
                        agent.OnStuck();
                    }
                }
            } else {
                agent.StuckTime = 0.0f;
            }

            agent.LastPosition = crowdPos;

            // Update transform position from crowd
            transform.Position = crowdPos;

            // Update facing direction from velocity
            if (glm::length(crowdVel) > 0.1f) {
                float yaw = std::atan2(crowdVel.x, crowdVel.z);
                transform.Rotation.y = glm::degrees(yaw);
            }
        }
    }

    void NavigationSystem::UpdatePatrol(float deltaTime, entt::registry& registry) {
        auto view = registry.view<ECS::TransformComponent, ECS::NavAgentComponent>();

        for (auto entity : view) {
            auto& transform = view.get<ECS::TransformComponent>(entity);
            auto& agent = view.get<ECS::NavAgentComponent>(entity);

            if (!agent.IsPatrolling || agent.PatrolRoute.Waypoints.empty()) continue;

            // Check if at current waypoint
            const auto& waypoint = agent.PatrolRoute.Waypoints[agent.PatrolWaypointIndex];
            float dist = glm::length(transform.Position - waypoint.Position);

            if (dist <= agent.StoppingDistance && !agent.IsMoving) {
                // At waypoint, wait if needed
                agent.PatrolWaitTimer += deltaTime;

                if (agent.OnPatrolWaypointReached) {
                    agent.OnPatrolWaypointReached(agent.PatrolWaypointIndex);
                }

                if (agent.PatrolWaitTimer >= waypoint.WaitTime) {
                    agent.PatrolWaitTimer = 0.0f;

                    // Move to next waypoint
                    if (agent.PatrolRoute.Loop) {
                        agent.PatrolWaypointIndex = 
                            (agent.PatrolWaypointIndex + 1) % 
                            static_cast<int>(agent.PatrolRoute.Waypoints.size());
                    } else if (agent.PatrolRoute.PingPong) {
                        // TODO: Implement ping-pong patrol
                        agent.PatrolWaypointIndex = 
                            (agent.PatrolWaypointIndex + 1) % 
                            static_cast<int>(agent.PatrolRoute.Waypoints.size());
                    } else {
                        agent.PatrolWaypointIndex++;
                        if (agent.PatrolWaypointIndex >= 
                            static_cast<int>(agent.PatrolRoute.Waypoints.size())) {
                            agent.IsPatrolling = false;
                            continue;
                        }
                    }

                    // Set new target
                    const auto& nextWaypoint = agent.PatrolRoute.Waypoints[agent.PatrolWaypointIndex];
                    SetAgentDestination(entity, registry, nextWaypoint.Position);
                    agent.IsPatrolling = true;  // Restore patrol flag
                }
            }
        }
    }

    void NavigationSystem::SyncAgentTransforms(entt::registry& registry) {
        // This syncs ECS transforms to crowd - done automatically in UpdateAgents
    }

} // namespace Navigation
} // namespace Core
