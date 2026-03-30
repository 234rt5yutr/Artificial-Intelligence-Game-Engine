#pragma once

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/HierarchyComponent.h"
#include "Core/Profile.h"
#include "Core/Log.h"
#include <vector>
#include <algorithm>

namespace Core {
namespace ECS {

    class TransformSystem {
    public:
        TransformSystem() = default;
        ~TransformSystem() = default;

        // Update all transforms in the scene
        void Update(Scene& scene);

        // Force update all transforms (ignores dirty flags)
        void ForceUpdateAll(Scene& scene);

        // Hierarchy management helpers
        static void SetParent(Scene& scene, entt::entity child, entt::entity parent);
        static void RemoveParent(Scene& scene, entt::entity child);
        static void UpdateDepths(Scene& scene, entt::entity entity, uint32_t depth);

    private:
        // Update a single entity's world matrix
        void UpdateEntityTransform(entt::registry& registry, entt::entity entity, 
                                   const Math::Mat4& parentWorldMatrix);

        // Process hierarchy in correct order (parents before children)
        void ProcessHierarchy(Scene& scene);

        // Cached sorted entities for efficient iteration
        std::vector<entt::entity> m_SortedEntities;
    };

} // namespace ECS
} // namespace Core
