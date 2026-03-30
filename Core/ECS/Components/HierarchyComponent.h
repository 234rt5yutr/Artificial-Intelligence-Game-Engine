#pragma once

#include <entt/entt.hpp>
#include <vector>

namespace Core {
namespace ECS {

    struct HierarchyComponent {
        entt::entity Parent{ entt::null };
        std::vector<entt::entity> Children;

        // Hierarchy depth for sorting (root = 0)
        uint32_t Depth = 0;

        HierarchyComponent() = default;
        explicit HierarchyComponent(entt::entity parent)
            : Parent(parent), Depth(0) {}

        bool HasParent() const { return Parent != entt::null; }
        bool HasChildren() const { return !Children.empty(); }
    };

} // namespace ECS
} // namespace Core
