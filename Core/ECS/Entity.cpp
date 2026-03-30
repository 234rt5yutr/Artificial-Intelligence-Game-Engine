#include "Core/ECS/Entity.h"
#include "Core/ECS/Scene.h"

namespace Core {
namespace ECS {

    Entity::Entity(entt::entity handle, Scene* scene)
        : m_Handle(handle)
        , m_Scene(scene)
    {
    }

    bool Entity::IsValid() const
    {
        return m_Scene != nullptr && m_Scene->GetRegistry().valid(m_Handle);
    }

} // namespace ECS
} // namespace Core
