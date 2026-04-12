#include "SceneHierarchyPanel.h"

#include "Core/Editor/EditorAPI.h"
#include "Core/Editor/EditorContext.h"
#include "Core/ECS/Components/HierarchyComponent.h"
#include "Core/ECS/Components/NameComponent.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Core {
namespace Editor {
namespace {

    struct HierarchyRow {
        entt::entity Entity = entt::null;
        uint32_t Depth = 0;
        bool HasChildren = false;
        bool Expanded = false;
    };

    std::string ToLowerCopy(std::string_view text) {
        std::string lowered(text.begin(), text.end());
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return lowered;
    }

    bool ContainsCaseInsensitive(std::string_view text, std::string_view query) {
        if (query.empty()) {
            return true;
        }
        return ToLowerCopy(text).find(ToLowerCopy(query)) != std::string::npos;
    }

    std::string GetEntityDisplayName(const entt::registry& registry, entt::entity entity) {
        if (const auto* name = registry.try_get<ECS::NameComponent>(entity)) {
            if (!name->Name.empty()) {
                return name->Name;
            }
        }
        return std::string("Entity ") + std::to_string(static_cast<uint32_t>(entity));
    }

    void SortEntitiesByDisplayName(std::vector<entt::entity>& entities, const entt::registry& registry) {
        std::sort(entities.begin(), entities.end(), [&registry](entt::entity lhs, entt::entity rhs) {
            const std::string lhsName = GetEntityDisplayName(registry, lhs);
            const std::string rhsName = GetEntityDisplayName(registry, rhs);
            if (lhsName == rhsName) {
                return static_cast<uint32_t>(lhs) < static_cast<uint32_t>(rhs);
            }
            return lhsName < rhsName;
        });
    }

    bool NodeMatchesFilter(entt::entity entity,
                           const std::unordered_map<uint32_t, std::vector<entt::entity>>& childrenMap,
                           const entt::registry& registry,
                           std::string_view searchQuery,
                           std::unordered_set<uint32_t>& recursionGuard) {
        const uint32_t entityId = static_cast<uint32_t>(entity);
        if (recursionGuard.contains(entityId)) {
            return false;
        }
        recursionGuard.insert(entityId);

        const std::string name = GetEntityDisplayName(registry, entity);
        if (ContainsCaseInsensitive(name, searchQuery)) {
            recursionGuard.erase(entityId);
            return true;
        }

        const auto it = childrenMap.find(entityId);
        if (it != childrenMap.end()) {
            for (const entt::entity child : it->second) {
                if (NodeMatchesFilter(child, childrenMap, registry, searchQuery, recursionGuard)) {
                    recursionGuard.erase(entityId);
                    return true;
                }
            }
        }

        recursionGuard.erase(entityId);
        return false;
    }

    void FlattenVisibleRows(std::vector<HierarchyRow>& outRows,
                            entt::entity entity,
                            uint32_t depth,
                            const std::unordered_map<uint32_t, std::vector<entt::entity>>& childrenMap,
                            const entt::registry& registry,
                            const SceneHierarchyPanelState& state,
                            std::string_view searchQuery) {
        const uint32_t entityId = static_cast<uint32_t>(entity);
        const auto childrenIt = childrenMap.find(entityId);
        const bool hasChildren = childrenIt != childrenMap.end() && !childrenIt->second.empty();
        const bool expanded = state.ExpandedEntities.contains(entityId);

        outRows.push_back(HierarchyRow{entity, depth, hasChildren, expanded});

        if (!hasChildren || !expanded) {
            return;
        }

        for (const entt::entity child : childrenIt->second) {
            std::unordered_set<uint32_t> recursionGuard;
            if (!searchQuery.empty() &&
                !NodeMatchesFilter(child, childrenMap, registry, searchQuery, recursionGuard)) {
                continue;
            }
            FlattenVisibleRows(outRows, child, depth + 1, childrenMap, registry, state, searchQuery);
        }
    }

    void BuildHierarchyRows(std::vector<HierarchyRow>& rows, EditorContext& ctx) {
        rows.clear();
        if (!ctx.ActiveScene) {
            return;
        }

        auto& registry = ctx.ActiveScene->GetRegistry();
        std::vector<entt::entity> roots;
        std::unordered_map<uint32_t, std::vector<entt::entity>> childrenMap;

        auto& entityStorage = registry.storage<entt::entity>();
        for (const entt::entity entity : entityStorage) {
            const auto* hierarchy = registry.try_get<ECS::HierarchyComponent>(entity);
            if (!hierarchy || hierarchy->Parent == entt::null || !registry.valid(hierarchy->Parent)) {
                roots.push_back(entity);
                continue;
            }

            childrenMap[static_cast<uint32_t>(hierarchy->Parent)].push_back(entity);
        }

        for (auto& [_, children] : childrenMap) {
            SortEntitiesByDisplayName(children, registry);
        }
        SortEntitiesByDisplayName(roots, registry);

        for (const entt::entity root : roots) {
            std::unordered_set<uint32_t> recursionGuard;
            if (!ctx.Panels.Hierarchy.SearchQuery.empty() &&
                !NodeMatchesFilter(root, childrenMap, registry, ctx.Panels.Hierarchy.SearchQuery, recursionGuard)) {
                continue;
            }
            FlattenVisibleRows(rows, root, 0, childrenMap, registry, ctx.Panels.Hierarchy, ctx.Panels.Hierarchy.SearchQuery);
        }
    }

} // namespace

    void DrawSceneHierarchyPanel(EditorContext& ctx) {
        if (!ctx.Panels.Hierarchy.Open) {
            return;
        }

        if (!ImGui::Begin("Scene Hierarchy", &ctx.Panels.Hierarchy.Open)) {
            ImGui::End();
            return;
        }

        if (!ctx.ActiveScene) {
            ImGui::TextUnformatted("No active scene.");
            ImGui::End();
            return;
        }

        constexpr std::size_t kMaxSearchLength = 255;
        char searchBuffer[kMaxSearchLength + 1] = {};
        const std::size_t copyLength = std::min(ctx.Panels.Hierarchy.SearchQuery.size(), kMaxSearchLength);
        std::memcpy(searchBuffer, ctx.Panels.Hierarchy.SearchQuery.data(), copyLength);
        searchBuffer[copyLength] = '\0';

        if (ImGui::InputTextWithHint("##HierarchySearch", "Search entities...", searchBuffer, sizeof(searchBuffer))) {
            ctx.Panels.Hierarchy.SearchQuery = searchBuffer;
        }

        ImGui::Separator();

        std::vector<HierarchyRow> rows;
        BuildHierarchyRows(rows, ctx);

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const HierarchyRow& row = rows[static_cast<std::size_t>(i)];
                const uint32_t entityId = static_cast<uint32_t>(row.Entity);
                const std::string label = GetEntityDisplayName(ctx.ActiveScene->GetRegistry(), row.Entity);

                ImGui::PushID(static_cast<int>(entityId));

                const float indentPixels = static_cast<float>(row.Depth) * 14.0f;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indentPixels);

                if (row.HasChildren) {
                    if (ImGui::SmallButton(row.Expanded ? "v" : ">")) {
                        if (row.Expanded) {
                            ctx.Panels.Hierarchy.ExpandedEntities.erase(entityId);
                        } else {
                            ctx.Panels.Hierarchy.ExpandedEntities.insert(entityId);
                        }
                    }
                    ImGui::SameLine();
                } else {
                    ImGui::Dummy(ImVec2(20.0f, 0.0f));
                    ImGui::SameLine();
                }

                const bool selected = (ctx.Selection.Primary == row.Entity);
                if (ImGui::Selectable(label.c_str(), selected)) {
                    SelectEntityInEditor(ctx, row.Entity, "hierarchy");
                }

                ImGui::PopID();
            }
        }

        ImGui::End();
    }

} // namespace Editor
} // namespace Core

