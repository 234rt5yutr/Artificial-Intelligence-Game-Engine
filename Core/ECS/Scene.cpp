#include "Core/ECS/Scene.h"
#include "Core/ECS/Entity.h"
#include "Core/ECS/Components/NameComponent.h"
#include "Core/Log.h"
#include "Core/Profile.h"

namespace Core {
namespace ECS {

    Scene::Scene(const std::string& name)
        : m_Name(name)
    {
        ENGINE_CORE_INFO("Scene '{}' created", m_Name);
    }

    Scene::~Scene()
    {
        ENGINE_CORE_INFO("Scene '{}' destroyed", m_Name);
    }

    Scene::Scene(Scene&& other) noexcept
        : m_Name(std::move(other.m_Name))
        , m_Registry(std::move(other.m_Registry))
    {
    }

    Scene& Scene::operator=(Scene&& other) noexcept
    {
        if (this != &other) {
            m_Name = std::move(other.m_Name);
            m_Registry = std::move(other.m_Registry);
        }
        return *this;
    }

    Entity Scene::CreateEntity(const std::string& name)
    {
        PROFILE_FUNCTION();
        
        entt::entity handle = m_Registry.create();
        m_Registry.emplace<NameComponent>(handle, NameComponent{name});
        Entity entity(handle, this);
        
        ENGINE_CORE_TRACE("Entity '{}' created in scene '{}'", name, m_Name);
        return entity;
    }

    void Scene::DestroyEntity(Entity entity)
    {
        PROFILE_FUNCTION();
        
        if (IsValidEntity(entity)) {
            m_Registry.destroy(entity.GetHandle());
            ENGINE_CORE_TRACE("Entity destroyed in scene '{}'", m_Name);
        }
    }

    bool Scene::IsValidEntity(Entity entity) const
    {
        return m_Registry.valid(entity.GetHandle());
    }

    std::size_t Scene::GetEntityCount() const
    {
        return m_Registry.storage<entt::entity>()->size();
    }

    void Scene::Clear()
    {
        PROFILE_FUNCTION();
        
        m_Registry.clear();
        ENGINE_CORE_INFO("Scene '{}' cleared", m_Name);
    }

    void Scene::OnUpdate(float deltaTime)
    {
        PROFILE_FUNCTION();
        
        // Systems will be implemented in future steps
        (void)deltaTime;
    }

} // namespace ECS
} // namespace Core
