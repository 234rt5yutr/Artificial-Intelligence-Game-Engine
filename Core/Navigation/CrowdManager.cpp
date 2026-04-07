#include "CrowdManager.h"
#include <DetourCrowd.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <spdlog/spdlog.h>
#include <cstring>
#include <algorithm>

namespace Core {
namespace Navigation {

    CrowdManager::CrowdManager() = default;

    CrowdManager::~CrowdManager() {
        Shutdown();
    }

    bool CrowdManager::Initialize(dtNavMesh* navMesh, const CrowdConfig& config) {
        if (m_Initialized) {
            Shutdown();
        }

        if (!navMesh) {
            spdlog::error("[CrowdManager] Invalid NavMesh");
            return false;
        }

        m_NavMesh = navMesh;
        m_Config = config;

        // Create NavMesh query
        m_NavQuery = dtAllocNavMeshQuery();
        if (!m_NavQuery) {
            spdlog::error("[CrowdManager] Failed to allocate NavMesh query");
            return false;
        }

        dtStatus status = m_NavQuery->init(navMesh, 2048);
        if (dtStatusFailed(status)) {
            dtFreeNavMeshQuery(m_NavQuery);
            m_NavQuery = nullptr;
            spdlog::error("[CrowdManager] Failed to init NavMesh query");
            return false;
        }

        // Create crowd
        m_Crowd = dtAllocCrowd();
        if (!m_Crowd) {
            dtFreeNavMeshQuery(m_NavQuery);
            m_NavQuery = nullptr;
            spdlog::error("[CrowdManager] Failed to allocate crowd");
            return false;
        }

        if (!m_Crowd->init(static_cast<int>(config.MaxAgents), config.MaxAgentRadius, navMesh)) {
            dtFreeCrowd(m_Crowd);
            m_Crowd = nullptr;
            dtFreeNavMeshQuery(m_NavQuery);
            m_NavQuery = nullptr;
            spdlog::error("[CrowdManager] Failed to init crowd");
            return false;
        }

        // Set up obstacle avoidance parameters for each quality level
        dtObstacleAvoidanceParams params;

        // Low quality
        std::memset(&params, 0, sizeof(params));
        params.velBias = 0.5f;
        params.weightDesVel = 2.0f;
        params.weightCurVel = 0.75f;
        params.weightSide = 0.75f;
        params.weightToi = 2.5f;
        params.horizTime = 2.5f;
        params.gridSize = 33;
        params.adaptiveDivs = 5;
        params.adaptiveRings = 2;
        params.adaptiveDepth = 1;
        m_Crowd->setObstacleAvoidanceParams(0, &params);

        // Medium quality
        params.adaptiveDivs = 5;
        params.adaptiveRings = 2;
        params.adaptiveDepth = 2;
        m_Crowd->setObstacleAvoidanceParams(1, &params);

        // Good quality
        params.adaptiveDivs = 7;
        params.adaptiveRings = 2;
        params.adaptiveDepth = 3;
        m_Crowd->setObstacleAvoidanceParams(2, &params);

        // High quality
        params.adaptiveDivs = 7;
        params.adaptiveRings = 3;
        params.adaptiveDepth = 3;
        m_Crowd->setObstacleAvoidanceParams(3, &params);

        m_Initialized = true;
        spdlog::info("[CrowdManager] Initialized with max {} agents", config.MaxAgents);
        return true;
    }

    void CrowdManager::Shutdown() {
        if (m_Crowd) {
            dtFreeCrowd(m_Crowd);
            m_Crowd = nullptr;
        }

        if (m_NavQuery) {
            dtFreeNavMeshQuery(m_NavQuery);
            m_NavQuery = nullptr;
        }

        m_NavMesh = nullptr;
        m_Initialized = false;
    }

    void CrowdManager::Update(float deltaTime) {
        if (!m_Crowd) return;

        m_Crowd->update(deltaTime, nullptr);
    }

    void CrowdManager::ConvertToDetourParams(const CrowdAgentConfig& config,
                                              dtCrowdAgentParams& params) {
        std::memset(&params, 0, sizeof(params));

        params.radius = config.Radius;
        params.height = config.Height;
        params.maxAcceleration = config.MaxAcceleration;
        params.maxSpeed = config.MaxSpeed;
        params.collisionQueryRange = config.CollisionQueryRange;
        params.pathOptimizationRange = config.PathOptimizationRange;
        params.separationWeight = config.SeparationWeight;
        params.updateFlags = config.UpdateFlags;
        params.obstacleAvoidanceType = config.ObstacleAvoidanceType;

        // Query filter
        params.queryFilterType = 0;  // Default filter
    }

    AgentResult CrowdManager::AddAgent(const glm::vec3& position,
                                        const CrowdAgentConfig& config) {
        AgentResult result;

        if (!m_Crowd) {
            result.ErrorMessage = "Crowd not initialized";
            return result;
        }

        dtCrowdAgentParams params;
        ConvertToDetourParams(config, params);

        float pos[3] = { position.x, position.y, position.z };
        int agentIndex = m_Crowd->addAgent(pos, &params);

        if (agentIndex < 0) {
            result.ErrorMessage = "Failed to add agent (crowd may be full)";
            return result;
        }

        result.Success = true;
        result.AgentIndex = agentIndex;
        return result;
    }

    bool CrowdManager::RemoveAgent(int32_t agentIndex) {
        if (!m_Crowd) return false;

        const dtCrowdAgent* agent = m_Crowd->getAgent(agentIndex);
        if (!agent || !agent->active) return false;

        m_Crowd->removeAgent(agentIndex);
        return true;
    }

    bool CrowdManager::UpdateAgentParams(int32_t agentIndex, const CrowdAgentConfig& config) {
        if (!m_Crowd) return false;

        const dtCrowdAgent* agent = m_Crowd->getAgent(agentIndex);
        if (!agent || !agent->active) return false;

        dtCrowdAgentParams params;
        ConvertToDetourParams(config, params);

        m_Crowd->updateAgentParameters(agentIndex, &params);
        return true;
    }

    bool CrowdManager::SetAgentTarget(int32_t agentIndex, const glm::vec3& target) {
        if (!m_Crowd || !m_NavQuery) return false;

        const dtCrowdAgent* agent = m_Crowd->getAgent(agentIndex);
        if (!agent || !agent->active) return false;

        // Find nearest poly
        float targetPos[3] = { target.x, target.y, target.z };
        float extents[3] = { 2.0f, 4.0f, 2.0f };
        float nearestPos[3];
        dtPolyRef nearestRef;

        dtQueryFilter filter;
        filter.setIncludeFlags(0xFFFF);
        filter.setExcludeFlags(0);

        dtStatus status = m_NavQuery->findNearestPoly(targetPos, extents, &filter,
                                                       &nearestRef, nearestPos);
        if (dtStatusFailed(status) || nearestRef == 0) {
            return false;
        }

        return m_Crowd->requestMoveTarget(agentIndex, nearestRef, nearestPos);
    }

    bool CrowdManager::RequestMoveVelocity(int32_t agentIndex, const glm::vec3& velocity) {
        if (!m_Crowd) return false;

        const dtCrowdAgent* agent = m_Crowd->getAgent(agentIndex);
        if (!agent || !agent->active) return false;

        float vel[3] = { velocity.x, velocity.y, velocity.z };
        return m_Crowd->requestMoveVelocity(agentIndex, vel);
    }

    void CrowdManager::ResetAgentTarget(int32_t agentIndex) {
        if (!m_Crowd) return;

        const dtCrowdAgent* agent = m_Crowd->getAgent(agentIndex);
        if (!agent || !agent->active) return;

        m_Crowd->resetMoveTarget(agentIndex);
    }

    bool CrowdManager::TeleportAgent(int32_t agentIndex, const glm::vec3& position) {
        if (!m_Crowd) return false;

        // Detour doesn't support direct teleport, so we remove and re-add
        const dtCrowdAgent* agent = m_Crowd->getAgent(agentIndex);
        if (!agent || !agent->active) return false;

        // Store current params
        dtCrowdAgentParams params = agent->params;

        // Remove agent
        m_Crowd->removeAgent(agentIndex);

        // Re-add at new position
        float pos[3] = { position.x, position.y, position.z };
        int newIndex = m_Crowd->addAgent(pos, &params);

        return newIndex == agentIndex;  // Should get same index if slot was empty
    }

    glm::vec3 CrowdManager::GetAgentPosition(int32_t agentIndex) const {
        if (!m_Crowd) return glm::vec3(0.0f);

        const dtCrowdAgent* agent = m_Crowd->getAgent(agentIndex);
        if (!agent || !agent->active) return glm::vec3(0.0f);

        return glm::vec3(agent->npos[0], agent->npos[1], agent->npos[2]);
    }

    glm::vec3 CrowdManager::GetAgentVelocity(int32_t agentIndex) const {
        if (!m_Crowd) return glm::vec3(0.0f);

        const dtCrowdAgent* agent = m_Crowd->getAgent(agentIndex);
        if (!agent || !agent->active) return glm::vec3(0.0f);

        return glm::vec3(agent->vel[0], agent->vel[1], agent->vel[2]);
    }

    glm::vec3 CrowdManager::GetAgentDesiredVelocity(int32_t agentIndex) const {
        if (!m_Crowd) return glm::vec3(0.0f);

        const dtCrowdAgent* agent = m_Crowd->getAgent(agentIndex);
        if (!agent || !agent->active) return glm::vec3(0.0f);

        return glm::vec3(agent->dvel[0], agent->dvel[1], agent->dvel[2]);
    }

    glm::vec3 CrowdManager::GetAgentTarget(int32_t agentIndex) const {
        if (!m_Crowd) return glm::vec3(0.0f);

        const dtCrowdAgent* agent = m_Crowd->getAgent(agentIndex);
        if (!agent || !agent->active) return glm::vec3(0.0f);

        return glm::vec3(agent->targetPos[0], agent->targetPos[1], agent->targetPos[2]);
    }

    bool CrowdManager::IsAgentActive(int32_t agentIndex) const {
        if (!m_Crowd) return false;

        const dtCrowdAgent* agent = m_Crowd->getAgent(agentIndex);
        return agent && agent->active;
    }

    bool CrowdManager::HasAgentReachedTarget(int32_t agentIndex, float threshold) const {
        if (!m_Crowd) return false;

        const dtCrowdAgent* agent = m_Crowd->getAgent(agentIndex);
        if (!agent || !agent->active) return false;

        if (agent->targetState != DT_CROWDAGENT_TARGET_VALID) {
            return true;  // No valid target means "at target"
        }

        glm::vec3 pos(agent->npos[0], agent->npos[1], agent->npos[2]);
        glm::vec3 target(agent->targetPos[0], agent->targetPos[1], agent->targetPos[2]);

        return glm::length(pos - target) <= threshold;
    }

    AgentState CrowdManager::GetAgentState(int32_t agentIndex) const {
        if (!m_Crowd) return AgentState::Idle;

        const dtCrowdAgent* agent = m_Crowd->getAgent(agentIndex);
        if (!agent || !agent->active) return AgentState::Idle;

        switch (agent->state) {
            case DT_CROWDAGENT_STATE_WALKING:
                return AgentState::Moving;
            case DT_CROWDAGENT_STATE_OFFMESH:
                return AgentState::OffMesh;
            default:
                if (agent->targetState == DT_CROWDAGENT_TARGET_REQUESTING ||
                    agent->targetState == DT_CROWDAGENT_TARGET_WAITING_FOR_QUEUE ||
                    agent->targetState == DT_CROWDAGENT_TARGET_WAITING_FOR_PATH) {
                    return AgentState::WaitingForPath;
                }
                return AgentState::Idle;
        }
    }

    int CrowdManager::GetActiveAgentCount() const {
        if (!m_Crowd) return 0;

        int count = 0;
        for (int i = 0; i < m_Crowd->getAgentCount(); ++i) {
            const dtCrowdAgent* agent = m_Crowd->getAgent(i);
            if (agent && agent->active) {
                ++count;
            }
        }
        return count;
    }

    int CrowdManager::GetMaxAgents() const {
        return m_Crowd ? m_Crowd->getAgentCount() : 0;
    }

    std::vector<int32_t> CrowdManager::GetActiveAgentIndices() const {
        std::vector<int32_t> indices;
        if (!m_Crowd) return indices;

        for (int i = 0; i < m_Crowd->getAgentCount(); ++i) {
            const dtCrowdAgent* agent = m_Crowd->getAgent(i);
            if (agent && agent->active) {
                indices.push_back(i);
            }
        }
        return indices;
    }

    std::vector<int32_t> CrowdManager::FindAgentsInRadius(const glm::vec3& center,
                                                           float radius) const {
        std::vector<int32_t> result;
        if (!m_Crowd) return result;

        float radiusSq = radius * radius;

        for (int i = 0; i < m_Crowd->getAgentCount(); ++i) {
            const dtCrowdAgent* agent = m_Crowd->getAgent(i);
            if (!agent || !agent->active) continue;

            glm::vec3 pos(agent->npos[0], agent->npos[1], agent->npos[2]);
            float distSq = glm::dot(pos - center, pos - center);

            if (distSq <= radiusSq) {
                result.push_back(i);
            }
        }

        return result;
    }

    int32_t CrowdManager::GetNearestAgent(const glm::vec3& position, float maxDistance) const {
        if (!m_Crowd) return -1;

        int32_t nearest = -1;
        float nearestDistSq = maxDistance * maxDistance;

        for (int i = 0; i < m_Crowd->getAgentCount(); ++i) {
            const dtCrowdAgent* agent = m_Crowd->getAgent(i);
            if (!agent || !agent->active) continue;

            glm::vec3 pos(agent->npos[0], agent->npos[1], agent->npos[2]);
            float distSq = glm::dot(pos - position, pos - position);

            if (distSq < nearestDistSq) {
                nearestDistSq = distSq;
                nearest = i;
            }
        }

        return nearest;
    }

    void CrowdManager::SetObstacleAvoidanceParams(uint8_t type, float velBias,
                                                   float adaptiveDivs, float adaptiveRings,
                                                   float adaptiveDepth) {
        if (!m_Crowd || type > 3) return;

        dtObstacleAvoidanceParams params;
        std::memset(&params, 0, sizeof(params));

        params.velBias = velBias;
        params.weightDesVel = 2.0f;
        params.weightCurVel = 0.75f;
        params.weightSide = 0.75f;
        params.weightToi = 2.5f;
        params.horizTime = 2.5f;
        params.gridSize = 33;
        params.adaptiveDivs = static_cast<unsigned char>(adaptiveDivs);
        params.adaptiveRings = static_cast<unsigned char>(adaptiveRings);
        params.adaptiveDepth = static_cast<unsigned char>(adaptiveDepth);

        m_Crowd->setObstacleAvoidanceParams(type, &params);
    }

} // namespace Navigation
} // namespace Core
