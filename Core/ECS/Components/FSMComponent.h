#pragma once

// FSM Component
// ECS component that attaches a finite state machine to an entity

#include "Core/AI/FSM/FSM.h"
#include <string>
#include <memory>

namespace Core {
namespace ECS {

    /// @brief Component for entities controlled by a finite state machine
    struct FSMComponent {
        // =====================================================================
        // FSM Configuration
        // =====================================================================

        /// ID of the FSM template to use (from FSMManager)
        std::string TemplateId;

        /// Name of this FSM instance (for debugging)
        std::string InstanceName;

        /// Whether this FSM is active
        bool IsActive = true;

        /// Update frequency in Hz (0 = every frame)
        float UpdateFrequency = 0.0f;

        // =====================================================================
        // Runtime State
        // =====================================================================

        /// The actual FSM instance (created at runtime)
        std::shared_ptr<AI::FSM> FSMInstance;

        /// Time accumulator for frequency-limited updates
        float UpdateAccumulator = 0.0f;

        /// Whether the FSM has been started
        bool IsStarted = false;

        /// Number of updates executed
        uint32_t UpdateCount = 0;

        // =====================================================================
        // Debug Settings
        // =====================================================================

        /// Enable debug visualization
        bool EnableDebug = false;

        /// Record state history for debugging
        bool RecordHistory = false;

        /// Maximum history entries to keep
        uint32_t MaxHistoryEntries = 50;

        /// State transition history
        std::vector<std::string> StateHistory;

        // =====================================================================
        // Helper Methods
        // =====================================================================

        /// @brief Check if the FSM should be updated this frame
        bool ShouldUpdate(float deltaTime) {
            if (!IsActive || !FSMInstance) return false;

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

        /// @brief Start the FSM
        void Start() {
            if (FSMInstance && !IsStarted) {
                FSMInstance->Start();
                IsStarted = true;

                if (RecordHistory) {
                    RecordCurrentState();
                }
            }
        }

        /// @brief Stop the FSM
        void Stop() {
            if (FSMInstance && IsStarted) {
                FSMInstance->Stop();
                IsStarted = false;
            }
        }

        /// @brief Update the FSM
        void Update(float deltaTime) {
            if (!FSMInstance || !IsStarted) return;

            std::string prevState = FSMInstance->GetCurrentStateName();
            FSMInstance->Update(deltaTime);
            UpdateCount++;

            // Record state change
            if (RecordHistory && FSMInstance->GetCurrentStateName() != prevState) {
                RecordCurrentState();
            }
        }

        /// @brief Send an event to the FSM
        void SendEvent(const std::string& eventName) {
            if (FSMInstance && IsStarted) {
                FSMInstance->SendEvent(eventName);
            }
        }

        /// @brief Force transition to a specific state
        bool ForceTransition(const std::string& stateName) {
            if (FSMInstance && IsStarted) {
                bool result = FSMInstance->ForceTransition(stateName);
                if (result && RecordHistory) {
                    RecordCurrentState();
                }
                return result;
            }
            return false;
        }

        /// @brief Get current state name
        std::string GetCurrentStateName() const {
            return FSMInstance ? FSMInstance->GetCurrentStateName() : "";
        }

        /// @brief Check if in a specific state
        bool IsInState(const std::string& stateName) const {
            return FSMInstance && FSMInstance->IsInState(stateName);
        }

        /// @brief Reset the FSM
        void Reset() {
            if (FSMInstance) {
                FSMInstance->Stop();
                FSMInstance->Start();
            }
            UpdateCount = 0;
            UpdateAccumulator = 0.0f;
            StateHistory.clear();
        }

        /// @brief Get the blackboard from the FSM
        AI::Blackboard* GetBlackboard() {
            return FSMInstance ? &FSMInstance->GetBlackboard() : nullptr;
        }

        /// @brief Get the blackboard from the FSM (const)
        const AI::Blackboard* GetBlackboard() const {
            return FSMInstance ? &FSMInstance->GetBlackboard() : nullptr;
        }

    private:
        void RecordCurrentState() {
            if (!FSMInstance) return;
            
            StateHistory.push_back(FSMInstance->GetCurrentStateName());

            // Trim history if too long
            while (StateHistory.size() > MaxHistoryEntries) {
                StateHistory.erase(StateHistory.begin());
            }
        }
    };

} // namespace ECS
} // namespace Core
