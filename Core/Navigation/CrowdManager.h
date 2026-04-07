#pragma once

#include "NavigationConfig.h"
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <cstdint>

// Forward declarations
struct dtCrowd;
struct dtNavMesh;
struct dtNavMeshQuery;
struct dtCrowdAgentParams;

namespace Core {
namespace Navigation {

    /// @brief Result of agent operations
    struct AgentResult {
        bool Success = false;
        int32_t AgentIndex = -1;
        std::string ErrorMessage;
    };

    /// @brief Configuration for a crowd agent
    struct CrowdAgentConfig {
        float Radius = 0.5f;
        float Height = 2.0f;
        float MaxAcceleration = 8.0f;
        float MaxSpeed = 3.5f;
        float CollisionQueryRange = 12.0f;
        float PathOptimizationRange = 30.0f;
        float SeparationWeight = 2.0f;
        uint8_t UpdateFlags = 0xFF;  // All update flags
        uint8_t ObstacleAvoidanceType = 3;  // High quality
    };

    /// @brief Manages crowd simulation using DetourCrowd
    /// 
    /// Provides:
    /// - Agent registration and management
    /// - Automatic RVO collision avoidance
    /// - Path following
    /// - Crowd updates
    class CrowdManager {
    public:
        CrowdManager();
        ~CrowdManager();

        // =====================================================================
        // Lifecycle
        // =====================================================================

        /// @brief Initialize the crowd manager
        /// @param navMesh NavMesh to use for navigation
        /// @param config Crowd configuration
        /// @return True if successful
        bool Initialize(dtNavMesh* navMesh, const CrowdConfig& config = CrowdConfig{});

        /// @brief Shutdown and release resources
        void Shutdown();

        /// @brief Check if initialized
        bool IsInitialized() const { return m_Initialized; }

        /// @brief Update the crowd simulation
        /// @param deltaTime Time since last update
        void Update(float deltaTime);

        // =====================================================================
        // Agent Management
        // =====================================================================

        /// @brief Add an agent to the crowd
        /// @param position Initial position
        /// @param config Agent configuration
        /// @return Agent result with index
        AgentResult AddAgent(const glm::vec3& position, const CrowdAgentConfig& config);

        /// @brief Remove an agent from the crowd
        /// @param agentIndex Agent index
        /// @return True if removed
        bool RemoveAgent(int32_t agentIndex);

        /// @brief Update agent parameters
        /// @param agentIndex Agent index
        /// @param config New configuration
        /// @return True if updated
        bool UpdateAgentParams(int32_t agentIndex, const CrowdAgentConfig& config);

        /// @brief Set agent target position
        /// @param agentIndex Agent index
        /// @param target Target position
        /// @return True if target set successfully
        bool SetAgentTarget(int32_t agentIndex, const glm::vec3& target);

        /// @brief Request agent to move to position with velocity
        /// @param agentIndex Agent index
        /// @param velocity Desired velocity
        /// @return True if request accepted
        bool RequestMoveVelocity(int32_t agentIndex, const glm::vec3& velocity);

        /// @brief Reset agent's path/target
        /// @param agentIndex Agent index
        void ResetAgentTarget(int32_t agentIndex);

        /// @brief Teleport agent to new position
        /// @param agentIndex Agent index
        /// @param position New position
        /// @return True if teleported
        bool TeleportAgent(int32_t agentIndex, const glm::vec3& position);

        // =====================================================================
        // Agent State Queries
        // =====================================================================

        /// @brief Get agent position
        /// @param agentIndex Agent index
        /// @return Current position
        glm::vec3 GetAgentPosition(int32_t agentIndex) const;

        /// @brief Get agent velocity
        /// @param agentIndex Agent index
        /// @return Current velocity
        glm::vec3 GetAgentVelocity(int32_t agentIndex) const;

        /// @brief Get agent desired velocity
        /// @param agentIndex Agent index
        /// @return Desired velocity
        glm::vec3 GetAgentDesiredVelocity(int32_t agentIndex) const;

        /// @brief Get agent target position
        /// @param agentIndex Agent index
        /// @return Target position
        glm::vec3 GetAgentTarget(int32_t agentIndex) const;

        /// @brief Check if agent is active
        /// @param agentIndex Agent index
        /// @return True if active
        bool IsAgentActive(int32_t agentIndex) const;

        /// @brief Check if agent has reached its target
        /// @param agentIndex Agent index
        /// @param threshold Distance threshold
        /// @return True if at target
        bool HasAgentReachedTarget(int32_t agentIndex, float threshold = 0.5f) const;

        /// @brief Get agent state
        /// @param agentIndex Agent index
        /// @return Agent state
        AgentState GetAgentState(int32_t agentIndex) const;

        // =====================================================================
        // Crowd Queries
        // =====================================================================

        /// @brief Get number of active agents
        int GetActiveAgentCount() const;

        /// @brief Get maximum agent capacity
        int GetMaxAgents() const;

        /// @brief Get all agent indices
        std::vector<int32_t> GetActiveAgentIndices() const;

        /// @brief Find agents in radius
        /// @param center Search center
        /// @param radius Search radius
        /// @return List of agent indices
        std::vector<int32_t> FindAgentsInRadius(const glm::vec3& center, float radius) const;

        /// @brief Get nearest agent to position
        /// @param position Query position
        /// @param maxDistance Maximum search distance
        /// @return Agent index or -1 if none found
        int32_t GetNearestAgent(const glm::vec3& position, float maxDistance) const;

        // =====================================================================
        // Configuration
        // =====================================================================

        /// @brief Set obstacle avoidance parameters
        /// @param type Avoidance type (0-3)
        /// @param velBias Velocity bias
        /// @param adaptiveDivs Adaptive divisions
        /// @param adaptiveRings Adaptive rings
        /// @param adaptiveDepth Adaptive depth
        void SetObstacleAvoidanceParams(uint8_t type, float velBias,
                                         float adaptiveDivs, float adaptiveRings,
                                         float adaptiveDepth);

        /// @brief Get current configuration
        const CrowdConfig& GetConfig() const { return m_Config; }

        /// @brief Get the underlying Detour crowd
        dtCrowd* GetCrowd() { return m_Crowd; }

    private:
        /// @brief Convert CrowdAgentConfig to dtCrowdAgentParams
        void ConvertToDetourParams(const CrowdAgentConfig& config, dtCrowdAgentParams& params);

    private:
        bool m_Initialized = false;
        CrowdConfig m_Config;

        dtCrowd* m_Crowd = nullptr;
        dtNavMesh* m_NavMesh = nullptr;  // Not owned
        dtNavMeshQuery* m_NavQuery = nullptr;
    };

} // namespace Navigation
} // namespace Core
