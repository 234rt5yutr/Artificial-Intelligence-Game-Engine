#pragma once

// Behavior Tree Composite Nodes
// Control flow nodes with multiple children

#include "BehaviorTree.h"
#include "Blackboard.h"
#include <random>

namespace Core {
namespace AI {

    // ============================================================================
    // SequenceNode - Execute children in order, fail on first failure
    // Like a logical AND: all children must succeed for the sequence to succeed
    // ============================================================================
    class SequenceNode : public BTNode {
    public:
        explicit SequenceNode(const std::string& name = "Sequence")
            : BTNode(BTNodeType::Sequence, name)
            , m_CurrentChildIndex(0) {}

        BTStatus Tick(float deltaTime, Blackboard& blackboard) override {
            if (m_Children.empty()) {
                SetLastStatus(BTStatus::Success);
                return BTStatus::Success;
            }

            // Resume from last running child or start from beginning
            while (m_CurrentChildIndex < m_Children.size()) {
                auto& child = m_Children[m_CurrentChildIndex];
                if (!child) {
                    ++m_CurrentChildIndex;
                    continue;
                }

                BTStatus childStatus = child->Tick(deltaTime, blackboard);

                if (childStatus == BTStatus::Running) {
                    SetLastStatus(BTStatus::Running);
                    SetLastTickTime(deltaTime);
                    return BTStatus::Running;
                }

                if (childStatus == BTStatus::Failure) {
                    // Fail-fast: sequence fails on first child failure
                    Reset();
                    SetLastStatus(BTStatus::Failure);
                    return BTStatus::Failure;
                }

                // Child succeeded, move to next
                ++m_CurrentChildIndex;
            }

            // All children succeeded
            Reset();
            SetLastStatus(BTStatus::Success);
            return BTStatus::Success;
        }

        void Reset() override {
            BTNode::Reset();
            m_CurrentChildIndex = 0;
        }

    private:
        size_t m_CurrentChildIndex;
    };

    // ============================================================================
    // SelectorNode - Execute children in order, succeed on first success
    // Like a logical OR: any child succeeding makes the selector succeed
    // ============================================================================
    class SelectorNode : public BTNode {
    public:
        explicit SelectorNode(const std::string& name = "Selector")
            : BTNode(BTNodeType::Selector, name)
            , m_CurrentChildIndex(0) {}

        BTStatus Tick(float deltaTime, Blackboard& blackboard) override {
            if (m_Children.empty()) {
                SetLastStatus(BTStatus::Failure);
                return BTStatus::Failure;
            }

            // Resume from last running child or start from beginning
            while (m_CurrentChildIndex < m_Children.size()) {
                auto& child = m_Children[m_CurrentChildIndex];
                if (!child) {
                    ++m_CurrentChildIndex;
                    continue;
                }

                BTStatus childStatus = child->Tick(deltaTime, blackboard);

                if (childStatus == BTStatus::Running) {
                    SetLastStatus(BTStatus::Running);
                    SetLastTickTime(deltaTime);
                    return BTStatus::Running;
                }

                if (childStatus == BTStatus::Success) {
                    // Succeed-fast: selector succeeds on first child success
                    Reset();
                    SetLastStatus(BTStatus::Success);
                    return BTStatus::Success;
                }

                // Child failed, try next
                ++m_CurrentChildIndex;
            }

            // All children failed
            Reset();
            SetLastStatus(BTStatus::Failure);
            return BTStatus::Failure;
        }

        void Reset() override {
            BTNode::Reset();
            m_CurrentChildIndex = 0;
        }

    private:
        size_t m_CurrentChildIndex;
    };

    // ============================================================================
    // ParallelNode - Execute all children simultaneously
    // Supports configurable success and failure policies
    // ============================================================================
    class ParallelNode : public BTNode {
    public:
        // Policy for determining overall success/failure
        enum class Policy {
            RequireOne,     // Succeed/fail if any child succeeds/fails
            RequireAll      // Succeed/fail only if all children succeed/fail
        };

        explicit ParallelNode(
            Policy successPolicy = Policy::RequireAll,
            Policy failurePolicy = Policy::RequireOne,
            const std::string& name = "Parallel")
            : BTNode(BTNodeType::Parallel, name)
            , m_SuccessPolicy(successPolicy)
            , m_FailurePolicy(failurePolicy) {}

        BTStatus Tick(float deltaTime, Blackboard& blackboard) override {
            if (m_Children.empty()) {
                SetLastStatus(BTStatus::Success);
                return BTStatus::Success;
            }

            size_t successCount = 0;
            size_t failureCount = 0;
            size_t runningCount = 0;

            for (auto& child : m_Children) {
                if (!child) continue;

                // Skip already completed children on subsequent ticks
                if (child->GetLastStatus() == BTStatus::Success && m_ChildCompleted[child.get()]) {
                    ++successCount;
                    continue;
                }
                if (child->GetLastStatus() == BTStatus::Failure && m_ChildCompleted[child.get()]) {
                    ++failureCount;
                    continue;
                }

                BTStatus childStatus = child->Tick(deltaTime, blackboard);

                switch (childStatus) {
                    case BTStatus::Success:
                        ++successCount;
                        m_ChildCompleted[child.get()] = true;
                        break;
                    case BTStatus::Failure:
                        ++failureCount;
                        m_ChildCompleted[child.get()] = true;
                        break;
                    case BTStatus::Running:
                        ++runningCount;
                        break;
                }
            }

            size_t totalChildren = m_Children.size();

            // Check failure condition first
            bool shouldFail = (m_FailurePolicy == Policy::RequireOne && failureCount > 0) ||
                              (m_FailurePolicy == Policy::RequireAll && failureCount == totalChildren);
            if (shouldFail) {
                Reset();
                SetLastStatus(BTStatus::Failure);
                return BTStatus::Failure;
            }

            // Check success condition
            bool shouldSucceed = (m_SuccessPolicy == Policy::RequireOne && successCount > 0) ||
                                 (m_SuccessPolicy == Policy::RequireAll && successCount == totalChildren);
            if (shouldSucceed && runningCount == 0) {
                Reset();
                SetLastStatus(BTStatus::Success);
                return BTStatus::Success;
            }

            // Still running
            SetLastStatus(BTStatus::Running);
            SetLastTickTime(deltaTime);
            return BTStatus::Running;
        }

        void Reset() override {
            BTNode::Reset();
            m_ChildCompleted.clear();
        }

        // Configuration
        void SetSuccessPolicy(Policy policy) { m_SuccessPolicy = policy; }
        void SetFailurePolicy(Policy policy) { m_FailurePolicy = policy; }
        Policy GetSuccessPolicy() const { return m_SuccessPolicy; }
        Policy GetFailurePolicy() const { return m_FailurePolicy; }

        Json ToJson() const override {
            Json j = BTNode::ToJson();
            j["successPolicy"] = static_cast<int>(m_SuccessPolicy);
            j["failurePolicy"] = static_cast<int>(m_FailurePolicy);
            return j;
        }

    private:
        Policy m_SuccessPolicy;
        Policy m_FailurePolicy;
        std::unordered_map<BTNode*, bool> m_ChildCompleted;
    };

    // ============================================================================
    // RandomSelectorNode - Select a random child to execute
    // Optionally supports weighted random selection
    // ============================================================================
    class RandomSelectorNode : public BTNode {
    public:
        explicit RandomSelectorNode(const std::string& name = "RandomSelector")
            : BTNode(BTNodeType::RandomSelector, name)
            , m_SelectedChildIndex(SIZE_MAX)
            , m_Generator(std::random_device{}()) {}

        BTStatus Tick(float deltaTime, Blackboard& blackboard) override {
            if (m_Children.empty()) {
                SetLastStatus(BTStatus::Failure);
                return BTStatus::Failure;
            }

            // Select a child if not already selected
            if (m_SelectedChildIndex == SIZE_MAX) {
                m_SelectedChildIndex = SelectRandomChild();
            }

            if (m_SelectedChildIndex >= m_Children.size() || !m_Children[m_SelectedChildIndex]) {
                Reset();
                SetLastStatus(BTStatus::Failure);
                return BTStatus::Failure;
            }

            BTStatus childStatus = m_Children[m_SelectedChildIndex]->Tick(deltaTime, blackboard);

            if (childStatus == BTStatus::Running) {
                SetLastStatus(BTStatus::Running);
                SetLastTickTime(deltaTime);
                return BTStatus::Running;
            }

            Reset();
            SetLastStatus(childStatus);
            return childStatus;
        }

        void Reset() override {
            BTNode::Reset();
            m_SelectedChildIndex = SIZE_MAX;
        }

        // Set weights for weighted random selection
        // Weights correspond to children by index
        void SetWeights(const std::vector<float>& weights) {
            m_Weights = weights;
        }

    private:
        size_t m_SelectedChildIndex;
        std::vector<float> m_Weights;
        std::mt19937 m_Generator;

        size_t SelectRandomChild() {
            if (m_Children.empty()) return SIZE_MAX;

            if (m_Weights.empty() || m_Weights.size() != m_Children.size()) {
                // Uniform random selection
                std::uniform_int_distribution<size_t> dist(0, m_Children.size() - 1);
                return dist(m_Generator);
            }

            // Weighted random selection
            float totalWeight = 0.0f;
            for (float w : m_Weights) {
                totalWeight += w;
            }

            if (totalWeight <= 0.0f) {
                std::uniform_int_distribution<size_t> dist(0, m_Children.size() - 1);
                return dist(m_Generator);
            }

            std::uniform_real_distribution<float> dist(0.0f, totalWeight);
            float value = dist(m_Generator);

            float cumulative = 0.0f;
            for (size_t i = 0; i < m_Weights.size(); ++i) {
                cumulative += m_Weights[i];
                if (value <= cumulative) {
                    return i;
                }
            }

            return m_Children.size() - 1;
        }
    };

} // namespace AI
} // namespace Core
