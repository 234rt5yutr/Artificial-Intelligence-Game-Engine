#include "EditorSelection.h"

namespace Core {
namespace Editor {

    bool IsEntitySelectable(const EditorContext& ctx, entt::entity entity) {
        if (!ctx.ActiveScene || entity == entt::null) {
            return false;
        }

        const auto& registry = ctx.ActiveScene->GetRegistry();
        return registry.valid(entity);
    }

    bool SelectEntity(EditorContext& ctx, entt::entity entity, std::string_view source) {
        if (!IsEntitySelectable(ctx, entity)) {
            return false;
        }

        if (ctx.Selection.Primary == entity && ctx.Selection.Source == source) {
            return true;
        }

        ctx.Selection.Primary = entity;
        ctx.Selection.SelectionSet.clear();
        ctx.Selection.SelectionSet.push_back(entity);
        ctx.Selection.Source = std::string(source);
        ctx.Selection.Dirty = true;
        ctx.Dirty = true;
        return true;
    }

} // namespace Editor
} // namespace Core

