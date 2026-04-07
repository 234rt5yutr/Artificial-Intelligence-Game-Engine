#pragma once

#include "Core/Navigation/NavigationConfig.h"
#include <glm/glm.hpp>
#include <vector>
#include <functional>
#include <cstdint>

namespace Core {
namespace ECS {

    /// @brief Component for AI agents that use navigation
    struct NavAgentComponent {
        // =====================================================================
        // Agent Parameters
        // =====================================================================

        /// Agent radius for collision avoidance
        float Radius = 0.5f;

        /// Agent height
        float Height = 2.0f;

        /// Maximum movement speed
        float MaxSpeed = 3.5f;

        /// Maximum acceleration
        float MaxAcceleration = 8.0f;

        /// Navigation query filter (which areas can be traversed)
        uint16_t IncludeFlags = Navigation::FLAG_WALK | Navigation::FLAG_DOOR;
        uint16_t ExcludeFlags = Navigation::FLAG_DISABLED;

        // =====================================================================
        // Current State
        // =====================================================================

        /// Current agent state
        Navigation::AgentState State = Navigation::AgentState::Idle;

        /// Current velocity
        glm::vec3 Velocity = glm::vec3(0.0f);

        /// Current desired velocity (before collision avoidance)
        glm::vec3 DesiredVelocity = glm::vec3(0.0f);

        /// Current target position
        glm::vec3 TargetPosition = glm::vec3(0.0f);

        /// Whether the agent has a valid target
        bool HasTarget = false;

        /// Whether the agent is currently moving
        bool IsMoving = false;

        /// Distance to consider target reached
        float StoppingDistance = 0.5f;

        // =====================================================================
        // Path Following
        // =====================================================================

        /// Path follow mode
        Navigation::PathFollowMode FollowMode = Navigation::PathFollowMode::FollowPath;

        /// Current path waypoints
        std::vector<glm::vec3> CurrentPath;

        /// Current waypoint index
        int32_t CurrentWaypointIndex = 0;

        /// Distance ahead to look for path smoothing
        float PathLookahead = 2.0f;

        /// How close to get to waypoints before advancing
        float WaypointRadius = 0.5f;

        // =====================================================================
        // Avoidance Settings
        // =====================================================================

        /// Avoidance quality level
        Navigation::AvoidanceQuality AvoidanceQuality = Navigation::AvoidanceQuality::Medium;

        /// Avoidance priority (lower = higher priority)
        uint8_t AvoidancePriority = 50;

        /// Separation weight for crowd avoidance
        float SeparationWeight = 2.0f;

        /// Cohesion weight (for group behavior)
        float CohesionWeight = 0.0f;

        /// Alignment weight (for flocking behavior)
        float AlignmentWeight = 0.0f;

        // =====================================================================
        // Patrol Settings
        // =====================================================================

        /// Current patrol route (if any)
        Navigation::PatrolRoute PatrolRoute;

        /// Current patrol waypoint index
        int32_t PatrolWaypointIndex = 0;

        /// Whether currently patrolling
        bool IsPatrolling = false;

        /// Time spent waiting at current patrol point
        float PatrolWaitTimer = 0.0f;

        // =====================================================================
        // Internal State
        // =====================================================================

        /// Detour crowd agent index (-1 if not registered)
        int32_t CrowdAgentIndex = -1;

        /// Last known position (for stuck detection)
        glm::vec3 LastPosition = glm::vec3(0.0f);

        /// Time since last significant movement
        float StuckTime = 0.0f;

        /// Stuck threshold (seconds without movement)
        float StuckThreshold = 2.0f;

        // =====================================================================
        // Callbacks
        // =====================================================================

        /// Callback when path is complete
        std::function<void()> OnPathComplete;

        /// Callback when path fails
        std::function<void(const std::string&)> OnPathFailed;

        /// Callback when stuck is detected
        std::function<void()> OnStuck;

        /// Callback when patrol waypoint is reached
        std::function<void(int32_t)> OnPatrolWaypointReached;

        // =====================================================================
        // Helper Methods
        // =====================================================================

        /// @brief Reset the agent to idle state
        void Reset() {
            State = Navigation::AgentState::Idle;
            Velocity = glm::vec3(0.0f);
            DesiredVelocity = glm::vec3(0.0f);
            HasTarget = false;
            IsMoving = false;
            CurrentPath.clear();
            CurrentWaypointIndex = 0;
        }

        /// @brief Check if the agent can move
        bool CanMove() const {
            return State != Navigation::AgentState::Disabled &&
                   State != Navigation::AgentState::WaitingForPath;
        }

        /// @brief Get distance to current target
        float GetDistanceToTarget(const glm::vec3& currentPosition) const {
            if (!HasTarget) return 0.0f;
            return glm::length(TargetPosition - currentPosition);
        }

        /// @brief Check if at target
        bool IsAtTarget(const glm::vec3& currentPosition) const {
            return GetDistanceToTarget(currentPosition) <= StoppingDistance;
        }
    };

} // namespace ECS
} // namespace Core
