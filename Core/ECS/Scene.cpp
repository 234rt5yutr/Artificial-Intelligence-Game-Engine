#include "Core/ECS/Scene.h"
#include "Core/ECS/Entity.h"
#include "Core/ECS/Components/NameComponent.h"
#include "Core/ECS/Systems/UISystem.h"
#include "Core/ECS/Systems/TransformSystem.h"
#include "Core/ECS/SystemPipeline.h"
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
        // Systems hold registry references, so they must stop before it is destroyed.
        ShutdownSystems();
        if (m_UISystem) {
            m_UISystem->Shutdown();
        }
        ENGINE_CORE_INFO("Scene '{}' destroyed", m_Name);
    }

    Scene::Scene(Scene&& other) noexcept
        : m_Name(std::move(other.m_Name))
        , m_Registry(std::move(other.m_Registry))
        , m_UISystem(std::move(other.m_UISystem))
        , m_SystemPipeline(std::move(other.m_SystemPipeline))
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
            ShutdownSystems();
            if (m_UISystem) {
                m_UISystem->Shutdown();
            }
            m_Name = std::move(other.m_Name);
            m_Registry = std::move(other.m_Registry);
            m_UISystem = std::move(other.m_UISystem);
            m_SystemPipeline = std::move(other.m_SystemPipeline);
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

    Entity Scene::FindEntityByName(const std::string& name)
    {
        PROFILE_FUNCTION();

        auto view = m_Registry.view<NameComponent>();
        for (auto handle : view) {
            if (view.get<NameComponent>(handle).Name == name) {
                return Entity(handle, this);
            }
        }
        return Entity{};
    }

    Entity Scene::GetEntityByID(entt::entity handle)
    {
        if (handle == entt::null || !m_Registry.valid(handle)) {
            return Entity{};
        }
        return Entity(handle, this);
    }

    void Scene::SetParent(Entity child, Entity parent)
    {
        if (!IsValidEntity(child)) {
            return;
        }
        if (!parent.IsValid()) {
            TransformSystem::RemoveParent(*this, child.GetHandle());
            return;
        }
        TransformSystem::SetParent(*this, child.GetHandle(), parent.GetHandle());
    }

    void Scene::RemoveParent(Entity child)
    {
        if (!IsValidEntity(child)) {
            return;
        }
        TransformSystem::RemoveParent(*this, child.GetHandle());
    }

    std::vector<Entity> Scene::FindEntitiesByName(const std::string& name)
    {
        PROFILE_FUNCTION();

        std::vector<Entity> matches;
        auto view = m_Registry.view<NameComponent>();
        for (auto handle : view) {
            if (view.get<NameComponent>(handle).Name == name) {
                matches.emplace_back(handle, this);
            }
        }
        return matches;
    }

    std::size_t Scene::GetEntityCount() const
    {
        // `size()` on the entity storage counts recycled slots as well as live
        // entities, so it never went down when something was destroyed. That
        // number is reported straight out of the MCP scene tools, where it read
        // as "entities are leaking".
        const auto* storage = m_Registry.storage<entt::entity>();
        if (storage == nullptr) {
            return 0;
        }
        // entt keeps the entity storage under a swap-only deletion policy, where
        // free_list() is the number of entities still in use. size() counts
        // recycled slots as well, which is why the old count never went down.
        return storage->free_list();
    }

    void Scene::Clear()
    {
        PROFILE_FUNCTION();
        
        m_Registry.clear();
        ENGINE_CORE_INFO("Scene '{}' cleared", m_Name);
    }

    bool Scene::InitializeSystems(const SystemPipelineConfig& config,
                                  std::shared_ptr<RHI::RHIDevice> device)
    {
        if (!m_SystemPipeline) {
            m_SystemPipeline = std::make_unique<SystemPipeline>();
        }
        return m_SystemPipeline->Initialize(config, std::move(device));
    }

    void Scene::ShutdownSystems()
    {
        if (m_SystemPipeline) {
            m_SystemPipeline->Shutdown();
            m_SystemPipeline.reset();
        }
    }

    void Scene::OnUpdate(float deltaTime)
    {
        PROFILE_FUNCTION();

        // Simulation runs before UI so widgets see this frame's world state.
        OnUpdateSimulation(deltaTime);
        OnUpdateUI();
    }

    void Scene::OnUpdateSimulation(float deltaTime)
    {
        PROFILE_FUNCTION();

        m_LastUIDeltaTime = deltaTime;
        if (m_SystemPipeline != nullptr) {
            m_SystemPipeline->Update(*this, deltaTime);
        }
    }

    void Scene::OnUpdateUI()
    {
        PROFILE_FUNCTION();

        if (m_UIManager != nullptr) {
            m_ViewportSize = m_UIManager->GetViewportSize();
        }

        if (m_UISystem != nullptr) {
            m_UISystem->Update(m_LastUIDeltaTime, m_ViewportSize);
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
