#pragma once

// BehaviorTree Fluent Builder
// Provides a fluent API for constructing behavior trees programmatically

#include "BehaviorTreeContainer.h"
#include <stack>
#include <stdexcept>

namespace Core {
namespace AI {

    // Fluent builder for behavior tree construction
    class BehaviorTreeBuilder {
    public:
        explicit BehaviorTreeBuilder(const std::string& name = "BehaviorTree")
            : m_Name(name) {}

        // ====================================================================
        // Composite builders
        // ====================================================================

        // Start a sequence composite
        BehaviorTreeBuilder& Sequence(const std::string& name = "Sequence") {
            auto node = std::make_unique<SequenceNode>(name);
            PushNode(std::move(node));
            return *this;
        }

        // Start a selector composite
        BehaviorTreeBuilder& Selector(const std::string& name = "Selector") {
            auto node = std::make_unique<SelectorNode>(name);
            PushNode(std::move(node));
            return *this;
        }

        // Start a parallel composite
        BehaviorTreeBuilder& Parallel(
            ParallelNode::Policy successPolicy = ParallelNode::Policy::RequireAll,
            ParallelNode::Policy failurePolicy = ParallelNode::Policy::RequireOne,
            const std::string& name = "Parallel") {
            auto node = std::make_unique<ParallelNode>(successPolicy, failurePolicy, name);
            PushNode(std::move(node));
            return *this;
        }

        // Start a random selector composite
        BehaviorTreeBuilder& RandomSelector(const std::string& name = "RandomSelector") {
            auto node = std::make_unique<RandomSelectorNode>(name);
            PushNode(std::move(node));
            return *this;
        }

        // ====================================================================
        // Decorator builders
        // ====================================================================

        // Add an inverter decorator
        BehaviorTreeBuilder& Inverter(const std::string& name = "Inverter") {
            auto node = std::make_unique<InverterNode>(name);
            PushNode(std::move(node));
            return *this;
        }

        // Add a succeeder decorator
        BehaviorTreeBuilder& Succeeder(const std::string& name = "Succeeder") {
            auto node = std::make_unique<SucceederNode>(name);
            PushNode(std::move(node));
            return *this;
        }

        // Add a failer decorator
        BehaviorTreeBuilder& Failer(const std::string& name = "Failer") {
            auto node = std::make_unique<FailerNode>(name);
            PushNode(std::move(node));
            return *this;
        }

        // Add a repeater decorator
        BehaviorTreeBuilder& Repeater(int32_t count = -1, const std::string& name = "Repeater") {
            auto node = std::make_unique<RepeaterNode>(count, name);
            PushNode(std::move(node));
            return *this;
        }

        // Add a repeat-until-fail decorator
        BehaviorTreeBuilder& RepeatUntilFail(const std::string& name = "RepeatUntilFail") {
            auto node = std::make_unique<RepeatUntilFailNode>(name);
            PushNode(std::move(node));
            return *this;
        }

        // Add a cooldown decorator
        BehaviorTreeBuilder& Cooldown(float seconds, const std::string& name = "Cooldown") {
            auto node = std::make_unique<CooldownNode>(seconds, name);
            PushNode(std::move(node));
            return *this;
        }

        // Add a time limit decorator
        BehaviorTreeBuilder& TimeLimit(float seconds, const std::string& name = "TimeLimit") {
            auto node = std::make_unique<TimeLimitNode>(seconds, name);
            PushNode(std::move(node));
            return *this;
        }

        // ====================================================================
        // Leaf builders
        // ====================================================================

        // Add an action leaf
        BehaviorTreeBuilder& Action(ActionNode::ActionFunc action, 
                                    const std::string& name = "Action") {
            auto node = std::make_unique<ActionNode>(std::move(action), name);
            AddLeafNode(std::move(node));
            return *this;
        }

        // Add a condition leaf
        BehaviorTreeBuilder& Condition(ConditionNode::ConditionFunc condition,
                                       const std::string& name = "Condition") {
            auto node = std::make_unique<ConditionNode>(std::move(condition), name);
            AddLeafNode(std::move(node));
            return *this;
        }

        // Add a wait leaf
        BehaviorTreeBuilder& Wait(float seconds, const std::string& name = "Wait") {
            auto node = std::make_unique<WaitNode>(seconds, name);
            AddLeafNode(std::move(node));
            return *this;
        }

        // Add a log leaf
        BehaviorTreeBuilder& Log(const std::string& message, 
                                 LogNode::LogLevel level = LogNode::LogLevel::Info,
                                 const std::string& name = "Log") {
            auto node = std::make_unique<LogNode>(message, level, name);
            AddLeafNode(std::move(node));
            return *this;
        }

        // Add a subtree reference
        BehaviorTreeBuilder& SubTree(const std::string& treeId,
                                     const std::string& name = "SubTree") {
            auto node = std::make_unique<SubTreeNode>(treeId, nullptr, name);
            AddLeafNode(std::move(node));
            return *this;
        }

        // Add a blackboard condition
        template<typename T>
        BehaviorTreeBuilder& BlackboardCondition(const std::string& key, 
                                                  ComparisonOp op, 
                                                  const T& value,
                                                  const std::string& name = "BlackboardCondition") {
            auto node = std::make_unique<BlackboardConditionNode>(key, op, value, name);
            AddLeafNode(std::move(node));
            return *this;
        }

        // Add a set blackboard action
        template<typename T>
        BehaviorTreeBuilder& SetBlackboard(const std::string& key,
                                           const T& value,
                                           const std::string& name = "SetBlackboard") {
            auto node = std::make_unique<SetBlackboardNode>(key, value, name);
            AddLeafNode(std::move(node));
            return *this;
        }

        // ====================================================================
        // Structure control
        // ====================================================================

        // End the current composite or decorator scope
        BehaviorTreeBuilder& End() {
            if (m_NodeStack.empty()) {
                throw std::runtime_error("BehaviorTreeBuilder::End() called with empty stack");
            }

            auto node = std::move(m_NodeStack.top());
            m_NodeStack.pop();

            if (m_NodeStack.empty()) {
                // This is the root node
                m_Root = std::move(node);
            } else {
                // Add as child to parent
                m_NodeStack.top()->AddChild(std::move(node));
            }

            return *this;
        }

        // ====================================================================
        // Build
        // ====================================================================

        // Build and return the behavior tree
        std::unique_ptr<BehaviorTree> Build() {
            // Close any remaining open scopes
            while (!m_NodeStack.empty()) {
                End();
            }

            auto tree = std::make_unique<BehaviorTree>(m_Name);
            
            if (m_Root) {
                tree->SetRoot(std::move(m_Root));
            }

            return tree;
        }

        // Validate the tree structure without building
        bool Validate(std::vector<std::string>& errors) const {
            errors.clear();

            if (!m_NodeStack.empty()) {
                errors.push_back("Unclosed scopes: " + std::to_string(m_NodeStack.size()));
            }

            if (!m_Root) {
                errors.push_back("No root node defined");
            }

            return errors.empty();
        }

    private:
        std::string m_Name;
        std::unique_ptr<BTNode> m_Root;
        std::stack<std::unique_ptr<BTNode>> m_NodeStack;

        void PushNode(std::unique_ptr<BTNode> node) {
            m_NodeStack.push(std::move(node));
        }

        void AddLeafNode(std::unique_ptr<BTNode> node) {
            if (m_NodeStack.empty()) {
                // Leaf is the root (unusual but valid)
                m_Root = std::move(node);
            } else {
                m_NodeStack.top()->AddChild(std::move(node));
            }
        }
    };

    // ============================================================================
    // Common Behavior Tree Patterns
    // ============================================================================
    namespace BTPatterns {

        // Create a simple patrol pattern
        inline std::unique_ptr<BehaviorTree> CreatePatrolPattern(
            const std::vector<glm::vec3>& waypoints,
            std::function<BTStatus(float, Blackboard&, const glm::vec3&)> moveToAction) {
            
            return BehaviorTreeBuilder("PatrolPattern")
                .Repeater(-1, "InfinitePatrol")
                    .Sequence("PatrolSequence")
                        .Action([waypoints, moveToAction, idx = 0](float dt, Blackboard& bb) mutable {
                            if (waypoints.empty()) return BTStatus::Failure;
                            
                            BTStatus status = moveToAction(dt, bb, waypoints[idx]);
                            if (status == BTStatus::Success) {
                                idx = (idx + 1) % waypoints.size();
                            }
                            return status;
                        }, "MoveToWaypoint")
                        .Wait(1.0f, "WaitAtWaypoint")
                    .End()
                .End()
                .Build();
        }

        // Create a simple chase pattern
        inline std::unique_ptr<BehaviorTree> CreateChasePattern(
            std::function<bool(const Blackboard&)> hasTarget,
            std::function<BTStatus(float, Blackboard&)> chaseAction,
            std::function<BTStatus(float, Blackboard&)> searchAction) {
            
            return BehaviorTreeBuilder("ChasePattern")
                .Selector("ChaseOrSearch")
                    .Sequence("ChaseSequence")
                        .Condition(hasTarget, "HasTarget")
                        .Action(chaseAction, "ChaseTarget")
                    .End()
                    .Action(searchAction, "SearchForTarget")
                .End()
                .Build();
        }

        // Create a combat pattern with cooldowns
        inline std::unique_ptr<BehaviorTree> CreateCombatPattern(
            std::function<bool(const Blackboard&)> canAttack,
            std::function<BTStatus(float, Blackboard&)> attackAction,
            std::function<BTStatus(float, Blackboard&)> defendAction,
            float attackCooldown = 2.0f) {
            
            return BehaviorTreeBuilder("CombatPattern")
                .Selector("CombatSelector")
                    .Sequence("AttackSequence")
                        .Condition(canAttack, "CanAttack")
                        .Cooldown(attackCooldown, "AttackCooldown")
                            .Action(attackAction, "Attack")
                        .End()
                    .End()
                    .Action(defendAction, "Defend")
                .End()
                .Build();
        }

    } // namespace BTPatterns

} // namespace AI
} // namespace Core
