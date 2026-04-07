#pragma once

// Behavior Tree Framework
// Data-driven hierarchical AI decision-making system
// Supports composite nodes (Sequence, Selector, Parallel), decorator nodes (Inverter, Repeater, Cooldown),
// and leaf nodes (Action, Condition) with lambda support for custom behaviors

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <any>
#include <optional>
#include <nlohmann/json.hpp>

namespace Core {
namespace AI {

    using Json = nlohmann::json;

    // Forward declarations
    class BehaviorTree;
    class BTNode;
    class Blackboard;

    // Node execution status returned by Tick()
    enum class BTStatus : uint8_t {
        Success,    // Node completed successfully
        Failure,    // Node failed
        Running     // Node still executing (will be resumed next tick)
    };

    // Convert BTStatus to string for debugging
    inline const char* BTStatusToString(BTStatus status) {
        switch (status) {
            case BTStatus::Success: return "Success";
            case BTStatus::Failure: return "Failure";
            case BTStatus::Running: return "Running";
        }
        return "Unknown";
    }

    // Node type identifiers
    enum class BTNodeType : uint8_t {
        // Composites - control flow nodes with multiple children
        Sequence,           // Execute children in order, fail on first failure
        Selector,           // Execute children in order, succeed on first success
        Parallel,           // Execute all children simultaneously
        RandomSelector,     // Select random child to execute

        // Decorators - modify child behavior, single child
        Inverter,           // Invert child result (Success <-> Failure)
        Succeeder,          // Always return success regardless of child
        Failer,             // Always return failure regardless of child
        Repeater,           // Repeat child N times or forever
        RepeatUntilFail,    // Repeat until child returns failure
        Cooldown,           // Add cooldown between executions
        TimeLimit,          // Fail if child takes too long

        // Leaves - actual work nodes, no children
        Action,             // Execute custom action function
        Condition,          // Check condition function
        Wait,               // Wait for duration
        Log,                // Debug logging
        SubTree             // Reference another behavior tree
    };

    // Convert BTNodeType to string for debugging
    inline const char* BTNodeTypeToString(BTNodeType type) {
        switch (type) {
            case BTNodeType::Sequence: return "Sequence";
            case BTNodeType::Selector: return "Selector";
            case BTNodeType::Parallel: return "Parallel";
            case BTNodeType::RandomSelector: return "RandomSelector";
            case BTNodeType::Inverter: return "Inverter";
            case BTNodeType::Succeeder: return "Succeeder";
            case BTNodeType::Failer: return "Failer";
            case BTNodeType::Repeater: return "Repeater";
            case BTNodeType::RepeatUntilFail: return "RepeatUntilFail";
            case BTNodeType::Cooldown: return "Cooldown";
            case BTNodeType::TimeLimit: return "TimeLimit";
            case BTNodeType::Action: return "Action";
            case BTNodeType::Condition: return "Condition";
            case BTNodeType::Wait: return "Wait";
            case BTNodeType::Log: return "Log";
            case BTNodeType::SubTree: return "SubTree";
        }
        return "Unknown";
    }

    // Comparison operators for conditions
    enum class ComparisonOp : uint8_t {
        Equal,
        NotEqual,
        LessThan,
        LessOrEqual,
        GreaterThan,
        GreaterOrEqual
    };

    // Base class for all behavior tree nodes
    class BTNode {
    public:
        BTNode(BTNodeType type, const std::string& name = "")
            : m_Type(type)
            , m_Name(name.empty() ? BTNodeTypeToString(type) : name)
            , m_Parent(nullptr)
            , m_LastStatus(BTStatus::Success)
            , m_IsActive(false)
            , m_LastTickTime(0.0f)
            , m_NodeId(GenerateNodeId()) {}

        virtual ~BTNode() = default;

        // Non-copyable but movable
        BTNode(const BTNode&) = delete;
        BTNode& operator=(const BTNode&) = delete;
        BTNode(BTNode&&) = default;
        BTNode& operator=(BTNode&&) = default;

        // Core execution - called each frame while node is active
        virtual BTStatus Tick(float deltaTime, Blackboard& blackboard) = 0;

        // Reset node state for reuse
        virtual void Reset() {
            m_LastStatus = BTStatus::Success;
            m_IsActive = false;
            for (auto& child : m_Children) {
                if (child) child->Reset();
            }
        }

        // Called when node starts execution
        virtual void OnEnter([[maybe_unused]] Blackboard& blackboard) {
            m_IsActive = true;
        }

        // Called when node completes execution
        virtual void OnExit([[maybe_unused]] Blackboard& blackboard) {
            m_IsActive = false;
        }

        // Tree structure manipulation
        void AddChild(std::unique_ptr<BTNode> child) {
            if (child) {
                child->m_Parent = this;
                m_Children.push_back(std::move(child));
            }
        }

        void RemoveChild(size_t index) {
            if (index < m_Children.size()) {
                m_Children.erase(m_Children.begin() + static_cast<ptrdiff_t>(index));
            }
        }

        void ClearChildren() {
            m_Children.clear();
        }

        // Accessors
        BTNodeType GetType() const { return m_Type; }
        const std::string& GetName() const { return m_Name; }
        void SetName(const std::string& name) { m_Name = name; }
        
        BTStatus GetLastStatus() const { return m_LastStatus; }
        bool IsRunning() const { return m_LastStatus == BTStatus::Running; }
        bool IsActive() const { return m_IsActive; }
        float GetLastTickTime() const { return m_LastTickTime; }

        BTNode* GetParent() const { return m_Parent; }
        const std::vector<std::unique_ptr<BTNode>>& GetChildren() const { return m_Children; }
        std::vector<std::unique_ptr<BTNode>>& GetChildren() { return m_Children; }
        size_t GetChildCount() const { return m_Children.size(); }
        BTNode* GetChild(size_t index) const {
            return index < m_Children.size() ? m_Children[index].get() : nullptr;
        }

        uint32_t GetNodeId() const { return m_NodeId; }

        // Serialization support
        virtual Json ToJson() const {
            Json j;
            j["type"] = BTNodeTypeToString(m_Type);
            j["name"] = m_Name;
            j["nodeId"] = m_NodeId;

            if (!m_Children.empty()) {
                j["children"] = Json::array();
                for (const auto& child : m_Children) {
                    if (child) {
                        j["children"].push_back(child->ToJson());
                    }
                }
            }

            return j;
        }

    protected:
        BTNodeType m_Type;
        std::string m_Name;
        BTNode* m_Parent;
        std::vector<std::unique_ptr<BTNode>> m_Children;
        BTStatus m_LastStatus;
        bool m_IsActive;
        float m_LastTickTime;
        uint32_t m_NodeId;

        void SetLastStatus(BTStatus status) { m_LastStatus = status; }
        void SetLastTickTime(float time) { m_LastTickTime = time; }

    private:
        static uint32_t GenerateNodeId() {
            static uint32_t s_NextId = 1;
            return s_NextId++;
        }
    };

    // Smart pointer types
    using BTNodePtr = std::unique_ptr<BTNode>;
    using BTNodeRef = BTNode*;

} // namespace AI
} // namespace Core
