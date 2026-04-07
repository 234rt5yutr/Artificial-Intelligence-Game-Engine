#pragma once

#include "NavigationConfig.h"
#include "NavMeshManager.h"
#include "CrowdManager.h"
#include "TileCacheManager.h"
#include "NavMeshDebugDraw.h"
#include <glm/glm.hpp>
#include <memory>
#include <functional>
#include <entt/entt.hpp>

namespace Core {

namespace ECS {
    class Scene;
}

namespace Navigation {

    /// @brief Main navigation system integrating all navigation components
    /// 
    /// Coordinates:
    /// - NavMesh building and management
    /// - Crowd simulation
    /// - Dynamic obstacles
    /// - Agent pathfinding
    /// - ECS integration
    class NavigationSystem {
    public:
        static NavigationSystem& Get();

        // =====================================================================
        // Lifecycle
        // =====================================================================

        /// @brief Initialize the navigation system
        /// @param config NavMesh configuration
        /// @param crowdConfig Crowd configuration
        /// @return True if successful
        bool Initialize(const NavMeshConfig& config = NavMeshConfig{},
                        const CrowdConfig& crowdConfig = CrowdConfig{});

        /// @brief Shutdown the navigation system
        void Shutdown();

        /// @brief Check if initialized
        bool IsInitialized() const { return m_Initialized; }

        /// @brief Update the navigation system
        /// @param deltaTime Time since last update
        void Update(float deltaTime);

        /// @brief Build NavMesh from scene
        /// @param scene Scene to build from
        /// @return Build result
        NavMeshBuildResult BuildNavMesh(ECS::Scene* scene);

        // =====================================================================
        // Agent Management
        // =====================================================================

        /// @brief Register an entity as a navigation agent
        /// @param entity Entity handle
        /// @param registry Entity registry
        /// @return True if registered
        bool RegisterAgent(entt::entity entity, entt::registry& registry);

        /// @brief Unregister an agent
        /// @param entity Entity handle
        /// @param registry Entity registry
        void UnregisterAgent(entt::entity entity, entt::registry& registry);

        /// @brief Set agent destination
        /// @param entity Entity handle
        /// @param registry Entity registry
        /// @param destination Target position
        /// @return True if path request succeeded
        bool SetAgentDestination(entt::entity entity, entt::registry& registry,
                                  const glm::vec3& destination);

        /// @brief Stop agent movement
        /// @param entity Entity handle
        /// @param registry Entity registry
        void StopAgent(entt::entity entity, entt::registry& registry);

        /// @brief Set patrol route for agent
        /// @param entity Entity handle
        /// @param registry Entity registry
        /// @param route Patrol route
        void SetPatrolRoute(entt::entity entity, entt::registry& registry,
                            const PatrolRoute& route);

        /// @brief Start patrolling
        /// @param entity Entity handle
        /// @param registry Entity registry
        void StartPatrol(entt::entity entity, entt::registry& registry);

        /// @brief Stop patrolling
        /// @param entity Entity handle
        /// @param registry Entity registry
        void StopPatrol(entt::entity entity, entt::registry& registry);

        // =====================================================================
        // Pathfinding
        // =====================================================================

        /// @brief Find path between two points
        /// @param start Start position
        /// @param end End position
        /// @return Path query result
        PathQueryResult FindPath(const glm::vec3& start, const glm::vec3& end);

        /// @brief Find nearest point on NavMesh
        /// @param position Query position
        /// @return Nearest point on NavMesh
        glm::vec3 FindNearestPoint(const glm::vec3& position);

        /// @brief Check if point is on NavMesh
        /// @param position Query position
        /// @return True if on NavMesh
        bool IsPointOnNavMesh(const glm::vec3& position);

        // =====================================================================
        // Dynamic Obstacles
        // =====================================================================

        /// @brief Register an entity as a navigation obstacle
        /// @param entity Entity handle
        /// @param registry Entity registry
        /// @return True if registered
        bool RegisterObstacle(entt::entity entity, entt::registry& registry);

        /// @brief Unregister an obstacle
        /// @param entity Entity handle
        /// @param registry Entity registry
        void UnregisterObstacle(entt::entity entity, entt::registry& registry);

        /// @brief Update obstacles from scene
        /// @param registry Entity registry
        void UpdateObstacles(entt::registry& registry);

        // =====================================================================
        // Off-Mesh Connections
        // =====================================================================

        /// @brief Register an off-mesh link
        /// @param entity Entity handle
        /// @param registry Entity registry
        /// @return Connection ID
        uint32_t RegisterOffMeshLink(entt::entity entity, entt::registry& registry);

        /// @brief Unregister an off-mesh link
        /// @param connectionId Connection ID
        void UnregisterOffMeshLink(uint32_t connectionId);

        // =====================================================================
        // Debug Visualization
        // =====================================================================

        /// @brief Enable debug visualization
        void EnableDebugDraw(bool enable) { m_DebugDrawEnabled = enable; }

        /// @brief Check if debug draw is enabled
        bool IsDebugDrawEnabled() const { return m_DebugDrawEnabled; }

        /// @brief Get debug draw data
        NavMeshDebugDraw& GetDebugDraw() { return m_DebugDraw; }

        /// @brief Update debug draw data
        void UpdateDebugDraw();

        // =====================================================================
        // Component Access
        // =====================================================================

        NavMeshManager& GetNavMeshManager() { return NavMeshManager::Get(); }
        CrowdManager& GetCrowdManager() { return *m_CrowdManager; }
        TileCacheManager& GetTileCacheManager() { return *m_TileCacheManager; }

        // =====================================================================
        // Configuration
        // =====================================================================

        const NavMeshConfig& GetConfig() const { return m_Config; }
        const CrowdConfig& GetCrowdConfig() const { return m_CrowdConfig; }

    private:
        NavigationSystem() = default;
        ~NavigationSystem() = default;

        NavigationSystem(const NavigationSystem&) = delete;
        NavigationSystem& operator=(const NavigationSystem&) = delete;

        /// @brief Update agents from ECS
        void UpdateAgents(float deltaTime, entt::registry& registry);

        /// @brief Update patrol behavior
        void UpdatePatrol(float deltaTime, entt::registry& registry);

        /// @brief Sync agent position to transform
        void SyncAgentTransforms(entt::registry& registry);

    private:
        bool m_Initialized = false;
        NavMeshConfig m_Config;
        CrowdConfig m_CrowdConfig;

        std::unique_ptr<CrowdManager> m_CrowdManager;
        std::unique_ptr<TileCacheManager> m_TileCacheManager;

        NavMeshDebugDraw m_DebugDraw;
        bool m_DebugDrawEnabled = false;

        ECS::Scene* m_CurrentScene = nullptr;
    };

} // namespace Navigation
} // namespace Core
