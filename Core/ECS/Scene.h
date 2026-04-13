#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <functional>

namespace Core {
namespace UI { class UIManager; }
namespace ECS {

    // Forward declaration for Entity class
    class Entity;
    class UISystem;

    class Scene {
    public:
        Scene(const std::string& name = "Untitled Scene");
        ~Scene();

        // Delete copy operations
        Scene(const Scene&) = delete;
        Scene& operator=(const Scene&) = delete;

        // Move operations
        Scene(Scene&& other) noexcept;
        Scene& operator=(Scene&& other) noexcept;

        // Entity management
        Entity CreateEntity(const std::string& name = "Entity");
        void DestroyEntity(Entity entity);
        bool IsValidEntity(Entity entity) const;

        // Scene properties
        const std::string& GetName() const { return m_Name; }
        void SetName(const std::string& name) { m_Name = name; }

        // Registry access for systems
        entt::registry& GetRegistry() { return m_Registry; }
        const entt::registry& GetRegistry() const { return m_Registry; }

        // Entity iteration
        template<typename... Components>
        auto View() {
            return m_Registry.view<Components...>();
        }

        template<typename... Components>
        auto View() const {
            return m_Registry.view<Components...>();
        }

        // Entity count
        std::size_t GetEntityCount() const;

        // Clear all entities
        void Clear();

        // Update scene (called each frame)
        void OnUpdate(float deltaTime);
        void BindUIManager(UI::UIManager* uiManager);
        UISystem* GetUISystem() { return m_UISystem.get(); }
        const UISystem* GetUISystem() const { return m_UISystem.get(); }

    private:
        std::string m_Name;
        entt::registry m_Registry;
        std::unique_ptr<UISystem> m_UISystem;
        UI::UIManager* m_UIManager = nullptr;
        glm::vec2 m_ViewportSize{1920.0f, 1080.0f};

        friend class Entity;
    };

} // namespace ECS
} // namespace Core
