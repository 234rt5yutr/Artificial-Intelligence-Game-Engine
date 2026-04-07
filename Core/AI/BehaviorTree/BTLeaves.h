#pragma once

// Behavior Tree Leaf Nodes
// Actual work nodes that perform actions or check conditions

#include "BehaviorTree.h"
#include "Blackboard.h"
#include "../Log.h"
#include <functional>

namespace Core {
namespace AI {

    // ============================================================================
    // ActionNode - Execute a custom action function
    // The action returns BTStatus to control execution flow
    // ============================================================================
    class ActionNode : public BTNode {
    public:
        using ActionFunc = std::function<BTStatus(float deltaTime, Blackboard&)>;

        explicit ActionNode(ActionFunc action, const std::string& name = "Action")
            : BTNode(BTNodeType::Action, name)
            , m_Action(std::move(action)) {}

        BTStatus Tick(float deltaTime, Blackboard& blackboard) override {
            if (!m_Action) {
                SetLastStatus(BTStatus::Failure);
                return BTStatus::Failure;
            }

            BTStatus status = m_Action(deltaTime, blackboard);
            SetLastStatus(status);
            SetLastTickTime(deltaTime);
            return status;
        }

        // Set a new action function
        void SetAction(ActionFunc action) { m_Action = std::move(action); }

    private:
        ActionFunc m_Action;
    };

    // ============================================================================
    // ConditionNode - Check a condition function
    // Returns Success if condition is true, Failure if false
    // ============================================================================
    class ConditionNode : public BTNode {
    public:
        using ConditionFunc = std::function<bool(const Blackboard&)>;

        explicit ConditionNode(ConditionFunc condition, const std::string& name = "Condition")
            : BTNode(BTNodeType::Condition, name)
            , m_Condition(std::move(condition)) {}

        BTStatus Tick([[maybe_unused]] float deltaTime, Blackboard& blackboard) override {
            if (!m_Condition) {
                SetLastStatus(BTStatus::Failure);
                return BTStatus::Failure;
            }

            bool result = m_Condition(blackboard);
            BTStatus status = result ? BTStatus::Success : BTStatus::Failure;
            SetLastStatus(status);
            return status;
        }

        // Set a new condition function
        void SetCondition(ConditionFunc condition) { m_Condition = std::move(condition); }

    private:
        ConditionFunc m_Condition;
    };

    // ============================================================================
    // WaitNode - Wait for a specified duration
    // Returns Running until the duration has elapsed
    // ============================================================================
    class WaitNode : public BTNode {
    public:
        explicit WaitNode(float waitSeconds, const std::string& name = "Wait")
            : BTNode(BTNodeType::Wait, name)
            , m_WaitSeconds(waitSeconds)
            , m_Timer(0.0f) {}

        BTStatus Tick(float deltaTime, [[maybe_unused]] Blackboard& blackboard) override {
            m_Timer += deltaTime;

            if (m_Timer >= m_WaitSeconds) {
                Reset();
                SetLastStatus(BTStatus::Success);
                return BTStatus::Success;
            }

            SetLastStatus(BTStatus::Running);
            SetLastTickTime(deltaTime);
            return BTStatus::Running;
        }

        void Reset() override {
            BTNode::Reset();
            m_Timer = 0.0f;
        }

        // Configuration
        void SetWaitSeconds(float seconds) { m_WaitSeconds = seconds; }
        float GetWaitSeconds() const { return m_WaitSeconds; }
        float GetElapsedTime() const { return m_Timer; }
        float GetRemainingTime() const { return m_WaitSeconds - m_Timer; }

        Json ToJson() const override {
            Json j = BTNode::ToJson();
            j["waitSeconds"] = m_WaitSeconds;
            return j;
        }

    private:
        float m_WaitSeconds;
        float m_Timer;
    };

    // ============================================================================
    // LogNode - Debug logging node
    // Always succeeds after logging the message
    // ============================================================================
    class LogNode : public BTNode {
    public:
        enum class LogLevel {
            Debug,
            Info,
            Warning,
            Error
        };

        explicit LogNode(const std::string& message, LogLevel level = LogLevel::Info, 
                        const std::string& name = "Log")
            : BTNode(BTNodeType::Log, name)
            , m_Message(message)
            , m_Level(level)
            , m_LogOnce(false)
            , m_HasLogged(false) {}

        BTStatus Tick([[maybe_unused]] float deltaTime, Blackboard& blackboard) override {
            if (m_LogOnce && m_HasLogged) {
                SetLastStatus(BTStatus::Success);
                return BTStatus::Success;
            }

            // Expand blackboard variables in message
            std::string expandedMessage = ExpandMessage(m_Message, blackboard);

            // Log based on level
            switch (m_Level) {
                case LogLevel::Debug:
                    LOG_DEBUG("[BT:{}] {}", GetName(), expandedMessage);
                    break;
                case LogLevel::Info:
                    LOG_INFO("[BT:{}] {}", GetName(), expandedMessage);
                    break;
                case LogLevel::Warning:
                    LOG_WARN("[BT:{}] {}", GetName(), expandedMessage);
                    break;
                case LogLevel::Error:
                    LOG_ERROR("[BT:{}] {}", GetName(), expandedMessage);
                    break;
            }

            m_HasLogged = true;
            SetLastStatus(BTStatus::Success);
            return BTStatus::Success;
        }

        void Reset() override {
            BTNode::Reset();
            m_HasLogged = false;
        }

        // Configuration
        void SetMessage(const std::string& message) { m_Message = message; }
        const std::string& GetMessage() const { return m_Message; }
        void SetLogLevel(LogLevel level) { m_Level = level; }
        LogLevel GetLogLevel() const { return m_Level; }
        void SetLogOnce(bool logOnce) { m_LogOnce = logOnce; }
        bool GetLogOnce() const { return m_LogOnce; }

        Json ToJson() const override {
            Json j = BTNode::ToJson();
            j["message"] = m_Message;
            j["level"] = static_cast<int>(m_Level);
            j["logOnce"] = m_LogOnce;
            return j;
        }

    private:
        std::string m_Message;
        LogLevel m_Level;
        bool m_LogOnce;
        bool m_HasLogged;

        // Expand ${key} variables from blackboard
        static std::string ExpandMessage(const std::string& message, const Blackboard& blackboard) {
            std::string result = message;
            size_t start = 0;

            while ((start = result.find("${", start)) != std::string::npos) {
                size_t end = result.find('}', start);
                if (end == std::string::npos) break;

                std::string key = result.substr(start + 2, end - start - 2);
                std::string value = blackboard.GetValueAsString(key);
                
                result.replace(start, end - start + 1, value);
                start += value.length();
            }

            return result;
        }
    };

    // ============================================================================
    // SubTreeNode - Reference another behavior tree
    // Allows for modular tree composition
    // ============================================================================
    class SubTreeNode : public BTNode {
    public:
        using TreeLookupFunc = std::function<class BehaviorTree*(const std::string&)>;

        explicit SubTreeNode(const std::string& treeId, TreeLookupFunc lookupFunc = nullptr,
                            const std::string& name = "SubTree")
            : BTNode(BTNodeType::SubTree, name)
            , m_TreeId(treeId)
            , m_LookupFunc(std::move(lookupFunc))
            , m_CachedTree(nullptr) {}

        BTStatus Tick(float deltaTime, Blackboard& blackboard) override;  // Implemented in BehaviorTreeManager

        void Reset() override {
            BTNode::Reset();
            // Reset cached tree if needed
            if (m_CachedTree) {
                // Tree reset is handled by BehaviorTree::Reset()
            }
        }

        // Configuration
        void SetTreeId(const std::string& treeId) { 
            m_TreeId = treeId; 
            m_CachedTree = nullptr;
        }
        const std::string& GetTreeId() const { return m_TreeId; }
        
        void SetLookupFunc(TreeLookupFunc func) { m_LookupFunc = std::move(func); }

        Json ToJson() const override {
            Json j = BTNode::ToJson();
            j["treeId"] = m_TreeId;
            return j;
        }

    private:
        std::string m_TreeId;
        TreeLookupFunc m_LookupFunc;
        class BehaviorTree* m_CachedTree;

        friend class BehaviorTreeManager;
    };

    // ============================================================================
    // BlackboardConditionNode - Check blackboard value against a value
    // Supports various comparison operators
    // ============================================================================
    class BlackboardConditionNode : public BTNode {
    public:
        template<typename T>
        BlackboardConditionNode(const std::string& key, ComparisonOp op, const T& value,
                               const std::string& name = "BlackboardCondition")
            : BTNode(BTNodeType::Condition, name)
            , m_Key(key)
            , m_Operator(op)
            , m_Value(value) {}

        BTStatus Tick([[maybe_unused]] float deltaTime, Blackboard& blackboard) override {
            bool result = EvaluateCondition(blackboard);
            BTStatus status = result ? BTStatus::Success : BTStatus::Failure;
            SetLastStatus(status);
            return status;
        }

        Json ToJson() const override {
            Json j = BTNode::ToJson();
            j["key"] = m_Key;
            j["operator"] = static_cast<int>(m_Operator);
            return j;
        }

    private:
        std::string m_Key;
        ComparisonOp m_Operator;
        std::any m_Value;

        bool EvaluateCondition(const Blackboard& blackboard) const {
            if (!blackboard.Has(m_Key)) return false;

            // Try different types for comparison
            if (m_Value.type() == typeid(bool)) {
                auto bbVal = blackboard.TryGet<bool>(m_Key);
                if (bbVal) return Compare(*bbVal, std::any_cast<bool>(m_Value));
            }
            else if (m_Value.type() == typeid(int)) {
                auto bbVal = blackboard.TryGet<int>(m_Key);
                if (bbVal) return Compare(*bbVal, std::any_cast<int>(m_Value));
            }
            else if (m_Value.type() == typeid(float)) {
                auto bbVal = blackboard.TryGet<float>(m_Key);
                if (bbVal) return Compare(*bbVal, std::any_cast<float>(m_Value));
            }
            else if (m_Value.type() == typeid(std::string)) {
                auto bbVal = blackboard.TryGet<std::string>(m_Key);
                if (bbVal) return Compare(*bbVal, std::any_cast<std::string>(m_Value));
            }

            return false;
        }

        template<typename T>
        bool Compare(const T& a, const T& b) const {
            switch (m_Operator) {
                case ComparisonOp::Equal: return a == b;
                case ComparisonOp::NotEqual: return a != b;
                case ComparisonOp::LessThan: return a < b;
                case ComparisonOp::LessOrEqual: return a <= b;
                case ComparisonOp::GreaterThan: return a > b;
                case ComparisonOp::GreaterOrEqual: return a >= b;
            }
            return false;
        }
    };

    // ============================================================================
    // SetBlackboardNode - Set a value in the blackboard
    // Always succeeds after setting the value
    // ============================================================================
    class SetBlackboardNode : public BTNode {
    public:
        template<typename T>
        SetBlackboardNode(const std::string& key, const T& value, const std::string& name = "SetBlackboard")
            : BTNode(BTNodeType::Action, name)
            , m_Key(key)
            , m_Value(value) {}

        BTStatus Tick([[maybe_unused]] float deltaTime, Blackboard& blackboard) override {
            // Set value based on type
            if (m_Value.type() == typeid(bool)) {
                blackboard.Set(m_Key, std::any_cast<bool>(m_Value));
            }
            else if (m_Value.type() == typeid(int)) {
                blackboard.Set(m_Key, std::any_cast<int>(m_Value));
            }
            else if (m_Value.type() == typeid(float)) {
                blackboard.Set(m_Key, std::any_cast<float>(m_Value));
            }
            else if (m_Value.type() == typeid(std::string)) {
                blackboard.Set(m_Key, std::any_cast<std::string>(m_Value));
            }
            else if (m_Value.type() == typeid(glm::vec3)) {
                blackboard.Set(m_Key, std::any_cast<glm::vec3>(m_Value));
            }

            SetLastStatus(BTStatus::Success);
            return BTStatus::Success;
        }

    private:
        std::string m_Key;
        std::any m_Value;
    };

} // namespace AI
} // namespace Core
