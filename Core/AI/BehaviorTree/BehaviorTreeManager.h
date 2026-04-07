#pragma once

// BehaviorTree Manager
// Singleton that manages all behavior tree templates and instances

#include "BehaviorTreeContainer.h"
#include <unordered_map>
#include <string>
#include <memory>
#include <functional>
#include <mutex>

namespace Core {
namespace AI {

    // Singleton manager for behavior trees
    class BehaviorTreeManager {
    public:
        // Get the singleton instance
        static BehaviorTreeManager& Get() {
            static BehaviorTreeManager instance;
            return instance;
        }

        // Deleted copy/move operations
        BehaviorTreeManager(const BehaviorTreeManager&) = delete;
        BehaviorTreeManager& operator=(const BehaviorTreeManager&) = delete;
        BehaviorTreeManager(BehaviorTreeManager&&) = delete;
        BehaviorTreeManager& operator=(BehaviorTreeManager&&) = delete;

        // ====================================================================
        // Template Management
        // ====================================================================

        // Register a behavior tree template by ID
        void RegisterTemplate(const std::string& id, std::unique_ptr<BehaviorTree> tree) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Templates[id] = std::move(tree);
        }

        // Register a factory function for lazy creation
        void RegisterTemplateFactory(const std::string& id, 
                                     std::function<std::unique_ptr<BehaviorTree>()> factory) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Factories[id] = std::move(factory);
        }

        // Check if a template exists
        bool HasTemplate(const std::string& id) const {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return m_Templates.contains(id) || m_Factories.contains(id);
        }

        // Get a template (read-only, for inspection)
        const BehaviorTree* GetTemplate(const std::string& id) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            EnsureTemplateCreated(id);
            
            auto it = m_Templates.find(id);
            return it != m_Templates.end() ? it->second.get() : nullptr;
        }

        // Unregister a template
        void UnregisterTemplate(const std::string& id) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Templates.erase(id);
            m_Factories.erase(id);
        }

        // Get list of all registered template IDs
        std::vector<std::string> GetTemplateIds() const {
            std::lock_guard<std::mutex> lock(m_Mutex);
            std::vector<std::string> ids;
            ids.reserve(m_Templates.size() + m_Factories.size());
            
            for (const auto& [id, _] : m_Templates) {
                ids.push_back(id);
            }
            for (const auto& [id, _] : m_Factories) {
                if (!m_Templates.contains(id)) {
                    ids.push_back(id);
                }
            }
            
            return ids;
        }

        // ====================================================================
        // Instance Management
        // ====================================================================

        // Create a new instance from a template
        std::unique_ptr<BehaviorTree> CreateInstance(const std::string& templateId) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            
            // Try factory first (creates fresh instance each time)
            auto factoryIt = m_Factories.find(templateId);
            if (factoryIt != m_Factories.end()) {
                return factoryIt->second();
            }
            
            // Fall back to cloning template
            EnsureTemplateCreated(templateId);
            auto templateIt = m_Templates.find(templateId);
            if (templateIt != m_Templates.end()) {
                // Clone via JSON (simple but effective)
                return BehaviorTree::FromJson(templateIt->second->ToJson());
            }
            
            return nullptr;
        }

        // Assign a tree instance to an entity
        void AssignToEntity(uint32_t entityId, std::unique_ptr<BehaviorTree> tree) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_EntityTrees[entityId] = std::move(tree);
        }

        // Create instance from template and assign to entity
        bool AssignTemplateToEntity(uint32_t entityId, const std::string& templateId) {
            auto tree = CreateInstance(templateId);
            if (tree) {
                AssignToEntity(entityId, std::move(tree));
                return true;
            }
            return false;
        }

        // Get the tree assigned to an entity
        BehaviorTree* GetEntityTree(uint32_t entityId) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            auto it = m_EntityTrees.find(entityId);
            return it != m_EntityTrees.end() ? it->second.get() : nullptr;
        }

        // Remove tree from entity
        void RemoveFromEntity(uint32_t entityId) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_EntityTrees.erase(entityId);
        }

        // Get list of entities with assigned trees
        std::vector<uint32_t> GetEntitiesWithTrees() const {
            std::lock_guard<std::mutex> lock(m_Mutex);
            std::vector<uint32_t> entities;
            entities.reserve(m_EntityTrees.size());
            
            for (const auto& [entityId, _] : m_EntityTrees) {
                entities.push_back(entityId);
            }
            
            return entities;
        }

        // ====================================================================
        // Serialization
        // ====================================================================

        // Load templates from JSON
        void LoadTemplatesFromJson(const Json& json) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            
            if (!json.is_array()) return;
            
            for (const auto& treeJson : json) {
                if (treeJson.contains("name")) {
                    std::string name = treeJson["name"].get<std::string>();
                    auto tree = BehaviorTree::FromJson(treeJson);
                    if (tree) {
                        m_Templates[name] = std::move(tree);
                    }
                }
            }
        }

        // Save all templates to JSON
        Json SaveTemplatesToJson() {
            std::lock_guard<std::mutex> lock(m_Mutex);
            
            Json json = Json::array();
            
            for (const auto& [id, tree] : m_Templates) {
                if (tree) {
                    json.push_back(tree->ToJson());
                }
            }
            
            return json;
        }

        // ====================================================================
        // Lifecycle
        // ====================================================================

        // Clear all templates and instances
        void Clear() {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Templates.clear();
            m_Factories.clear();
            m_EntityTrees.clear();
        }

        // Get statistics
        struct Stats {
            size_t templateCount;
            size_t factoryCount;
            size_t entityTreeCount;
        };

        Stats GetStats() const {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return Stats{
                m_Templates.size(),
                m_Factories.size(),
                m_EntityTrees.size()
            };
        }

    private:
        BehaviorTreeManager() = default;
        ~BehaviorTreeManager() = default;

        mutable std::mutex m_Mutex;
        
        // Registered tree templates
        std::unordered_map<std::string, std::unique_ptr<BehaviorTree>> m_Templates;
        
        // Factory functions for lazy template creation
        std::unordered_map<std::string, std::function<std::unique_ptr<BehaviorTree>()>> m_Factories;
        
        // Trees assigned to specific entities
        std::unordered_map<uint32_t, std::unique_ptr<BehaviorTree>> m_EntityTrees;

        void EnsureTemplateCreated(const std::string& id) {
            // Must be called with lock held
            if (!m_Templates.contains(id) && m_Factories.contains(id)) {
                m_Templates[id] = m_Factories[id]();
            }
        }
    };

} // namespace AI
} // namespace Core
