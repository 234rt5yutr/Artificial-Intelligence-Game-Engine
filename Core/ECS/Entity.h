#pragma once

#include <entt/entt.hpp>
#include "Core/Assert.h"
#include "Core/ECS/Scene.h"

namespace Core {
namespace ECS {

    class Entity {
    public:
        Entity() = default;
        Entity(entt::entity handle, Scene* scene);
        Entity(const Entity& other) = default;
        Entity& operator=(const Entity& other) = default;

        // Component management
        template<typename T, typename... Args>
        T& AddComponent(Args&&... args)
        {
            ENGINE_ASSERT(!HasComponent<T>(), "Entity already has component!");
            return m_Scene->GetRegistry().emplace<T>(m_Handle, std::forward<Args>(args)...);
        }

        template<typename T, typename... Args>
        T& AddOrReplaceComponent(Args&&... args)
        {
            return m_Scene->GetRegistry().emplace_or_replace<T>(m_Handle, std::forward<Args>(args)...);
        }

        template<typename T>
        T& GetComponent()
        {
            ENGINE_ASSERT(HasComponent<T>(), "Entity does not have component!");
            return m_Scene->GetRegistry().get<T>(m_Handle);
        }

        template<typename T>
        const T& GetComponent() const
        {
            ENGINE_ASSERT(HasComponent<T>(), "Entity does not have component!");
            return m_Scene->GetRegistry().get<T>(m_Handle);
        }

        template<typename T>
        T* TryGetComponent()
        {
            return m_Scene->GetRegistry().try_get<T>(m_Handle);
        }

        template<typename T>
        const T* TryGetComponent() const
        {
            return m_Scene->GetRegistry().try_get<T>(m_Handle);
        }

        template<typename T>
        bool HasComponent() const
        {
            return m_Scene->GetRegistry().all_of<T>(m_Handle);
        }

        template<typename... T>
        bool HasComponents() const
        {
            return m_Scene->GetRegistry().all_of<T...>(m_Handle);
        }

        template<typename T>
        void RemoveComponent()
        {
            ENGINE_ASSERT(HasComponent<T>(), "Entity does not have component!");
            m_Scene->GetRegistry().remove<T>(m_Handle);
        }

        // Utility
        entt::entity GetHandle() const { return m_Handle; }
        Scene* GetScene() { return m_Scene; }
        const Scene* GetScene() const { return m_Scene; }
        bool IsValid() const;

        // Operators
        operator bool() const { return IsValid(); }
        operator entt::entity() const { return m_Handle; }
        operator uint32_t() const { return static_cast<uint32_t>(m_Handle); }

        bool operator==(const Entity& other) const
        {
            return m_Handle == other.m_Handle && m_Scene == other.m_Scene;
        }

        bool operator!=(const Entity& other) const
        {
            return !(*this == other);
        }

    private:
        entt::entity m_Handle{ entt::null };
        Scene* m_Scene = nullptr;
    };

} // namespace ECS
} // namespace Core
