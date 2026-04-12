#include "InspectorPanel.h"

#include "Core/Editor/EditorAPI.h"
#include "Core/Editor/EditorContext.h"
#include "Core/ECS/Components/CameraComponent.h"
#include "Core/ECS/Components/LightComponent.h"
#include "Core/ECS/Components/NameComponent.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/UIComponent.h"
#include "Core/ECS/Components/WorldUIComponent.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace Core {
namespace Editor {
namespace {

    void SubmitEdit(EditorContext& ctx,
                    entt::entity entity,
                    std::string_view componentType,
                    std::string_view propertyPath,
                    const EditorJson& value,
                    std::string_view label) {
        PropertyEditRequest request;
        request.Entity = entity;
        request.ComponentType = std::string(componentType);
        request.PropertyPath = std::string(propertyPath);
        request.NewValue = value;
        request.TransactionLabel = std::string(label);
        EditComponentPropertiesInEditor(ctx, request);
    }

    entt::entity ResolveTargetEntity(const EditorContext& ctx) {
        if (ctx.Panels.Inspector.LockSelection && ctx.ActiveScene) {
            const auto& registry = ctx.ActiveScene->GetRegistry();
            if (registry.valid(ctx.Panels.Inspector.LockedEntity)) {
                return ctx.Panels.Inspector.LockedEntity;
            }
        }
        return ctx.Selection.Primary;
    }

} // namespace

    void DrawInspectorPanel(EditorContext& ctx) {
        if (!ctx.Panels.Inspector.Open) {
            return;
        }

        if (!ImGui::Begin("Inspector", &ctx.Panels.Inspector.Open)) {
            ImGui::End();
            return;
        }

        if (!ctx.ActiveScene) {
            ImGui::TextUnformatted("No active scene.");
            ImGui::End();
            return;
        }

        auto& registry = ctx.ActiveScene->GetRegistry();
        const entt::entity targetEntity = ResolveTargetEntity(ctx);

        if (targetEntity == entt::null || !registry.valid(targetEntity)) {
            ImGui::TextUnformatted("No entity selected.");
            ImGui::End();
            return;
        }

        ImGui::Text("Entity: %u", static_cast<uint32_t>(targetEntity));
        ImGui::Text("Selected from: %s", ctx.Selection.Source.c_str());

        bool lockSelection = ctx.Panels.Inspector.LockSelection;
        if (ImGui::Checkbox("Lock selection", &lockSelection)) {
            ctx.Panels.Inspector.LockSelection = lockSelection;
            if (lockSelection) {
                ctx.Panels.Inspector.LockedEntity = targetEntity;
            } else {
                ctx.Panels.Inspector.LockedEntity = entt::null;
            }
        }

        ImGui::Separator();

        if (auto* name = registry.try_get<ECS::NameComponent>(targetEntity)) {
            if (ImGui::CollapsingHeader("Name", ImGuiTreeNodeFlags_DefaultOpen)) {
                constexpr std::size_t kNameLength = 255;
                std::array<char, kNameLength + 1> buffer{};
                const std::size_t copyLength = std::min(name->Name.size(), kNameLength);
                std::memcpy(buffer.data(), name->Name.data(), copyLength);
                buffer[copyLength] = '\0';

                if (ImGui::InputText("Display Name", buffer.data(), buffer.size())) {
                    SubmitEdit(ctx, targetEntity, "name", "name", std::string(buffer.data()), "Edit Name");
                }
            }
        }

        if (auto* transform = registry.try_get<ECS::TransformComponent>(targetEntity)) {
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                float position[3] = {transform->Position.x, transform->Position.y, transform->Position.z};
                if (ImGui::DragFloat3("Position", position, 0.05f)) {
                    SubmitEdit(ctx,
                               targetEntity,
                               "transform",
                               "position",
                               EditorJson{{"x", position[0]}, {"y", position[1]}, {"z", position[2]}},
                               "Edit Transform Position");
                }

                float rotation[3] = {
                    glm::degrees(transform->Rotation.x),
                    glm::degrees(transform->Rotation.y),
                    glm::degrees(transform->Rotation.z)};
                if (ImGui::DragFloat3("Rotation (deg)", rotation, 0.25f)) {
                    SubmitEdit(ctx,
                               targetEntity,
                               "transform",
                               "rotation",
                               EditorJson{{"x", glm::radians(rotation[0])},
                                          {"y", glm::radians(rotation[1])},
                                          {"z", glm::radians(rotation[2])}},
                               "Edit Transform Rotation");
                }

                float scale[3] = {transform->Scale.x, transform->Scale.y, transform->Scale.z};
                if (ImGui::DragFloat3("Scale", scale, 0.01f, 0.001f, 1000.0f)) {
                    SubmitEdit(ctx,
                               targetEntity,
                               "transform",
                               "scale",
                               EditorJson{{"x", scale[0]}, {"y", scale[1]}, {"z", scale[2]}},
                               "Edit Transform Scale");
                }
            }
        }

        if (auto* light = registry.try_get<ECS::LightComponent>(targetEntity)) {
            if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
                int lightType = static_cast<int>(light->Type);
                static constexpr const char* kLightTypes[] = {"Directional", "Point", "Spot"};
                if (ImGui::Combo("Type", &lightType, kLightTypes, IM_ARRAYSIZE(kLightTypes))) {
                    SubmitEdit(ctx, targetEntity, "light", "type", lightType, "Edit Light Type");
                }

                float color[3] = {light->Color.r, light->Color.g, light->Color.b};
                if (ImGui::ColorEdit3("Color", color)) {
                    SubmitEdit(ctx,
                               targetEntity,
                               "light",
                               "color",
                               EditorJson{{"x", color[0]}, {"y", color[1]}, {"z", color[2]}},
                               "Edit Light Color");
                }

                float intensity = light->Intensity;
                if (ImGui::DragFloat("Intensity", &intensity, 0.05f, 0.0f, 10000.0f)) {
                    SubmitEdit(ctx, targetEntity, "light", "intensity", intensity, "Edit Light Intensity");
                }

                if (light->Type != ECS::LightType::Directional) {
                    float radius = light->Radius;
                    if (ImGui::DragFloat("Radius", &radius, 0.05f, 0.0f, 10000.0f)) {
                        SubmitEdit(ctx, targetEntity, "light", "radius", radius, "Edit Light Radius");
                    }
                }

                bool castShadows = light->CastShadows;
                if (ImGui::Checkbox("Cast Shadows", &castShadows)) {
                    SubmitEdit(ctx, targetEntity, "light", "castShadows", castShadows, "Edit Light Shadows");
                }

                bool enabled = light->Enabled;
                if (ImGui::Checkbox("Enabled", &enabled)) {
                    SubmitEdit(ctx, targetEntity, "light", "enabled", enabled, "Edit Light Enabled");
                }
            }
        }

        if (auto* camera = registry.try_get<ECS::CameraComponent>(targetEntity)) {
            if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
                int projection = static_cast<int>(camera->Projection);
                static constexpr const char* kProjectionTypes[] = {"Perspective", "Orthographic"};
                if (ImGui::Combo("Projection", &projection, kProjectionTypes, IM_ARRAYSIZE(kProjectionTypes))) {
                    SubmitEdit(ctx, targetEntity, "camera", "projection", projection, "Edit Camera Projection");
                }

                float fov = camera->FieldOfView;
                if (ImGui::DragFloat("Field of View", &fov, 0.1f, 1.0f, 179.0f)) {
                    SubmitEdit(ctx, targetEntity, "camera", "fieldOfView", fov, "Edit Camera FOV");
                }

                float nearPlane = camera->NearPlane;
                if (ImGui::DragFloat("Near Plane", &nearPlane, 0.001f, 0.001f, 1000.0f, "%.4f")) {
                    SubmitEdit(ctx, targetEntity, "camera", "nearPlane", nearPlane, "Edit Camera Near Plane");
                }

                float farPlane = camera->FarPlane;
                if (ImGui::DragFloat("Far Plane", &farPlane, 0.1f, 0.1f, 100000.0f)) {
                    SubmitEdit(ctx, targetEntity, "camera", "farPlane", farPlane, "Edit Camera Far Plane");
                }

                float orthoSize = camera->OrthoSize;
                if (ImGui::DragFloat("Ortho Size", &orthoSize, 0.1f, 0.1f, 10000.0f)) {
                    SubmitEdit(ctx, targetEntity, "camera", "orthoSize", orthoSize, "Edit Camera Ortho Size");
                }

                bool active = camera->IsActive;
                if (ImGui::Checkbox("Active", &active)) {
                    SubmitEdit(ctx, targetEntity, "camera", "isActive", active, "Edit Camera Active");
                }
            }
        }

        if (auto* ui = registry.try_get<ECS::UIComponent>(targetEntity)) {
            if (ImGui::CollapsingHeader("UI")) {
                constexpr std::size_t kTextLength = 255;
                std::array<char, kTextLength + 1> buffer{};
                const std::size_t copyLength = std::min(ui->Text.size(), kTextLength);
                std::memcpy(buffer.data(), ui->Text.data(), copyLength);
                buffer[copyLength] = '\0';

                if (ImGui::InputText("Text", buffer.data(), buffer.size())) {
                    SubmitEdit(ctx, targetEntity, "ui", "text", std::string(buffer.data()), "Edit UI Text");
                }

                float progress = ui->Progress;
                if (ImGui::SliderFloat("Progress", &progress, 0.0f, 1.0f)) {
                    SubmitEdit(ctx, targetEntity, "ui", "progress", progress, "Edit UI Progress");
                }

                bool visible = ui->Visible;
                if (ImGui::Checkbox("Visible##ui", &visible)) {
                    SubmitEdit(ctx, targetEntity, "ui", "visible", visible, "Edit UI Visibility");
                }
            }
        }

        if (auto* worldUi = registry.try_get<ECS::WorldUIComponent>(targetEntity)) {
            if (ImGui::CollapsingHeader("World UI")) {
                constexpr std::size_t kTextLength = 255;
                std::array<char, kTextLength + 1> buffer{};
                const std::size_t copyLength = std::min(worldUi->Text.size(), kTextLength);
                std::memcpy(buffer.data(), worldUi->Text.data(), copyLength);
                buffer[copyLength] = '\0';

                if (ImGui::InputText("Text##worldui", buffer.data(), buffer.size())) {
                    SubmitEdit(ctx, targetEntity, "worldUI", "text", std::string(buffer.data()), "Edit WorldUI Text");
                }

                float progress = worldUi->Progress;
                if (ImGui::SliderFloat("Progress##worldui", &progress, 0.0f, 1.0f)) {
                    SubmitEdit(ctx, targetEntity, "worldUI", "progress", progress, "Edit WorldUI Progress");
                }

                bool visible = worldUi->Visible;
                if (ImGui::Checkbox("Visible##worldui", &visible)) {
                    SubmitEdit(ctx, targetEntity, "worldUI", "visible", visible, "Edit WorldUI Visibility");
                }
            }
        }

        ImGui::End();
    }

} // namespace Editor
} // namespace Core

