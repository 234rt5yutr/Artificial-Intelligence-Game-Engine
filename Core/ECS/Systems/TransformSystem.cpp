#include "Core/ECS/Systems/TransformSystem.h"

namespace Core {
namespace ECS {

    void TransformSystem::Update(Scene& scene)
    {
        PROFILE_FUNCTION();

        auto& registry = scene.GetRegistry();

        // First pass: Update all root transforms and mark children dirty if parent changed
        auto rootView = registry.view<TransformComponent>(entt::exclude<HierarchyComponent>);
        for (auto entity : rootView) {
            auto& transform = rootView.get<TransformComponent>(entity);
            if (transform.IsDirty) {
                transform.WorldMatrix = transform.GetLocalMatrix();
                transform.IsDirty = false;
            }
        }

        // Also handle entities with HierarchyComponent but no parent (roots with hierarchy)
        auto hierarchyView = registry.view<TransformComponent, HierarchyComponent>();
        
        // Sort by depth for correct parent-before-child ordering
        m_SortedEntities.clear();
        for (auto entity : hierarchyView) {
            m_SortedEntities.push_back(entity);
        }

        std::sort(m_SortedEntities.begin(), m_SortedEntities.end(),
            [&registry](entt::entity a, entt::entity b) {
                auto& hierA = registry.get<HierarchyComponent>(a);
                auto& hierB = registry.get<HierarchyComponent>(b);
                return hierA.Depth < hierB.Depth;
            });

        // Process in depth order
        for (auto entity : m_SortedEntities) {
            auto& transform = registry.get<TransformComponent>(entity);
            auto& hierarchy = registry.get<HierarchyComponent>(entity);

            if (hierarchy.HasParent()) {
                // Get parent's world matrix
                auto* parentTransform = registry.try_get<TransformComponent>(hierarchy.Parent);
                if (parentTransform) {
                    // If parent is dirty, child must also update
                    if (parentTransform->IsDirty || transform.IsDirty) {
                        transform.WorldMatrix = parentTransform->WorldMatrix * transform.GetLocalMatrix();
                        transform.IsDirty = false;
                    }
                } else {
                    // Parent doesn't have transform, treat as root
                    if (transform.IsDirty) {
                        transform.WorldMatrix = transform.GetLocalMatrix();
                        transform.IsDirty = false;
                    }
                }
            } else {
                // Root entity with hierarchy component
                if (transform.IsDirty) {
                    transform.WorldMatrix = transform.GetLocalMatrix();
                    transform.IsDirty = false;
                }
            }
        }

        ENGINE_CORE_TRACE("TransformSystem: Updated {} hierarchical entities", m_SortedEntities.size());
    }

    void TransformSystem::ForceUpdateAll(Scene& scene)
    {
        PROFILE_FUNCTION();

        auto& registry = scene.GetRegistry();

        // Mark all transforms as dirty
        auto view = registry.view<TransformComponent>();
        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            transform.IsDirty = true;
        }

        // Run normal update
        Update(scene);
    }

    void TransformSystem::SetParent(Scene& scene, entt::entity child, entt::entity parent)
    {
        auto& registry = scene.GetRegistry();

        // Ensure child has hierarchy component
        if (!registry.all_of<HierarchyComponent>(child)) {
            registry.emplace<HierarchyComponent>(child);
        }

        auto& childHierarchy = registry.get<HierarchyComponent>(child);

        // Remove from old parent if exists
        if (childHierarchy.HasParent()) {
            auto* oldParentHierarchy = registry.try_get<HierarchyComponent>(childHierarchy.Parent);
            if (oldParentHierarchy) {
                auto& children = oldParentHierarchy->Children;
                children.erase(std::remove(children.begin(), children.end(), child), children.end());
            }
        }

        // Set new parent
        childHierarchy.Parent = parent;

        if (parent != entt::null) {
            // Ensure parent has hierarchy component
            if (!registry.all_of<HierarchyComponent>(parent)) {
                registry.emplace<HierarchyComponent>(parent);
            }

            auto& parentHierarchy = registry.get<HierarchyComponent>(parent);
            parentHierarchy.Children.push_back(child);

            // Update depths
            UpdateDepths(scene, child, parentHierarchy.Depth + 1);
        } else {
            childHierarchy.Depth = 0;
            UpdateDepths(scene, child, 0);
        }

        // Mark transform as dirty
        if (auto* transform = registry.try_get<TransformComponent>(child)) {
            transform->IsDirty = true;
        }

        ENGINE_CORE_TRACE("TransformSystem: Set parent for entity");
    }

    void TransformSystem::RemoveParent(Scene& scene, entt::entity child)
    {
        SetParent(scene, child, entt::null);
    }

    void TransformSystem::UpdateDepths(Scene& scene, entt::entity entity, uint32_t depth)
    {
        auto& registry = scene.GetRegistry();

        auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
        if (!hierarchy) return;

        hierarchy->Depth = depth;

        // Recursively update children
        for (auto childEntity : hierarchy->Children) {
            UpdateDepths(scene, childEntity, depth + 1);
        }
    }

    void TransformSystem::UpdateEntityTransform(entt::registry& registry, entt::entity entity,
                                                 const Math::Mat4& parentWorldMatrix)
    {
        auto* transform = registry.try_get<TransformComponent>(entity);
        if (!transform) return;

        transform->WorldMatrix = parentWorldMatrix * transform->GetLocalMatrix();
        transform->IsDirty = false;

        // Update children recursively
        auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
        if (hierarchy) {
            for (auto child : hierarchy->Children) {
                UpdateEntityTransform(registry, child, transform->WorldMatrix);
            }
        }
    }

    void TransformSystem::ProcessHierarchy(Scene& scene)
    {
        PROFILE_FUNCTION();

        auto& registry = scene.GetRegistry();

        // Find all root entities (no parent or parent is null)
        auto view = registry.view<TransformComponent, HierarchyComponent>();
        
        for (auto entity : view) {
            auto& hierarchy = view.get<HierarchyComponent>(entity);
            if (!hierarchy.HasParent()) {
                // This is a root, process its subtree
                UpdateEntityTransform(registry, entity, Math::Mat4(1.0f));
            }
        }
    }

} // namespace ECS
} // namespace Core
