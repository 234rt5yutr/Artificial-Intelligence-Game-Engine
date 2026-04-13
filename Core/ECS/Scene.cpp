#include "Core/ECS/Scene.h"
#include "Core/ECS/Entity.h"
#include "Core/ECS/Components/NameComponent.h"
#include "Core/ECS/Systems/UISystem.h"
#include "Core/Log.h"
#include "Core/Profile.h"
#include "Core/UI/UIManager.h"

namespace Core {
namespace ECS {

    Scene::Scene(const std::string& name)
        : m_Name(name)
    {
        m_UISystem = std::make_unique<UISystem>();
        m_UISystem->Initialize(this);
        ENGINE_CORE_INFO("Scene '{}' created", m_Name);
    }

    Scene::~Scene()
    {
        if (m_UISystem) {
            m_UISystem->Shutdown();
        }
        ENGINE_CORE_INFO("Scene '{}' destroyed", m_Name);
    }

    Scene::Scene(Scene&& other) noexcept
        : m_Name(std::move(other.m_Name))
        , m_Registry(std::move(other.m_Registry))
        , m_UISystem(std::move(other.m_UISystem))
        , m_UIManager(other.m_UIManager)
        , m_ViewportSize(other.m_ViewportSize)
    {
        if (m_UISystem) {
            m_UISystem->Initialize(this);
        }
        other.m_UIManager = nullptr;
    }

    Scene& Scene::operator=(Scene&& other) noexcept
    {
        if (this != &other) {
            if (m_UISystem) {
                m_UISystem->Shutdown();
            }
            m_Name = std::move(other.m_Name);
            m_Registry = std::move(other.m_Registry);
            m_UISystem = std::move(other.m_UISystem);
            m_UIManager = other.m_UIManager;
            m_ViewportSize = other.m_ViewportSize;

            if (m_UISystem) {
                m_UISystem->Initialize(this);
            }
            other.m_UIManager = nullptr;
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

        if (m_UIManager != nullptr) {
            m_ViewportSize = m_UIManager->GetViewportSize();
        }

        if (m_UISystem != nullptr) {
            m_UISystem->Update(deltaTime, m_ViewportSize);
            if (m_UIManager != nullptr && m_UIManager->IsInitialized()) {
                m_UISystem->Render(m_UIManager->GetTextRenderer(), m_ViewportSize);
            }
        }
    }

    void Scene::BindUIManager(UI::UIManager* uiManager) {
        m_UIManager = uiManager;
    }

} // namespace ECS
} // namespace Core
