#pragma once

// Behavior Tree Decorator Nodes
// Single-child nodes that modify the behavior of their child

#include "BehaviorTree.h"
#include "Blackboard.h"

namespace Core {
namespace AI {

    // ============================================================================
    // InverterNode - Invert child result
    // Success becomes Failure, Failure becomes Success, Running stays Running
    // ============================================================================
    class InverterNode : public BTNode {
    public:
        explicit InverterNode(const std::string& name = "Inverter")
            : BTNode(BTNodeType::Inverter, name) {}

        BTStatus Tick(float deltaTime, Blackboard& blackboard) override {
            if (m_Children.empty() || !m_Children[0]) {
                SetLastStatus(BTStatus::Failure);
                return BTStatus::Failure;
            }

            BTStatus childStatus = m_Children[0]->Tick(deltaTime, blackboard);

            switch (childStatus) {
                case BTStatus::Success:
                    SetLastStatus(BTStatus::Failure);
                    return BTStatus::Failure;
                case BTStatus::Failure:
                    SetLastStatus(BTStatus::Success);
                    return BTStatus::Success;
                case BTStatus::Running:
                    SetLastStatus(BTStatus::Running);
                    SetLastTickTime(deltaTime);
                    return BTStatus::Running;
            }

            SetLastStatus(BTStatus::Failure);
            return BTStatus::Failure;
        }
    };

    // ============================================================================
    // SucceederNode - Always return success regardless of child result
    // Useful for optional branches that should not fail the parent
    // ============================================================================
    class SucceederNode : public BTNode {
    public:
        explicit SucceederNode(const std::string& name = "Succeeder")
            : BTNode(BTNodeType::Succeeder, name) {}

        BTStatus Tick(float deltaTime, Blackboard& blackboard) override {
            if (m_Children.empty() || !m_Children[0]) {
                SetLastStatus(BTStatus::Success);
                return BTStatus::Success;
            }

            BTStatus childStatus = m_Children[0]->Tick(deltaTime, blackboard);

            if (childStatus == BTStatus::Running) {
                SetLastStatus(BTStatus::Running);
                SetLastTickTime(deltaTime);
                return BTStatus::Running;
            }

            // Always succeed when child completes
            SetLastStatus(BTStatus::Success);
            return BTStatus::Success;
        }
    };

    // ============================================================================
    // FailerNode - Always return failure regardless of child result
    // Useful for testing or forcing failure states
    // ============================================================================
    class FailerNode : public BTNode {
    public:
        explicit FailerNode(const std::string& name = "Failer")
            : BTNode(BTNodeType::Failer, name) {}

        BTStatus Tick(float deltaTime, Blackboard& blackboard) override {
            if (m_Children.empty() || !m_Children[0]) {
                SetLastStatus(BTStatus::Failure);
                return BTStatus::Failure;
            }

            BTStatus childStatus = m_Children[0]->Tick(deltaTime, blackboard);

            if (childStatus == BTStatus::Running) {
                SetLastStatus(BTStatus::Running);
                SetLastTickTime(deltaTime);
                return BTStatus::Running;
            }

            // Always fail when child completes
            SetLastStatus(BTStatus::Failure);
            return BTStatus::Failure;
        }
    };

    // ============================================================================
    // RepeaterNode - Repeat child N times or forever
    // RepeatCount of -1 means infinite repetition
    // ============================================================================
    class RepeaterNode : public BTNode {
    public:
        explicit RepeaterNode(int32_t repeatCount = -1, const std::string& name = "Repeater")
            : BTNode(BTNodeType::Repeater, name)
            , m_RepeatCount(repeatCount)
            , m_CurrentCount(0) {}

        BTStatus Tick(float deltaTime, Blackboard& blackboard) override {
            if (m_Children.empty() || !m_Children[0]) {
                SetLastStatus(BTStatus::Success);
                return BTStatus::Success;
            }

            // Infinite repetition
            if (m_RepeatCount < 0) {
                BTStatus childStatus = m_Children[0]->Tick(deltaTime, blackboard);
                
                if (childStatus == BTStatus::Running) {
                    SetLastStatus(BTStatus::Running);
                    SetLastTickTime(deltaTime);
                    return BTStatus::Running;
                }

                // Reset child and keep running
                m_Children[0]->Reset();
                SetLastStatus(BTStatus::Running);
                return BTStatus::Running;
            }

            // Finite repetition
            while (m_CurrentCount < static_cast<size_t>(m_RepeatCount)) {
                BTStatus childStatus = m_Children[0]->Tick(deltaTime, blackboard);

                if (childStatus == BTStatus::Running) {
                    SetLastStatus(BTStatus::Running);
                    SetLastTickTime(deltaTime);
                    return BTStatus::Running;
                }

                // Child completed, increment count
                ++m_CurrentCount;
                m_Children[0]->Reset();
            }

            // All repetitions complete
            Reset();
            SetLastStatus(BTStatus::Success);
            return BTStatus::Success;
        }

        void Reset() override {
            BTNode::Reset();
            m_CurrentCount = 0;
        }

        // Configuration
        void SetRepeatCount(int32_t count) { m_RepeatCount = count; }
        int32_t GetRepeatCount() const { return m_RepeatCount; }
        size_t GetCurrentCount() const { return m_CurrentCount; }

        Json ToJson() const override {
            Json j = BTNode::ToJson();
            j["repeatCount"] = m_RepeatCount;
            return j;
        }

    private:
        int32_t m_RepeatCount;  // -1 = infinite
        size_t m_CurrentCount;
    };

    // ============================================================================
    // RepeatUntilFailNode - Repeat until child returns failure
    // Succeeds when child eventually fails, keeps running otherwise
    // ============================================================================
    class RepeatUntilFailNode : public BTNode {
    public:
        explicit RepeatUntilFailNode(const std::string& name = "RepeatUntilFail")
            : BTNode(BTNodeType::RepeatUntilFail, name) {}

        BTStatus Tick(float deltaTime, Blackboard& blackboard) override {
            if (m_Children.empty() || !m_Children[0]) {
                SetLastStatus(BTStatus::Success);
                return BTStatus::Success;
            }

            BTStatus childStatus = m_Children[0]->Tick(deltaTime, blackboard);

            if (childStatus == BTStatus::Running) {
                SetLastStatus(BTStatus::Running);
                SetLastTickTime(deltaTime);
                return BTStatus::Running;
            }

            if (childStatus == BTStatus::Failure) {
                // Child failed, we succeed
                SetLastStatus(BTStatus::Success);
                return BTStatus::Success;
            }

            // Child succeeded, repeat
            m_Children[0]->Reset();
            SetLastStatus(BTStatus::Running);
            return BTStatus::Running;
        }
    };

    // ============================================================================
    // CooldownNode - Add cooldown between child executions
    // Fails during cooldown period, allows execution when ready
    // ============================================================================
    class CooldownNode : public BTNode {
    public:
        explicit CooldownNode(float cooldownSeconds, const std::string& name = "Cooldown")
            : BTNode(BTNodeType::Cooldown, name)
            , m_CooldownSeconds(cooldownSeconds)
            , m_Timer(0.0f)
            , m_OnCooldown(false)
            , m_ChildRunning(false) {}

        BTStatus Tick(float deltaTime, Blackboard& blackboard) override {
            // Update cooldown timer
            if (m_OnCooldown) {
                m_Timer -= deltaTime;
                if (m_Timer <= 0.0f) {
                    m_OnCooldown = false;
                    m_Timer = 0.0f;
                } else {
                    SetLastStatus(BTStatus::Failure);
                    return BTStatus::Failure;
                }
            }

            if (m_Children.empty() || !m_Children[0]) {
                SetLastStatus(BTStatus::Failure);
                return BTStatus::Failure;
            }

            BTStatus childStatus = m_Children[0]->Tick(deltaTime, blackboard);

            if (childStatus == BTStatus::Running) {
                m_ChildRunning = true;
                SetLastStatus(BTStatus::Running);
                SetLastTickTime(deltaTime);
                return BTStatus::Running;
            }

            // Child completed, start cooldown
            if (m_ChildRunning || !m_OnCooldown) {
                m_OnCooldown = true;
                m_Timer = m_CooldownSeconds;
                m_ChildRunning = false;
                m_Children[0]->Reset();
            }

            SetLastStatus(childStatus);
            return childStatus;
        }

        void Reset() override {
            BTNode::Reset();
            m_Timer = 0.0f;
            m_OnCooldown = false;
            m_ChildRunning = false;
        }

        // Configuration
        void SetCooldownSeconds(float seconds) { m_CooldownSeconds = seconds; }
        float GetCooldownSeconds() const { return m_CooldownSeconds; }
        float GetRemainingCooldown() const { return m_OnCooldown ? m_Timer : 0.0f; }
        bool IsOnCooldown() const { return m_OnCooldown; }

        Json ToJson() const override {
            Json j = BTNode::ToJson();
            j["cooldownSeconds"] = m_CooldownSeconds;
            return j;
        }

    private:
        float m_CooldownSeconds;
        float m_Timer;
        bool m_OnCooldown;
        bool m_ChildRunning;
    };

    // ============================================================================
    // TimeLimitNode - Fail if child takes too long
    // Useful for preventing stuck states
    // ============================================================================
    class TimeLimitNode : public BTNode {
    public:
        explicit TimeLimitNode(float timeLimitSeconds, const std::string& name = "TimeLimit")
            : BTNode(BTNodeType::TimeLimit, name)
            , m_TimeLimitSeconds(timeLimitSeconds)
            , m_Timer(0.0f) {}

        BTStatus Tick(float deltaTime, Blackboard& blackboard) override {
            if (m_Children.empty() || !m_Children[0]) {
                SetLastStatus(BTStatus::Failure);
                return BTStatus::Failure;
            }

            m_Timer += deltaTime;

            if (m_Timer >= m_TimeLimitSeconds) {
                // Time limit exceeded, fail
                Reset();
                SetLastStatus(BTStatus::Failure);
                return BTStatus::Failure;
            }

            BTStatus childStatus = m_Children[0]->Tick(deltaTime, blackboard);

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
            m_Timer = 0.0f;
        }

        // Configuration
        void SetTimeLimitSeconds(float seconds) { m_TimeLimitSeconds = seconds; }
        float GetTimeLimitSeconds() const { return m_TimeLimitSeconds; }
        float GetElapsedTime() const { return m_Timer; }

        Json ToJson() const override {
            Json j = BTNode::ToJson();
            j["timeLimitSeconds"] = m_TimeLimitSeconds;
            return j;
        }

    private:
        float m_TimeLimitSeconds;
        float m_Timer;
    };

} // namespace AI
} // namespace Core
