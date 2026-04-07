#pragma once

// BehaviorTree Debug Visualization
// Provides debug rendering and logging for behavior tree execution

#include "Core/AI/BehaviorTree/BehaviorTreeContainer.h"
#include <string>
#include <sstream>
#include <functional>

namespace Core {
namespace AI {

    //=========================================================================
    // Debug Node Info
    //=========================================================================

    struct BTDebugNodeInfo {
        std::string Name;
        BTNodeType Type;
        BTStatus Status;
        bool IsActive;
        int Depth;
        std::vector<BTDebugNodeInfo> Children;

        // For visualization
        float ExecutionTime = 0.0f;
        uint32_t ExecutionCount = 0;
    };

    //=========================================================================
    // Debug Visualizer
    //=========================================================================

    class BehaviorTreeDebugger {
    public:
        // Callback types for custom rendering
        using TextCallback = std::function<void(const std::string&, int depth)>;
        using NodeCallback = std::function<void(const BTDebugNodeInfo&)>;

        BehaviorTreeDebugger() = default;

        // ================================================================
        // Tree Structure Visualization
        // ================================================================

        /// Generate a text representation of the tree structure
        std::string GenerateTreeText(const BehaviorTree& tree) {
            std::stringstream ss;
            
            ss << "=== Behavior Tree: " << tree.GetName() << " ===\n";
            ss << "Status: " << StatusToString(tree.GetLastStatus()) << "\n\n";

            if (tree.GetRoot()) {
                PrintNodeRecursive(ss, tree.GetRoot(), 0);
            } else {
                ss << "(empty tree)\n";
            }

            return ss.str();
        }

        /// Generate tree structure with custom callback
        void TraverseTree(const BehaviorTree& tree, NodeCallback callback) {
            if (tree.GetRoot()) {
                auto info = BuildNodeInfo(tree.GetRoot(), 0);
                TraverseNodeInfo(info, callback);
            }
        }

        // ================================================================
        // Active Path Visualization
        // ================================================================

        /// Generate text showing the currently active path through the tree
        std::string GenerateActivePath(const BehaviorTree& tree) {
            std::stringstream ss;
            
            ss << "Active Path:\n";
            
            auto activePath = tree.GetActivePath();
            if (activePath.empty()) {
                ss << "  (no active nodes)\n";
                return ss.str();
            }

            for (size_t i = 0; i < activePath.size(); ++i) {
                auto* node = activePath[i];
                ss << "  ";
                for (size_t j = 0; j < i; ++j) ss << "  ";
                ss << "-> " << node->GetName() 
                   << " [" << StatusToString(node->GetStatus()) << "]\n";
            }

            return ss.str();
        }

        // ================================================================
        // Blackboard Visualization
        // ================================================================

        /// Generate text showing blackboard contents
        std::string GenerateBlackboardText(const Blackboard& blackboard) {
            return blackboard.ToJson().dump(2);
        }

        // ================================================================
        // Status Helpers
        // ================================================================

        /// Convert BTStatus to string
        static std::string StatusToString(BTStatus status) {
            switch (status) {
                case BTStatus::Success: return "SUCCESS";
                case BTStatus::Failure: return "FAILURE";
                case BTStatus::Running: return "RUNNING";
                default: return "UNKNOWN";
            }
        }

        /// Convert BTNodeType to string
        static std::string NodeTypeToString(BTNodeType type) {
            switch (type) {
                case BTNodeType::Composite: return "Composite";
                case BTNodeType::Decorator: return "Decorator";
                case BTNodeType::Leaf: return "Leaf";
                default: return "Unknown";
            }
        }

        /// Get status color (for UI rendering)
        static void GetStatusColor(BTStatus status, float& r, float& g, float& b) {
            switch (status) {
                case BTStatus::Success:
                    r = 0.2f; g = 0.8f; b = 0.2f; // Green
                    break;
                case BTStatus::Failure:
                    r = 0.8f; g = 0.2f; b = 0.2f; // Red
                    break;
                case BTStatus::Running:
                    r = 0.8f; g = 0.8f; b = 0.2f; // Yellow
                    break;
                default:
                    r = 0.5f; g = 0.5f; b = 0.5f; // Gray
            }
        }

        // ================================================================
        // Execution Logging
        // ================================================================

        /// Enable/disable execution logging
        void SetLoggingEnabled(bool enabled) { m_LoggingEnabled = enabled; }
        bool IsLoggingEnabled() const { return m_LoggingEnabled; }

        /// Log a node execution
        void LogNodeExecution(const std::string& treeName, 
                             const std::string& nodeName,
                             BTStatus status) {
            if (!m_LoggingEnabled) return;
            
            m_ExecutionLog.push_back({treeName, nodeName, status});
            
            // Trim log if too long
            while (m_ExecutionLog.size() > m_MaxLogEntries) {
                m_ExecutionLog.erase(m_ExecutionLog.begin());
            }
        }

        /// Get execution log
        struct LogEntry {
            std::string TreeName;
            std::string NodeName;
            BTStatus Status;
        };

        const std::vector<LogEntry>& GetExecutionLog() const { 
            return m_ExecutionLog; 
        }

        /// Clear execution log
        void ClearLog() { m_ExecutionLog.clear(); }

        /// Set maximum log entries
        void SetMaxLogEntries(size_t max) { m_MaxLogEntries = max; }

        // ================================================================
        // Statistics
        // ================================================================

        struct TreeStats {
            uint32_t TotalNodes = 0;
            uint32_t CompositeNodes = 0;
            uint32_t DecoratorNodes = 0;
            uint32_t LeafNodes = 0;
            uint32_t MaxDepth = 0;
        };

        /// Calculate tree statistics
        TreeStats CalculateStats(const BehaviorTree& tree) {
            TreeStats stats;
            if (tree.GetRoot()) {
                CalculateStatsRecursive(tree.GetRoot(), 0, stats);
            }
            return stats;
        }

    private:
        bool m_LoggingEnabled = false;
        size_t m_MaxLogEntries = 1000;
        std::vector<LogEntry> m_ExecutionLog;

        void PrintNodeRecursive(std::stringstream& ss, const BTNode* node, int depth) {
            // Indentation
            for (int i = 0; i < depth; ++i) {
                ss << "  ";
            }

            // Node info
            ss << "├─ " << node->GetName() 
               << " [" << NodeTypeToString(node->GetType()) << "] "
               << StatusToString(node->GetStatus());
            
            if (node->IsActive()) {
                ss << " (ACTIVE)";
            }
            
            ss << "\n";

            // Children
            for (const auto& child : node->GetChildren()) {
                if (child) {
                    PrintNodeRecursive(ss, child.get(), depth + 1);
                }
            }
        }

        BTDebugNodeInfo BuildNodeInfo(const BTNode* node, int depth) {
            BTDebugNodeInfo info;
            info.Name = node->GetName();
            info.Type = node->GetType();
            info.Status = node->GetStatus();
            info.IsActive = node->IsActive();
            info.Depth = depth;

            for (const auto& child : node->GetChildren()) {
                if (child) {
                    info.Children.push_back(BuildNodeInfo(child.get(), depth + 1));
                }
            }

            return info;
        }

        void TraverseNodeInfo(const BTDebugNodeInfo& info, NodeCallback& callback) {
            callback(info);
            for (const auto& child : info.Children) {
                TraverseNodeInfo(child, callback);
            }
        }

        void CalculateStatsRecursive(const BTNode* node, int depth, TreeStats& stats) {
            stats.TotalNodes++;
            stats.MaxDepth = std::max(stats.MaxDepth, static_cast<uint32_t>(depth));

            switch (node->GetType()) {
                case BTNodeType::Composite:
                    stats.CompositeNodes++;
                    break;
                case BTNodeType::Decorator:
                    stats.DecoratorNodes++;
                    break;
                case BTNodeType::Leaf:
                    stats.LeafNodes++;
                    break;
            }

            for (const auto& child : node->GetChildren()) {
                if (child) {
                    CalculateStatsRecursive(child.get(), depth + 1, stats);
                }
            }
        }
    };

    //=========================================================================
    // FSM Debugger (similar interface for FSM)
    //=========================================================================

    class FSMDebugger {
    public:
        /// Generate a text representation of the FSM structure
        std::string GenerateFSMText(const FSM& fsm) {
            std::stringstream ss;
            
            ss << "=== FSM: " << fsm.GetName() << " ===\n";
            ss << "Current State: " << fsm.GetCurrentStateName() << "\n\n";

            ss << "States:\n";
            for (const auto& name : fsm.GetStateNames()) {
                ss << "  - " << name;
                if (fsm.IsInState(name)) {
                    ss << " (ACTIVE)";
                }
                ss << "\n";
            }

            ss << "\nTransition History:\n";
            for (const auto& entry : fsm.GetTransitionHistory()) {
                ss << "  " << entry << "\n";
            }

            return ss.str();
        }

        /// Generate blackboard text
        std::string GenerateBlackboardText(const FSM& fsm) {
            return fsm.GetBlackboard().ToJson().dump(2);
        }
    };

} // namespace AI
} // namespace Core
