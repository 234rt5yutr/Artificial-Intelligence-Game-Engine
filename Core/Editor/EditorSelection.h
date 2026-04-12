#pragma once

#include "EditorContext.h"

#include <string_view>

namespace Core {
namespace Editor {

    bool IsEntitySelectable(const EditorContext& ctx, entt::entity entity);
    bool SelectEntity(EditorContext& ctx, entt::entity entity, std::string_view source);

} // namespace Editor
} // namespace Core

