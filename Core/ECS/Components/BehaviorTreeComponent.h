#pragma once

// Behavior Tree Component
// ECS component that attaches a behavior tree to an entity

#include "Core/AI/BehaviorTree/BehaviorTreeContainer.h"
#include <string>
#include <memory>

namespace Core {
namespace ECS {

    /// @brief Component for entities controlled by a behavior tree
    struct BehaviorTreeComponent {
        // =====================================================================
        // Tree Configuration
        // =====================================================================

        /// ID of the behavior tree template to use (from BehaviorTreeManager)
        std::string TemplateId;

        /// Name of this behavior tree instance (for debugging)
        std::string InstanceName;

        /// Whether this behavior tree is active
        bool IsActive = true;

        /// Update frequency in Hz (0 = every frame)
        float UpdateFrequency = 0.0f;

        // =====================================================================
        // Runtime State
        // =====================================================================

        /// The actual behavior tree instance (created at runtime)
        std::shared_ptr<AI::BehaviorTree> TreeInstance;

        /// Time accumulator for frequency-limited updates
        float UpdateAccumulator = 0.0f;

        /// Last tick result
        AI::BTStatus LastStatus = AI::BTStatus::Success;

        /// Number of ticks executed
        uint32_t TickCount = 0;

        /// Whether the tree needs to be reset on next update
        bool NeedsReset = false;

        // =====================================================================
        // Debug Settings
        // =====================================================================

        /// Enable debug visualization
        bool EnableDebug = false;

        /// Record execution history for debugging
        bool RecordHistory = false;

        /// Maximum history entries to keep
        uint32_t MaxHistoryEntries = 100;

        /// Execution history (node name -> status)
        std::vector<std::pair<std::string, AI::BTStatus>> ExecutionHistory;

        // =====================================================================
        // Helper Methods
        // =====================================================================

        /// @brief Check if the tree should be updated this frame
        bool ShouldUpdate(float deltaTime) {
            if (!IsActive || !TreeInstance) return false;

            if (UpdateFrequency <= 0.0f) {
                return true; // Update every frame
            }

            UpdateAccumulator += deltaTime;
            float interval = 1.0f / UpdateFrequency;

            if (UpdateAccumulator >= interval) {
                UpdateAccumulator -= interval;
                return true;
            }

            return false;
        }

        /// @brief Tick the behavior tree
        AI::BTStatus Tick(float deltaTime) {
            if (!TreeInstance) return AI::BTStatus::Failure;

            if (NeedsReset) {
                TreeInstance->Reset();
                NeedsReset = false;
            }

            LastStatus = TreeInstance->Tick(deltaTime);
            TickCount++;

            // Record to history if enabled
            if (RecordHistory && TreeInstance->GetRoot()) {
                RecordExecutionToHistory();
            }

            return LastStatus;
        }

        /// @brief Reset the behavior tree
        void Reset() {
            if (TreeInstance) {
                TreeInstance->Reset();
            }
            LastStatus = AI::BTStatus::Success;
            TickCount = 0;
            UpdateAccumulator = 0.0f;
            ExecutionHistory.clear();
        }

        /// @brief Get the blackboard from the tree
        AI::Blackboard* GetBlackboard() {
            return TreeInstance ? &TreeInstance->GetBlackboard() : nullptr;
        }

        /// @brief Get the blackboard from the tree (const)
        const AI::Blackboard* GetBlackboard() const {
            return TreeInstance ? &TreeInstance->GetBlackboard() : nullptr;
        }

        /// @brief Get currently active nodes (for visualization)
        std::vector<AI::BTNode*> GetActivePath() const {
            return TreeInstance ? TreeInstance->GetActivePath() : std::vector<AI::BTNode*>{};
        }

    private:
        void RecordExecutionToHistory() {
            auto activePath = TreeInstance->GetActivePath();
            for (auto* node : activePath) {
                ExecutionHistory.emplace_back(node->GetName(), node->GetStatus());
            }

            // Trim history if too long
            while (ExecutionHistory.size() > MaxHistoryEntries) {
                ExecutionHistory.erase(ExecutionHistory.begin());
            }
        }
    };

} // namespace ECS
} // namespace Core
