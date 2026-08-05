#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace Core {
namespace UI { class UIManager; }
namespace RHI { class RHIDevice; }
namespace ECS {

    // Forward declaration for Entity class
    class Entity;
    class UISystem;
    class SystemPipeline;
    struct SystemPipelineConfig;

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

        // Look up the first entity whose NameComponent matches exactly.
        // Returns a default (invalid) Entity when there is no match, so callers
        // check with IsValid() rather than handling a sentinel.
        Entity FindEntityByName(const std::string& name);
        std::vector<Entity> FindEntitiesByName(const std::string& name);

        // Wrap a raw handle. Returns an invalid Entity if the handle is stale.
        Entity GetEntityByID(entt::entity handle);

        // Hierarchy. Passing a null parent detaches the child.
        void SetParent(Entity child, Entity parent);
        void RemoveParent(Entity child);

        // Visit every entity carrying all of Components. The callback receives the
        // wrapped Entity plus each component by reference.
        template<typename... Components, typename Func>
        void ForEach(Func&& func) {
            auto view = m_Registry.view<Components...>();
            for (auto handle : view) {
                func(Entity(handle, this), view.template get<Components>(handle)...);
            }
        }

        // Raw-handle accessors, for systems that already hold entt handles and do
        // not want to wrap them in an Entity first.
        bool IsValid(entt::entity handle) const { return m_Registry.valid(handle); }

        template<typename T>
        T& Get(entt::entity handle) { return m_Registry.get<T>(handle); }

        template<typename T>
        const T& Get(entt::entity handle) const { return m_Registry.get<T>(handle); }

        template<typename T>
        T* TryGet(entt::entity handle) { return m_Registry.try_get<T>(handle); }

        template<typename T>
        bool Has(entt::entity handle) const { return m_Registry.all_of<T>(handle); }

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

        // Split halves of OnUpdate, for callers that run rendering on another
        // thread. The simulation half touches no renderer state and can overlap
        // submission; the UI half pushes draws into the shared text renderer and
        // must not run while the render thread is flushing it.
        void OnUpdateSimulation(float deltaTime);
        void OnUpdateUI();
        void BindUIManager(UI::UIManager* uiManager);
        UISystem* GetUISystem() { return m_UISystem.get(); }
        const UISystem* GetUISystem() const { return m_UISystem.get(); }

        // Brings up the simulation systems (physics, input, animation, cameras,
        // transforms, render collection) that OnUpdate then ticks each frame.
        // Without this the scene only runs its UI, which is what it did before.
        bool InitializeSystems(const SystemPipelineConfig& config,
                               std::shared_ptr<RHI::RHIDevice> device = nullptr);
        void ShutdownSystems();
        SystemPipeline* GetSystemPipeline() const { return m_SystemPipeline.get(); }

    private:
        std::string m_Name;
        entt::registry m_Registry;
        std::unique_ptr<UISystem> m_UISystem;
        std::unique_ptr<SystemPipeline> m_SystemPipeline;
        UI::UIManager* m_UIManager = nullptr;
        glm::vec2 m_ViewportSize{1920.0f, 1080.0f};
        float m_LastUIDeltaTime = 0.0f;

        friend class Entity;
    };

} // namespace ECS
} // namespace Core
