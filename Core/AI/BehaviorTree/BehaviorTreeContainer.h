#pragma once

// BehaviorTree Container Class
// Main container class that holds the tree structure and manages execution

#include "BehaviorTree.h"
#include "Blackboard.h"
#include "BTComposites.h"
#include "BTDecorators.h"
#include "BTLeaves.h"
#include <string>
#include <memory>

namespace Core {
namespace AI {

    // Main BehaviorTree class
    class BehaviorTree {
    public:
        explicit BehaviorTree(const std::string& name = "BehaviorTree")
            : m_Name(name)
            , m_LastStatus(BTStatus::Success) {}

        ~BehaviorTree() = default;

        // Non-copyable but movable
        BehaviorTree(const BehaviorTree&) = delete;
        BehaviorTree& operator=(const BehaviorTree&) = delete;
        BehaviorTree(BehaviorTree&&) = default;
        BehaviorTree& operator=(BehaviorTree&&) = default;

        // Execute one tick of the behavior tree
        BTStatus Tick(float deltaTime) {
            if (!m_Root) {
                m_LastStatus = BTStatus::Success;
                return BTStatus::Success;
            }

            m_LastStatus = m_Root->Tick(deltaTime, m_Blackboard);
            return m_LastStatus;
        }

        // Reset the tree for reuse
        void Reset() {
            m_LastStatus = BTStatus::Success;
            if (m_Root) {
                m_Root->Reset();
            }
        }

        // Set the root node
        void SetRoot(std::unique_ptr<BTNode> root) {
            m_Root = std::move(root);
        }

        // Get the root node
        BTNode* GetRoot() const { return m_Root.get(); }

        // Blackboard access
        Blackboard& GetBlackboard() { return m_Blackboard; }
        const Blackboard& GetBlackboard() const { return m_Blackboard; }

        // Accessors
        const std::string& GetName() const { return m_Name; }
        void SetName(const std::string& name) { m_Name = name; }
        BTStatus GetLastStatus() const { return m_LastStatus; }

        // Get the path of currently active nodes (for debugging)
        std::vector<BTNode*> GetActivePath() const {
            std::vector<BTNode*> path;
            if (m_Root) {
                CollectActivePath(m_Root.get(), path);
            }
            return path;
        }

        // Get all nodes in the tree
        std::vector<BTNode*> GetAllNodes() const {
            std::vector<BTNode*> nodes;
            if (m_Root) {
                CollectAllNodes(m_Root.get(), nodes);
            }
            return nodes;
        }

        // Serialization
        Json ToJson() const {
            Json j;
            j["name"] = m_Name;
            j["blackboard"] = m_Blackboard.ToJson();
            
            if (m_Root) {
                j["root"] = m_Root->ToJson();
            }
            
            return j;
        }

        // Deserialization factory
        static std::unique_ptr<BehaviorTree> FromJson(const Json& json);

    private:
        std::string m_Name;
        std::unique_ptr<BTNode> m_Root;
        Blackboard m_Blackboard;
        BTStatus m_LastStatus;

        static void CollectActivePath(BTNode* node, std::vector<BTNode*>& path) {
            if (!node) return;
            
            if (node->IsActive() || node->IsRunning()) {
                path.push_back(node);
            }
            
            for (const auto& child : node->GetChildren()) {
                if (child) {
                    CollectActivePath(child.get(), path);
                }
            }
        }

        static void CollectAllNodes(BTNode* node, std::vector<BTNode*>& nodes) {
            if (!node) return;
            
            nodes.push_back(node);
            
            for (const auto& child : node->GetChildren()) {
                if (child) {
                    CollectAllNodes(child.get(), nodes);
                }
            }
        }
    };

    // Node factory for JSON deserialization
    class BTNodeFactory {
    public:
        using NodeCreator = std::function<std::unique_ptr<BTNode>(const Json&)>;

        static BTNodeFactory& Get() {
            static BTNodeFactory instance;
            return instance;
        }

        void RegisterNodeType(const std::string& typeName, NodeCreator creator) {
            m_Creators[typeName] = std::move(creator);
        }

        std::unique_ptr<BTNode> CreateNode(const std::string& typeName, const Json& json) {
            auto it = m_Creators.find(typeName);
            if (it != m_Creators.end()) {
                return it->second(json);
            }
            return nullptr;
        }

        std::unique_ptr<BTNode> CreateNodeFromJson(const Json& json) {
            if (!json.contains("type")) return nullptr;
            
            std::string typeName = json["type"].get<std::string>();
            auto node = CreateNode(typeName, json);
            
            if (node && json.contains("children")) {
                for (const auto& childJson : json["children"]) {
                    auto child = CreateNodeFromJson(childJson);
                    if (child) {
                        node->AddChild(std::move(child));
                    }
                }
            }
            
            return node;
        }

    private:
        BTNodeFactory() {
            // Register built-in node types
            RegisterBuiltinTypes();
        }

        void RegisterBuiltinTypes() {
            // Composites
            RegisterNodeType("Sequence", [](const Json& json) {
                std::string name = json.value("name", "Sequence");
                return std::make_unique<SequenceNode>(name);
            });

            RegisterNodeType("Selector", [](const Json& json) {
                std::string name = json.value("name", "Selector");
                return std::make_unique<SelectorNode>(name);
            });

            RegisterNodeType("Parallel", [](const Json& json) {
                std::string name = json.value("name", "Parallel");
                auto successPolicy = static_cast<ParallelNode::Policy>(
                    json.value("successPolicy", static_cast<int>(ParallelNode::Policy::RequireAll)));
                auto failurePolicy = static_cast<ParallelNode::Policy>(
                    json.value("failurePolicy", static_cast<int>(ParallelNode::Policy::RequireOne)));
                return std::make_unique<ParallelNode>(successPolicy, failurePolicy, name);
            });

            RegisterNodeType("RandomSelector", [](const Json& json) {
                std::string name = json.value("name", "RandomSelector");
                return std::make_unique<RandomSelectorNode>(name);
            });

            // Decorators
            RegisterNodeType("Inverter", [](const Json& json) {
                std::string name = json.value("name", "Inverter");
                return std::make_unique<InverterNode>(name);
            });

            RegisterNodeType("Succeeder", [](const Json& json) {
                std::string name = json.value("name", "Succeeder");
                return std::make_unique<SucceederNode>(name);
            });

            RegisterNodeType("Failer", [](const Json& json) {
                std::string name = json.value("name", "Failer");
                return std::make_unique<FailerNode>(name);
            });

            RegisterNodeType("Repeater", [](const Json& json) {
                std::string name = json.value("name", "Repeater");
                int32_t repeatCount = json.value("repeatCount", -1);
                return std::make_unique<RepeaterNode>(repeatCount, name);
            });

            RegisterNodeType("RepeatUntilFail", [](const Json& json) {
                std::string name = json.value("name", "RepeatUntilFail");
                return std::make_unique<RepeatUntilFailNode>(name);
            });

            RegisterNodeType("Cooldown", [](const Json& json) {
                std::string name = json.value("name", "Cooldown");
                float cooldownSeconds = json.value("cooldownSeconds", 1.0f);
                return std::make_unique<CooldownNode>(cooldownSeconds, name);
            });

            RegisterNodeType("TimeLimit", [](const Json& json) {
                std::string name = json.value("name", "TimeLimit");
                float timeLimitSeconds = json.value("timeLimitSeconds", 5.0f);
                return std::make_unique<TimeLimitNode>(timeLimitSeconds, name);
            });

            // Leaves
            RegisterNodeType("Wait", [](const Json& json) {
                std::string name = json.value("name", "Wait");
                float waitSeconds = json.value("waitSeconds", 1.0f);
                return std::make_unique<WaitNode>(waitSeconds, name);
            });

            RegisterNodeType("Log", [](const Json& json) {
                std::string name = json.value("name", "Log");
                std::string message = json.value("message", "");
                auto level = static_cast<LogNode::LogLevel>(
                    json.value("level", static_cast<int>(LogNode::LogLevel::Info)));
                auto node = std::make_unique<LogNode>(message, level, name);
                node->SetLogOnce(json.value("logOnce", false));
                return node;
            });

            RegisterNodeType("SubTree", [](const Json& json) {
                std::string name = json.value("name", "SubTree");
                std::string treeId = json.value("treeId", "");
                return std::make_unique<SubTreeNode>(treeId, nullptr, name);
            });

            // Note: Action and Condition nodes cannot be deserialized from JSON
            // because they require lambda functions. They must be created programmatically.
        }

        std::unordered_map<std::string, NodeCreator> m_Creators;
    };

    // BehaviorTree deserialization implementation
    inline std::unique_ptr<BehaviorTree> BehaviorTree::FromJson(const Json& json) {
        auto tree = std::make_unique<BehaviorTree>(json.value("name", "BehaviorTree"));
        
        if (json.contains("blackboard")) {
            tree->m_Blackboard.FromJson(json["blackboard"]);
        }
        
        if (json.contains("root")) {
            auto root = BTNodeFactory::Get().CreateNodeFromJson(json["root"]);
            tree->SetRoot(std::move(root));
        }
        
        return tree;
    }

} // namespace AI
} // namespace Core
