#include "EditorAPI.h"

#include "Core/Editor/Commands\ComponentPropertyCommand.h"
#include "Core/Editor/EditorSelection.h"
#include "Core/Editor/Panels/InspectorPanel.h"
#include "Core/Editor/Panels/SceneHierarchyPanel.h"
#include "Core/ECS/Components/CameraComponent.h"
#include "Core/ECS/Components/HierarchyComponent.h"
#include "Core/ECS/Components/LightComponent.h"
#include "Core/ECS/Components/MeshComponent.h"
#include "Core/ECS/Components/NameComponent.h"
#include "Core/ECS/Components/PrefabInstanceComponent.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/UIComponent.h"
#include "Core/ECS/Components/WorldUIComponent.h"
#include "Core/ECS/Entity.h"
#include "Core/Log.h"
#include "Core/Security/PathValidator.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <queue>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Core {
namespace Editor {
namespace {

    constexpr uint32_t kPrefabSchemaVersion = 1;
    constexpr uint32_t kGraphSchemaVersion = 1;
    constexpr uint32_t kTimelineSchemaVersion = 1;

    std::string GenerateGuid(std::string_view prefix) {
        static std::atomic<uint64_t> counter{1};
        const uint64_t tick = static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        const uint64_t serial = counter.fetch_add(1, std::memory_order_relaxed);
        std::ostringstream builder;
        builder << prefix << "-" << std::hex << tick << "-" << serial;
        return builder.str();
    }

    float ClampFloat(float value, float minimum, float maximum) {
        if (std::isnan(value) || std::isinf(value)) {
            return minimum;
        }
        return std::clamp(value, minimum, maximum);
    }

    bool JsonToVec3(const EditorJson& value, Math::Vec3& outVec3) {
        if (!value.is_object()) {
            return false;
        }

        outVec3 = Math::Vec3(
            value.value("x", outVec3.x),
            value.value("y", outVec3.y),
            value.value("z", outVec3.z));
        return true;
    }

    EditorJson Vec3ToJson(const Math::Vec3& value) {
        return EditorJson{{"x", value.x}, {"y", value.y}, {"z", value.z}};
    }

    EditorJson SerializeTransformComponent(const ECS::TransformComponent& transform) {
        return EditorJson{
            {"position", Vec3ToJson(transform.Position)},
            {"rotation", Vec3ToJson(transform.Rotation)},
            {"scale", Vec3ToJson(transform.Scale)}
        };
    }

    ECS::TransformComponent DeserializeTransformComponent(const EditorJson& value) {
        ECS::TransformComponent transform;
        if (value.contains("position")) {
            JsonToVec3(value["position"], transform.Position);
        }
        if (value.contains("rotation")) {
            JsonToVec3(value["rotation"], transform.Rotation);
        }
        if (value.contains("scale")) {
            JsonToVec3(value["scale"], transform.Scale);
        }
        transform.IsDirty = true;
        return transform;
    }

    EditorJson SerializeLightComponent(const ECS::LightComponent& light) {
        return EditorJson{
            {"type", static_cast<int>(light.Type)},
            {"color", Vec3ToJson(light.Color)},
            {"intensity", light.Intensity},
            {"radius", light.Radius},
            {"innerCutoff", light.InnerCutoff},
            {"outerCutoff", light.OuterCutoff},
            {"castShadows", light.CastShadows},
            {"enabled", light.Enabled}
        };
    }

    ECS::LightComponent DeserializeLightComponent(const EditorJson& value) {
        ECS::LightComponent light;
        light.Type = static_cast<ECS::LightType>(std::clamp(value.value("type", 1), 0, 2));
        JsonToVec3(value.value("color", EditorJson::object()), light.Color);
        light.Intensity = value.value("intensity", 1.0f);
        light.Radius = value.value("radius", 10.0f);
        light.InnerCutoff = value.value("innerCutoff", 0.0f);
        light.OuterCutoff = value.value("outerCutoff", 0.0f);
        light.CastShadows = value.value("castShadows", false);
        light.Enabled = value.value("enabled", true);
        return light;
    }

    EditorJson SerializeMeshComponent(const ECS::MeshComponent& mesh) {
        return EditorJson{
            {"meshPath", mesh.MeshPath},
            {"materialIndex", mesh.MaterialIndex},
            {"visible", mesh.Visible},
            {"castShadows", mesh.CastShadows},
            {"receiveShadows", mesh.ReceiveShadows}
        };
    }

    ECS::MeshComponent DeserializeMeshComponent(const EditorJson& value) {
        ECS::MeshComponent mesh;
        mesh.MeshPath = value.value("meshPath", std::string{});
        mesh.MaterialIndex = value.value("materialIndex", 0);
        mesh.Visible = value.value("visible", true);
        mesh.CastShadows = value.value("castShadows", true);
        mesh.ReceiveShadows = value.value("receiveShadows", true);
        return mesh;
    }

    EditorJson SerializeCameraComponent(const ECS::CameraComponent& camera) {
        return EditorJson{
            {"projection", static_cast<int>(camera.Projection)},
            {"fieldOfView", camera.FieldOfView},
            {"nearPlane", camera.NearPlane},
            {"farPlane", camera.FarPlane},
            {"aspectRatio", camera.AspectRatio},
            {"orthoSize", camera.OrthoSize},
            {"isActive", camera.IsActive}
        };
    }

    ECS::CameraComponent DeserializeCameraComponent(const EditorJson& value) {
        ECS::CameraComponent camera;
        camera.Projection = static_cast<ECS::ProjectionType>(std::clamp(value.value("projection", 0), 0, 1));
        camera.FieldOfView = value.value("fieldOfView", 60.0f);
        camera.NearPlane = value.value("nearPlane", 0.1f);
        camera.FarPlane = value.value("farPlane", 1000.0f);
        camera.AspectRatio = value.value("aspectRatio", 16.0f / 9.0f);
        camera.OrthoSize = value.value("orthoSize", 10.0f);
        camera.IsActive = value.value("isActive", false);
        camera.IsDirty = true;
        return camera;
    }

    EditorJson SerializeUIComponent(const ECS::UIComponent& ui) {
        return EditorJson{
            {"anchor", static_cast<int>(ui.Anchor)},
            {"offset", EditorJson{{"x", ui.Offset.x}, {"y", ui.Offset.y}}},
            {"size", EditorJson{{"x", ui.Size.x}, {"y", ui.Size.y}}},
            {"type", static_cast<int>(ui.Type)},
            {"widgetId", ui.WidgetId},
            {"text", ui.Text},
            {"fontSize", ui.FontSize},
            {"fontFamily", ui.FontFamily},
            {"visible", ui.Visible},
            {"interactive", ui.Interactive},
            {"zOrder", ui.ZOrder},
            {"progress", ui.Progress}
        };
    }

    ECS::UIComponent DeserializeUIComponent(const EditorJson& value) {
        ECS::UIComponent ui;
        ui.Anchor = static_cast<UI::Anchor>(value.value("anchor", static_cast<int>(UI::Anchor::TopLeft)));
        ui.Offset.x = value.value("offset", EditorJson::object()).value("x", 0.0f);
        ui.Offset.y = value.value("offset", EditorJson::object()).value("y", 0.0f);
        ui.Size.x = value.value("size", EditorJson::object()).value("x", 100.0f);
        ui.Size.y = value.value("size", EditorJson::object()).value("y", 100.0f);
        ui.Type = static_cast<ECS::WidgetType>(value.value("type", static_cast<int>(ECS::WidgetType::None)));
        ui.WidgetId = value.value("widgetId", std::string{});
        ui.Text = value.value("text", std::string{});
        ui.FontSize = value.value("fontSize", 16.0f);
        ui.FontFamily = value.value("fontFamily", std::string{"default"});
        ui.Visible = value.value("visible", true);
        ui.Interactive = value.value("interactive", false);
        ui.ZOrder = value.value("zOrder", 0);
        ui.Progress = value.value("progress", 1.0f);
        ui.IsDirty = true;
        return ui;
    }

    EditorJson SerializeWorldUIComponent(const ECS::WorldUIComponent& worldUi) {
        return EditorJson{
            {"localOffset", Vec3ToJson(Math::Vec3(worldUi.LocalOffset))},
            {"size", EditorJson{{"x", worldUi.Size.x}, {"y", worldUi.Size.y}}},
            {"type", static_cast<int>(worldUi.Type)},
            {"widgetId", worldUi.WidgetId},
            {"text", worldUi.Text},
            {"fontSize", worldUi.FontSize},
            {"fontFamily", worldUi.FontFamily},
            {"visible", worldUi.Visible},
            {"progress", worldUi.Progress}
        };
    }

    ECS::WorldUIComponent DeserializeWorldUIComponent(const EditorJson& value) {
        ECS::WorldUIComponent worldUi;
        Math::Vec3 offset = Math::Vec3(worldUi.LocalOffset);
        JsonToVec3(value.value("localOffset", EditorJson::object()), offset);
        worldUi.LocalOffset = glm::vec3(offset.x, offset.y, offset.z);
        worldUi.Size.x = value.value("size", EditorJson::object()).value("x", 100.0f);
        worldUi.Size.y = value.value("size", EditorJson::object()).value("y", 20.0f);
        worldUi.Type = static_cast<ECS::WidgetType>(value.value("type", static_cast<int>(ECS::WidgetType::Label)));
        worldUi.WidgetId = value.value("widgetId", std::string{});
        worldUi.Text = value.value("text", std::string{});
        worldUi.FontSize = value.value("fontSize", 14.0f);
        worldUi.FontFamily = value.value("fontFamily", std::string{"default"});
        worldUi.Visible = value.value("visible", true);
        worldUi.Progress = value.value("progress", 1.0f);
        worldUi.IsDirty = true;
        return worldUi;
    }

    EditorJson SerializeEntityForPrefab(entt::entity entity, const entt::registry& registry) {
        EditorJson serializedEntity{
            {"id", static_cast<uint32_t>(entity)},
            {"components", EditorJson::object()}
        };

        if (const auto* name = registry.try_get<ECS::NameComponent>(entity)) {
            serializedEntity["name"] = name->Name;
        }

        if (const auto* transform = registry.try_get<ECS::TransformComponent>(entity)) {
            serializedEntity["components"]["transform"] = SerializeTransformComponent(*transform);
        }
        if (const auto* light = registry.try_get<ECS::LightComponent>(entity)) {
            serializedEntity["components"]["light"] = SerializeLightComponent(*light);
        }
        if (const auto* mesh = registry.try_get<ECS::MeshComponent>(entity)) {
            serializedEntity["components"]["mesh"] = SerializeMeshComponent(*mesh);
        }
        if (const auto* camera = registry.try_get<ECS::CameraComponent>(entity)) {
            serializedEntity["components"]["camera"] = SerializeCameraComponent(*camera);
        }
        if (const auto* ui = registry.try_get<ECS::UIComponent>(entity)) {
            serializedEntity["components"]["ui"] = SerializeUIComponent(*ui);
        }
        if (const auto* worldUi = registry.try_get<ECS::WorldUIComponent>(entity)) {
            serializedEntity["components"]["worldUI"] = SerializeWorldUIComponent(*worldUi);
        }

        if (const auto* hierarchy = registry.try_get<ECS::HierarchyComponent>(entity)) {
            if (hierarchy->Parent != entt::null) {
                serializedEntity["parent"] = static_cast<uint32_t>(hierarchy->Parent);
            } else {
                serializedEntity["parent"] = nullptr;
            }

            EditorJson children = EditorJson::array();
            for (const entt::entity child : hierarchy->Children) {
                children.push_back(static_cast<uint32_t>(child));
            }
            serializedEntity["children"] = std::move(children);
        }

        return serializedEntity;
    }

    Result<EditorJson> ReadPropertyValue(const EditorContext& ctx,
                                         entt::entity entity,
                                         const std::string& componentType,
                                         const std::string& propertyPath) {
        if (!ctx.ActiveScene) {
            return Result<EditorJson>::Failure("Editor.InvalidScene");
        }

        const auto& registry = ctx.ActiveScene->GetRegistry();
        if (!registry.valid(entity)) {
            return Result<EditorJson>::Failure("Editor.InvalidEntity");
        }

        if (componentType == "name") {
            if (const auto* component = registry.try_get<ECS::NameComponent>(entity)) {
                if (propertyPath == "name") {
                    return Result<EditorJson>::Success(EditorJson(component->Name));
                }
            }
            return Result<EditorJson>::Failure("Editor.ComponentMissing");
        }

        if (componentType == "transform") {
            if (const auto* component = registry.try_get<ECS::TransformComponent>(entity)) {
                if (propertyPath == "position") return Result<EditorJson>::Success(Vec3ToJson(component->Position));
                if (propertyPath == "rotation") return Result<EditorJson>::Success(Vec3ToJson(component->Rotation));
                if (propertyPath == "scale") return Result<EditorJson>::Success(Vec3ToJson(component->Scale));
            }
            return Result<EditorJson>::Failure("Editor.ComponentMissing");
        }

        if (componentType == "light") {
            if (const auto* component = registry.try_get<ECS::LightComponent>(entity)) {
                if (propertyPath == "type") return Result<EditorJson>::Success(EditorJson(static_cast<int>(component->Type)));
                if (propertyPath == "color") return Result<EditorJson>::Success(Vec3ToJson(component->Color));
                if (propertyPath == "intensity") return Result<EditorJson>::Success(EditorJson(component->Intensity));
                if (propertyPath == "radius") return Result<EditorJson>::Success(EditorJson(component->Radius));
                if (propertyPath == "enabled") return Result<EditorJson>::Success(EditorJson(component->Enabled));
                if (propertyPath == "castShadows") return Result<EditorJson>::Success(EditorJson(component->CastShadows));
            }
            return Result<EditorJson>::Failure("Editor.ComponentMissing");
        }

        if (componentType == "camera") {
            if (const auto* component = registry.try_get<ECS::CameraComponent>(entity)) {
                if (propertyPath == "projection") return Result<EditorJson>::Success(EditorJson(static_cast<int>(component->Projection)));
                if (propertyPath == "fieldOfView") return Result<EditorJson>::Success(EditorJson(component->FieldOfView));
                if (propertyPath == "nearPlane") return Result<EditorJson>::Success(EditorJson(component->NearPlane));
                if (propertyPath == "farPlane") return Result<EditorJson>::Success(EditorJson(component->FarPlane));
                if (propertyPath == "orthoSize") return Result<EditorJson>::Success(EditorJson(component->OrthoSize));
                if (propertyPath == "isActive") return Result<EditorJson>::Success(EditorJson(component->IsActive));
            }
            return Result<EditorJson>::Failure("Editor.ComponentMissing");
        }

        if (componentType == "ui") {
            if (const auto* component = registry.try_get<ECS::UIComponent>(entity)) {
                if (propertyPath == "text") return Result<EditorJson>::Success(EditorJson(component->Text));
                if (propertyPath == "visible") return Result<EditorJson>::Success(EditorJson(component->Visible));
                if (propertyPath == "progress") return Result<EditorJson>::Success(EditorJson(component->Progress));
                if (propertyPath == "fontSize") return Result<EditorJson>::Success(EditorJson(component->FontSize));
            }
            return Result<EditorJson>::Failure("Editor.ComponentMissing");
        }

        if (componentType == "worldUI") {
            if (const auto* component = registry.try_get<ECS::WorldUIComponent>(entity)) {
                if (propertyPath == "text") return Result<EditorJson>::Success(EditorJson(component->Text));
                if (propertyPath == "visible") return Result<EditorJson>::Success(EditorJson(component->Visible));
                if (propertyPath == "progress") return Result<EditorJson>::Success(EditorJson(component->Progress));
                if (propertyPath == "fontSize") return Result<EditorJson>::Success(EditorJson(component->FontSize));
            }
            return Result<EditorJson>::Failure("Editor.ComponentMissing");
        }

        return Result<EditorJson>::Failure("Editor.UnsupportedComponent");
    }

    bool ApplyPropertyValue(EditorContext& ctx,
                            entt::entity entity,
                            const std::string& componentType,
                            const std::string& propertyPath,
                            const EditorJson& value,
                            std::string& error) {
        if (!ctx.ActiveScene) {
            error = "Editor.InvalidScene";
            return false;
        }

        auto& registry = ctx.ActiveScene->GetRegistry();
        if (!registry.valid(entity)) {
            error = "Editor.InvalidEntity";
            return false;
        }

        if (componentType == "name") {
            auto* component = registry.try_get<ECS::NameComponent>(entity);
            if (!component) {
                error = "Editor.ComponentMissing";
                return false;
            }
            if (propertyPath == "name" && value.is_string()) {
                component->Name = value.get<std::string>();
                return true;
            }
            error = "Editor.InvalidProperty";
            return false;
        }

        if (componentType == "transform") {
            auto* component = registry.try_get<ECS::TransformComponent>(entity);
            if (!component) {
                error = "Editor.ComponentMissing";
                return false;
            }

            Math::Vec3 vec{};
            if (propertyPath == "position" && JsonToVec3(value, vec)) {
                component->Position = vec;
                component->IsDirty = true;
                return true;
            }
            if (propertyPath == "rotation" && JsonToVec3(value, vec)) {
                component->Rotation = vec;
                component->IsDirty = true;
                return true;
            }
            if (propertyPath == "scale" && JsonToVec3(value, vec)) {
                component->Scale = vec;
                component->IsDirty = true;
                return true;
            }
            error = "Editor.InvalidProperty";
            return false;
        }

        if (componentType == "light") {
            auto* component = registry.try_get<ECS::LightComponent>(entity);
            if (!component) {
                error = "Editor.ComponentMissing";
                return false;
            }

            if (propertyPath == "type" && value.is_number_integer()) {
                int incomingType = value.get<int>();
                incomingType = std::clamp(incomingType, 0, 2);
                component->Type = static_cast<ECS::LightType>(incomingType);
                return true;
            }
            if (propertyPath == "color") {
                Math::Vec3 color = component->Color;
                if (!JsonToVec3(value, color)) {
                    error = "Editor.InvalidPropertyValue";
                    return false;
                }
                component->Color = Math::Vec3(
                    ClampFloat(color.x, 0.0f, 1.0f),
                    ClampFloat(color.y, 0.0f, 1.0f),
                    ClampFloat(color.z, 0.0f, 1.0f));
                return true;
            }
            if (propertyPath == "intensity" && value.is_number()) {
                component->Intensity = ClampFloat(value.get<float>(), 0.0f, 100000.0f);
                return true;
            }
            if (propertyPath == "radius" && value.is_number()) {
                component->Radius = ClampFloat(value.get<float>(), 0.0f, 100000.0f);
                return true;
            }
            if (propertyPath == "enabled" && value.is_boolean()) {
                component->Enabled = value.get<bool>();
                return true;
            }
            if (propertyPath == "castShadows" && value.is_boolean()) {
                component->CastShadows = value.get<bool>();
                return true;
            }

            error = "Editor.InvalidProperty";
            return false;
        }

        if (componentType == "camera") {
            auto* component = registry.try_get<ECS::CameraComponent>(entity);
            if (!component) {
                error = "Editor.ComponentMissing";
                return false;
            }

            if (propertyPath == "projection" && value.is_number_integer()) {
                int incomingType = value.get<int>();
                incomingType = std::clamp(incomingType, 0, 1);
                component->Projection = static_cast<ECS::ProjectionType>(incomingType);
                component->IsDirty = true;
                return true;
            }
            if (propertyPath == "fieldOfView" && value.is_number()) {
                component->FieldOfView = ClampFloat(value.get<float>(), 1.0f, 179.0f);
                component->IsDirty = true;
                return true;
            }
            if (propertyPath == "nearPlane" && value.is_number()) {
                component->NearPlane = ClampFloat(value.get<float>(), 0.001f, 100000.0f);
                component->NearPlane = std::min(component->NearPlane, component->FarPlane - 0.001f);
                component->IsDirty = true;
                return true;
            }
            if (propertyPath == "farPlane" && value.is_number()) {
                component->FarPlane = ClampFloat(value.get<float>(), component->NearPlane + 0.001f, 100000.0f);
                component->IsDirty = true;
                return true;
            }
            if (propertyPath == "orthoSize" && value.is_number()) {
                component->OrthoSize = ClampFloat(value.get<float>(), 0.001f, 100000.0f);
                component->IsDirty = true;
                return true;
            }
            if (propertyPath == "isActive" && value.is_boolean()) {
                component->IsActive = value.get<bool>();
                component->IsDirty = true;
                return true;
            }

            error = "Editor.InvalidProperty";
            return false;
        }

        if (componentType == "ui") {
            auto* component = registry.try_get<ECS::UIComponent>(entity);
            if (!component) {
                error = "Editor.ComponentMissing";
                return false;
            }

            if (propertyPath == "text" && value.is_string()) {
                component->Text = value.get<std::string>();
                component->IsDirty = true;
                return true;
            }
            if (propertyPath == "visible" && value.is_boolean()) {
                component->Visible = value.get<bool>();
                component->IsDirty = true;
                return true;
            }
            if (propertyPath == "progress" && value.is_number()) {
                component->Progress = ClampFloat(value.get<float>(), 0.0f, 1.0f);
                component->IsDirty = true;
                return true;
            }
            if (propertyPath == "fontSize" && value.is_number()) {
                component->FontSize = ClampFloat(value.get<float>(), 1.0f, 512.0f);
                component->IsDirty = true;
                return true;
            }

            error = "Editor.InvalidProperty";
            return false;
        }

        if (componentType == "worldUI") {
            auto* component = registry.try_get<ECS::WorldUIComponent>(entity);
            if (!component) {
                error = "Editor.ComponentMissing";
                return false;
            }

            if (propertyPath == "text" && value.is_string()) {
                component->Text = value.get<std::string>();
                component->IsDirty = true;
                return true;
            }
            if (propertyPath == "visible" && value.is_boolean()) {
                component->Visible = value.get<bool>();
                component->IsDirty = true;
                return true;
            }
            if (propertyPath == "progress" && value.is_number()) {
                component->Progress = ClampFloat(value.get<float>(), 0.0f, 1.0f);
                component->IsDirty = true;
                return true;
            }
            if (propertyPath == "fontSize" && value.is_number()) {
                component->FontSize = ClampFloat(value.get<float>(), 1.0f, 512.0f);
                component->IsDirty = true;
                return true;
            }

            error = "Editor.InvalidProperty";
            return false;
        }

        error = "Editor.UnsupportedComponent";
        return false;
    }

    bool ValidateWritablePath(std::string_view outputPath, std::filesystem::path& resolvedPath) {
        if (outputPath.empty()) {
            return false;
        }

        std::filesystem::path path(outputPath);
        if (!Security::PathValidator::QuickValidate(path)) {
            return false;
        }

        resolvedPath = std::filesystem::absolute(path);
        return true;
    }

    std::vector<entt::entity> CollectHierarchySubtree(const entt::registry& registry, entt::entity root) {
        std::vector<entt::entity> orderedEntities;
        if (!registry.valid(root)) {
            return orderedEntities;
        }

        std::queue<entt::entity> pending;
        std::unordered_set<uint32_t> visited;

        pending.push(root);
        while (!pending.empty()) {
            const entt::entity current = pending.front();
            pending.pop();

            const uint32_t currentId = static_cast<uint32_t>(current);
            if (visited.contains(currentId)) {
                continue;
            }
            visited.insert(currentId);
            orderedEntities.push_back(current);

            const auto* hierarchy = registry.try_get<ECS::HierarchyComponent>(current);
            if (!hierarchy) {
                continue;
            }

            for (const entt::entity child : hierarchy->Children) {
                if (registry.valid(child)) {
                    pending.push(child);
                }
            }
        }

        return orderedEntities;
    }

    EditorJson RemapSerializedEntity(const EditorJson& serializedEntity,
                                     const std::unordered_map<uint32_t, uint32_t>& localIdMap) {
        EditorJson remapped = serializedEntity;
        const uint32_t runtimeId = serializedEntity.value("id", 0u);
        const auto idIt = localIdMap.find(runtimeId);
        if (idIt != localIdMap.end()) {
            remapped["id"] = idIt->second;
        }

        if (serializedEntity.contains("parent") && serializedEntity["parent"].is_number_unsigned()) {
            const uint32_t runtimeParent = serializedEntity["parent"].get<uint32_t>();
            const auto parentIt = localIdMap.find(runtimeParent);
            if (parentIt != localIdMap.end()) {
                remapped["parent"] = parentIt->second;
            } else {
                remapped["parent"] = nullptr;
            }
        }

        if (serializedEntity.contains("children") && serializedEntity["children"].is_array()) {
            EditorJson remappedChildren = EditorJson::array();
            for (const auto& child : serializedEntity["children"]) {
                if (!child.is_number_unsigned()) {
                    continue;
                }
                const uint32_t runtimeChild = child.get<uint32_t>();
                const auto childIt = localIdMap.find(runtimeChild);
                if (childIt != localIdMap.end()) {
                    remappedChildren.push_back(childIt->second);
                }
            }
            remapped["children"] = std::move(remappedChildren);
        }

        return remapped;
    }

    void EnsureHierarchyRelationship(entt::registry& registry, entt::entity parent, entt::entity child) {
        auto& parentHierarchy = registry.emplace_or_replace<ECS::HierarchyComponent>(parent);
        auto& childHierarchy = registry.emplace_or_replace<ECS::HierarchyComponent>(child);

        childHierarchy.Parent = parent;
        childHierarchy.Depth = parentHierarchy.Depth + 1;

        if (std::find(parentHierarchy.Children.begin(), parentHierarchy.Children.end(), child) == parentHierarchy.Children.end()) {
            parentHierarchy.Children.push_back(child);
        }
    }

    bool WriteJsonFile(const std::filesystem::path& path, const EditorJson& payload, std::string& error) {
        std::error_code ec;
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) {
                error = "Prefab.WriteFailed";
                return false;
            }
        }

        std::ofstream output(path, std::ios::out | std::ios::trunc);
        if (!output) {
            error = "Prefab.WriteFailed";
            return false;
        }

        output << payload.dump(2);
        output.close();
        return true;
    }

    uint64_t HashJsonPayload(const EditorJson& payload) {
        const std::string content = payload.dump();
        uint64_t hash = 14695981039346656037ULL;
        for (const unsigned char ch : content) {
            hash ^= static_cast<uint64_t>(ch);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    VisualScriptGraphAsset& EnsureGraphAsset(EditorContext& ctx, std::string_view graphGuid) {
        const std::string guid(graphGuid);
        auto it = ctx.GraphRuntime.Graphs.find(guid);
        if (it != ctx.GraphRuntime.Graphs.end()) {
            return it->second;
        }

        VisualScriptGraphAsset graph;
        graph.Guid = guid;
        graph.SchemaVersion = kGraphSchemaVersion;
        graph.EntryNodeId = "entry";
        graph.Nodes.push_back(GraphNode{"entry", "Entry", EditorJson::object()});

        auto [insertedIt, _] = ctx.GraphRuntime.Graphs.emplace(graph.Guid, std::move(graph));
        return insertedIt->second;
    }

    TimelineAsset& EnsureTimelineAsset(EditorContext& ctx, std::string_view timelineGuid) {
        const std::string guid(timelineGuid);
        auto it = ctx.Sequencer.Timelines.find(guid);
        if (it != ctx.Sequencer.Timelines.end()) {
            return it->second;
        }

        TimelineAsset timeline;
        timeline.Guid = guid;
        timeline.SchemaVersion = kTimelineSchemaVersion;
        timeline.DurationSeconds = 10.0f;
        timeline.FrameRate = 30.0f;

        auto [insertedIt, _] = ctx.Sequencer.Timelines.emplace(timeline.Guid, std::move(timeline));
        return insertedIt->second;
    }

    Result<TimelineTrack*> FindTimelineTrack(EditorContext& ctx, std::string_view trackId) {
        for (auto& [_, timeline] : ctx.Sequencer.Timelines) {
            for (auto& track : timeline.Tracks) {
                if (track.TrackId == trackId) {
                    return Result<TimelineTrack*>::Success(&track);
                }
            }
        }
        return Result<TimelineTrack*>::Failure("Timeline.TrackNotFound");
    }

} // namespace

    void OpenSceneHierarchyPanel(EditorContext& ctx) {
        DrawSceneHierarchyPanel(ctx);
    }

    void OpenInspectorPanel(EditorContext& ctx) {
        DrawInspectorPanel(ctx);
    }

    bool SelectEntityInEditor(EditorContext& ctx, entt::entity entity, const char* source) {
        const std::string_view selectionSource = source ? std::string_view(source) : std::string_view("unknown");
        return SelectEntity(ctx, entity, selectionSource);
    }

    PropertyEditResult EditComponentPropertiesInEditor(EditorContext& ctx, const PropertyEditRequest& req) {
        PropertyEditResult result;
        result.TransactionLabel = req.TransactionLabel;

        if (!ctx.ActiveScene) {
            result.Error = "Editor.InvalidScene";
            return result;
        }

        const auto currentValueResult = ReadPropertyValue(ctx, req.Entity, req.ComponentType, req.PropertyPath);
        if (!currentValueResult.Ok) {
            result.Error = currentValueResult.Error;
            return result;
        }

        if (currentValueResult.Value == req.NewValue) {
            result.Success = true;
            result.ValueChanged = false;
            return result;
        }

        const std::string componentType = req.ComponentType;
        const std::string propertyPath = req.PropertyPath;
        const EditorJson beforeValue = currentValueResult.Value;
        const EditorJson afterValue = req.NewValue;
        const entt::entity targetEntity = req.Entity;
        const std::string label = req.TransactionLabel.empty()
                                      ? ("Edit " + componentType + "." + propertyPath)
                                      : req.TransactionLabel;

        auto command = std::make_unique<Commands::ComponentPropertyCommand>(
            label,
            [&ctx, targetEntity, componentType, propertyPath, afterValue]() {
                std::string commandError;
                return ApplyPropertyValue(ctx, targetEntity, componentType, propertyPath, afterValue, commandError);
            },
            [&ctx, targetEntity, componentType, propertyPath, beforeValue]() {
                std::string commandError;
                return ApplyPropertyValue(ctx, targetEntity, componentType, propertyPath, beforeValue, commandError);
            });

        if (!ctx.History.Execute(std::move(command), label)) {
            result.Error = "Editor.CommandApplyFailed";
            return result;
        }

        result.Success = true;
        result.ValueChanged = true;
        result.DirtyStateChanged = true;
        ctx.Dirty = true;
        return result;
    }

    bool UndoEditorAction(EditorContext& ctx) {
        const bool undone = ctx.History.Undo();
        if (undone) {
            ctx.Dirty = true;
            ctx.Selection.Dirty = true;
        }
        return undone;
    }

    bool RedoEditorAction(EditorContext& ctx) {
        const bool redone = ctx.History.Redo();
        if (redone) {
            ctx.Dirty = true;
            ctx.Selection.Dirty = true;
        }
        return redone;
    }

    Result<std::string> CreatePrefabAsset(EditorContext& ctx, entt::entity rootEntity, std::string_view outputPath) {
        if (!ctx.ActiveScene) {
            return Result<std::string>::Failure("Prefab.InvalidScene");
        }

        auto& registry = ctx.ActiveScene->GetRegistry();
        if (!registry.valid(rootEntity)) {
            return Result<std::string>::Failure("Editor.InvalidEntity");
        }

        std::filesystem::path resolvedPath;
        if (!ValidateWritablePath(outputPath, resolvedPath)) {
            return Result<std::string>::Failure("Prefab.InvalidOutputPath");
        }

        const std::vector<entt::entity> entities = CollectHierarchySubtree(registry, rootEntity);
        if (entities.empty()) {
            return Result<std::string>::Failure("Prefab.EmptySelection");
        }

        std::unordered_map<uint32_t, uint32_t> localIdMap;
        localIdMap.reserve(entities.size());
        for (std::size_t index = 0; index < entities.size(); ++index) {
            localIdMap.emplace(static_cast<uint32_t>(entities[index]), static_cast<uint32_t>(index));
        }

        EditorJson serializedEntities = EditorJson::array();
        for (const entt::entity entity : entities) {
            const EditorJson serialized = SerializeEntityForPrefab(entity, registry);
            serializedEntities.push_back(RemapSerializedEntity(serialized, localIdMap));
        }

        const std::string prefabGuid = GenerateGuid("prefab");
        const std::string displayName = registry.try_get<ECS::NameComponent>(rootEntity)
                                            ? registry.get<ECS::NameComponent>(rootEntity).Name
                                            : std::string("Prefab");

        EditorJson payload{
            {"schemaVersion", kPrefabSchemaVersion},
            {"guid", prefabGuid},
            {"displayName", displayName},
            {"rootLocalId", localIdMap[static_cast<uint32_t>(rootEntity)]},
            {"entities", serializedEntities}
        };

        std::string writeError;
        if (!WriteJsonFile(resolvedPath, payload, writeError)) {
            return Result<std::string>::Failure(writeError);
        }

        PrefabAssetData asset;
        asset.Guid = prefabGuid;
        asset.DisplayName = displayName;
        asset.SourcePath = resolvedPath.string();
        asset.SchemaVersion = kPrefabSchemaVersion;
        asset.Payload = std::move(payload);
        ctx.Prefabs.Assets[prefabGuid] = std::move(asset);
        ctx.Dirty = true;

        return Result<std::string>::Success(prefabGuid);
    }

    Result<entt::entity> InstantiatePrefabAsset(EditorContext& ctx, std::string_view prefabGuid, const SpawnOptions& options) {
        if (!ctx.ActiveScene) {
            return Result<entt::entity>::Failure("Prefab.InvalidScene");
        }

        const std::string guid(prefabGuid);
        auto assetIt = ctx.Prefabs.Assets.find(guid);
        if (assetIt == ctx.Prefabs.Assets.end()) {
            return Result<entt::entity>::Failure("Prefab.NotFound");
        }

        auto& registry = ctx.ActiveScene->GetRegistry();
        const EditorJson& payload = assetIt->second.Payload;
        if (!payload.contains("entities") || !payload["entities"].is_array()) {
            return Result<entt::entity>::Failure("Prefab.InvalidPayload");
        }

        const EditorJson& entities = payload["entities"];
        std::unordered_map<uint32_t, entt::entity> localToRuntimeMap;
        localToRuntimeMap.reserve(entities.size());

        for (const auto& serializedEntity : entities) {
            const uint32_t localId = serializedEntity.value("id", 0u);
            const std::string name = serializedEntity.value("name", std::string("PrefabEntity"));
            ECS::Entity createdEntity = ctx.ActiveScene->CreateEntity(name);
            localToRuntimeMap[localId] = createdEntity.GetHandle();
        }

        for (const auto& serializedEntity : entities) {
            const uint32_t localId = serializedEntity.value("id", 0u);
            const auto mapIt = localToRuntimeMap.find(localId);
            if (mapIt == localToRuntimeMap.end()) {
                continue;
            }

            const entt::entity runtimeEntity = mapIt->second;
            if (!registry.valid(runtimeEntity)) {
                continue;
            }

            const auto componentsIt = serializedEntity.find("components");
            if (componentsIt != serializedEntity.end() && componentsIt->is_object()) {
                const auto& components = *componentsIt;
                if (const auto transformIt = components.find("transform"); transformIt != components.end()) {
                    registry.emplace_or_replace<ECS::TransformComponent>(runtimeEntity, DeserializeTransformComponent(*transformIt));
                }
                if (const auto lightIt = components.find("light"); lightIt != components.end()) {
                    registry.emplace_or_replace<ECS::LightComponent>(runtimeEntity, DeserializeLightComponent(*lightIt));
                }
                if (const auto meshIt = components.find("mesh"); meshIt != components.end()) {
                    registry.emplace_or_replace<ECS::MeshComponent>(runtimeEntity, DeserializeMeshComponent(*meshIt));
                }
                if (const auto cameraIt = components.find("camera"); cameraIt != components.end()) {
                    registry.emplace_or_replace<ECS::CameraComponent>(runtimeEntity, DeserializeCameraComponent(*cameraIt));
                }
                if (const auto uiIt = components.find("ui"); uiIt != components.end()) {
                    registry.emplace_or_replace<ECS::UIComponent>(runtimeEntity, DeserializeUIComponent(*uiIt));
                }
                if (const auto worldUiIt = components.find("worldUI"); worldUiIt != components.end()) {
                    registry.emplace_or_replace<ECS::WorldUIComponent>(runtimeEntity, DeserializeWorldUIComponent(*worldUiIt));
                }
            }

            if (serializedEntity.contains("name") && serializedEntity["name"].is_string()) {
                registry.emplace_or_replace<ECS::NameComponent>(runtimeEntity, ECS::NameComponent{serializedEntity["name"].get<std::string>()});
            }
        }

        for (const auto& serializedEntity : entities) {
            const uint32_t localId = serializedEntity.value("id", 0u);
            const auto mapIt = localToRuntimeMap.find(localId);
            if (mapIt == localToRuntimeMap.end()) {
                continue;
            }

            if (!serializedEntity.contains("parent") || !serializedEntity["parent"].is_number_unsigned()) {
                continue;
            }

            const uint32_t parentLocalId = serializedEntity["parent"].get<uint32_t>();
            const auto parentMapIt = localToRuntimeMap.find(parentLocalId);
            if (parentMapIt == localToRuntimeMap.end()) {
                continue;
            }

            EnsureHierarchyRelationship(registry, parentMapIt->second, mapIt->second);
        }

        const uint32_t rootLocalId = payload.value("rootLocalId", 0u);
        const auto rootIt = localToRuntimeMap.find(rootLocalId);
        if (rootIt == localToRuntimeMap.end()) {
            return Result<entt::entity>::Failure("Prefab.RootNotFound");
        }

        const entt::entity runtimeRoot = rootIt->second;
        if (auto* transform = registry.try_get<ECS::TransformComponent>(runtimeRoot)) {
            transform->Position = options.Position;
            transform->Rotation = options.Rotation;
            transform->Scale = options.Scale;
            transform->IsDirty = true;
        } else {
            registry.emplace<ECS::TransformComponent>(
                runtimeRoot,
                ECS::TransformComponent{options.Position, options.Rotation, options.Scale});
        }

        if (options.Parent != entt::null && registry.valid(options.Parent) && runtimeRoot != options.Parent) {
            EnsureHierarchyRelationship(registry, options.Parent, runtimeRoot);
        }

        ECS::PrefabInstanceComponent prefabInstance;
        prefabInstance.PrefabGuid = guid;
        prefabInstance.InstanceGuid = GenerateGuid("prefab-instance");
        prefabInstance.Overrides = EditorJson::array();
        prefabInstance.HasLocalOverrides = false;
        registry.emplace_or_replace<ECS::PrefabInstanceComponent>(runtimeRoot, std::move(prefabInstance));

        const auto* instanceComponent = registry.try_get<ECS::PrefabInstanceComponent>(runtimeRoot);
        if (instanceComponent) {
            ctx.Prefabs.InstanceToPrefab[instanceComponent->InstanceGuid] = guid;
        }

        if (options.SelectAfterSpawn) {
            SelectEntityInEditor(ctx, runtimeRoot, "prefab");
        }

        ctx.Dirty = true;
        return Result<entt::entity>::Success(runtimeRoot);
    }

    Result<void> ApplyPrefabOverrides(EditorContext& ctx, std::string_view instanceGuid) {
        if (!ctx.ActiveScene) {
            return Result<void>::Failure("Prefab.InvalidScene");
        }

        const std::string instanceId(instanceGuid);
        const auto instanceIt = ctx.Prefabs.InstanceToPrefab.find(instanceId);
        if (instanceIt == ctx.Prefabs.InstanceToPrefab.end()) {
            return Result<void>::Failure("Prefab.InstanceNotFound");
        }

        const auto assetIt = ctx.Prefabs.Assets.find(instanceIt->second);
        if (assetIt == ctx.Prefabs.Assets.end()) {
            return Result<void>::Failure("Prefab.NotFound");
        }

        auto& registry = ctx.ActiveScene->GetRegistry();
        entt::entity instanceRoot = entt::null;
        auto view = registry.view<ECS::PrefabInstanceComponent>();
        for (const entt::entity entity : view) {
            const auto& instance = view.get<ECS::PrefabInstanceComponent>(entity);
            if (instance.InstanceGuid == instanceId) {
                instanceRoot = entity;
                break;
            }
        }

        if (instanceRoot == entt::null) {
            return Result<void>::Failure("Prefab.InstanceRootNotFound");
        }

        EditorJson serializedRoot = SerializeEntityForPrefab(instanceRoot, registry);
        auto updatedAsset = assetIt->second;
        updatedAsset.Payload["lastAppliedInstance"] = instanceId;
        updatedAsset.Payload["lastAppliedRoot"] = serializedRoot;

        std::string writeError;
        if (!updatedAsset.SourcePath.empty()) {
            const std::filesystem::path sourcePath(updatedAsset.SourcePath);
            if (!WriteJsonFile(sourcePath, updatedAsset.Payload, writeError)) {
                return Result<void>::Failure(writeError);
            }
        }

        ctx.Prefabs.Assets[updatedAsset.Guid] = std::move(updatedAsset);
        if (auto* instance = registry.try_get<ECS::PrefabInstanceComponent>(instanceRoot)) {
            instance->Overrides = EditorJson::array();
            instance->HasLocalOverrides = false;
        }

        ctx.Dirty = true;
        return Result<void>::Success();
    }

    Result<std::string> CreatePrefabVariant(EditorContext& ctx,
                                            std::string_view parentPrefabGuid,
                                            const VariantOptions& options) {
        const std::string parentGuid(parentPrefabGuid);
        if (ctx.Prefabs.Assets.find(parentGuid) == ctx.Prefabs.Assets.end()) {
            return Result<std::string>::Failure("Prefab.ParentNotFound");
        }

        std::filesystem::path resolvedPath;
        if (!options.OutputPath.empty() && !ValidateWritablePath(options.OutputPath, resolvedPath)) {
            return Result<std::string>::Failure("PrefabVariant.InvalidOutputPath");
        }

        PrefabVariantData variant;
        variant.Guid = GenerateGuid("prefab-variant");
        variant.ParentPrefabGuid = parentGuid;
        variant.DisplayName = options.DisplayName.empty() ? "PrefabVariant" : options.DisplayName;
        variant.SchemaVersion = kPrefabSchemaVersion;
        variant.Overrides = options.Overrides;
        variant.SourcePath = resolvedPath.string();

        EditorJson payload{
            {"schemaVersion", variant.SchemaVersion},
            {"guid", variant.Guid},
            {"parentPrefabGuid", variant.ParentPrefabGuid},
            {"displayName", variant.DisplayName},
            {"overrides", variant.Overrides}
        };

        if (!variant.SourcePath.empty()) {
            std::string writeError;
            if (!WriteJsonFile(resolvedPath, payload, writeError)) {
                return Result<std::string>::Failure(writeError);
            }
        }

        ctx.Prefabs.Variants[variant.Guid] = std::move(variant);
        ctx.Dirty = true;
        return Result<std::string>::Success(payload["guid"].get<std::string>());
    }

    void OpenVisualScriptingGraphEditor(EditorContext& ctx, std::string_view graphGuid) {
        VisualScriptGraphAsset& graph = EnsureGraphAsset(ctx, graphGuid);
        ctx.Panels.VisualScripting.Open = true;
        ctx.Panels.VisualScripting.ActiveGraphGuid = graph.Guid;
        ctx.Dirty = true;
    }

    Result<CompiledGraphIR> CompileVisualScriptingGraph(EditorContext& ctx, std::string_view graphGuid) {
        const std::string guid(graphGuid);
        auto graphIt = ctx.GraphRuntime.Graphs.find(guid);
        if (graphIt == ctx.GraphRuntime.Graphs.end()) {
            return Result<CompiledGraphIR>::Failure("Graph.NotFound");
        }

        const VisualScriptGraphAsset& graph = graphIt->second;
        if (graph.Nodes.empty()) {
            return Result<CompiledGraphIR>::Failure("Graph.Empty");
        }

        std::unordered_map<std::string, std::vector<std::string>> adjacency;
        std::unordered_set<std::string> nodeIds;
        nodeIds.reserve(graph.Nodes.size());

        for (const auto& node : graph.Nodes) {
            nodeIds.insert(node.NodeId);
        }

        for (const auto& edge : graph.Edges) {
            if (!nodeIds.contains(edge.SourceNodeId) || !nodeIds.contains(edge.TargetNodeId)) {
                return Result<CompiledGraphIR>::Failure("Graph.InvalidEdge");
            }

            adjacency[edge.SourceNodeId].push_back(edge.TargetNodeId);
        }

        if (!nodeIds.contains(graph.EntryNodeId)) {
            return Result<CompiledGraphIR>::Failure("Graph.EntryMissing");
        }

        std::unordered_set<std::string> reachable;
        std::queue<std::string> pending;
        pending.push(graph.EntryNodeId);
        while (!pending.empty()) {
            const std::string current = pending.front();
            pending.pop();
            if (!reachable.insert(current).second) {
                continue;
            }
            for (const std::string& target : adjacency[current]) {
                pending.push(target);
            }
        }

        std::unordered_map<std::string, int> reachableIndegree;
        for (const auto& nodeId : reachable) {
            reachableIndegree[nodeId] = 0;
        }
        for (const auto& [source, targets] : adjacency) {
            if (!reachable.contains(source)) {
                continue;
            }
            for (const auto& target : targets) {
                if (reachable.contains(target)) {
                    reachableIndegree[target]++;
                }
            }
        }

        std::queue<std::string> toProcess;
        for (const auto& [nodeId, degree] : reachableIndegree) {
            if (degree == 0) {
                toProcess.push(nodeId);
            }
        }

        std::vector<std::string> executionOrder;
        executionOrder.reserve(reachable.size());
        while (!toProcess.empty()) {
            const std::string current = toProcess.front();
            toProcess.pop();
            executionOrder.push_back(current);

            for (const std::string& target : adjacency[current]) {
                if (!reachable.contains(target)) {
                    continue;
                }
                reachableIndegree[target]--;
                if (reachableIndegree[target] == 0) {
                    toProcess.push(target);
                }
            }
        }

        if (executionOrder.size() != reachable.size()) {
            return Result<CompiledGraphIR>::Failure("Graph.CycleDetected");
        }

        CompiledGraphIR ir;
        ir.GraphGuid = guid;
        ir.IsValid = true;
        ir.ExecutionOrder = std::move(executionOrder);
        ir.ContentHash = HashJsonPayload(EditorJson{
            {"guid", graph.Guid},
            {"entry", graph.EntryNodeId},
            {"nodes", graph.Nodes.size()},
            {"edges", graph.Edges.size()}});

        ctx.CompiledGraphs[guid] = ir;
        return Result<CompiledGraphIR>::Success(ir);
    }

    Result<void> ExecuteVisualScriptingGraph(EditorContext& ctx, std::string_view graphGuid, ExecutionMode mode) {
        const std::string guid(graphGuid);
        if (mode == ExecutionMode::OnStart && !ctx.IsPlayMode) {
            return Result<void>::Success();
        }

        auto compiledIt = ctx.CompiledGraphs.find(guid);
        if (compiledIt == ctx.CompiledGraphs.end()) {
            auto compileResult = CompileVisualScriptingGraph(ctx, guid);
            if (!compileResult.Ok) {
                return Result<void>::Failure(compileResult.Error);
            }
            compiledIt = ctx.CompiledGraphs.find(guid);
        }

        if (compiledIt == ctx.CompiledGraphs.end() || !compiledIt->second.IsValid) {
            return Result<void>::Failure("Graph.NotCompiled");
        }

        for (const std::string& nodeId : compiledIt->second.ExecutionOrder) {
            ctx.Diagnostics.push_back("Graph node executed: " + nodeId);
        }

        return Result<void>::Success();
    }

    void OpenCinematicSequencerEditor(EditorContext& ctx, std::string_view timelineGuid) {
        TimelineAsset& timeline = EnsureTimelineAsset(ctx, timelineGuid);
        ctx.Panels.Sequencer.Open = true;
        ctx.Panels.Sequencer.ActiveTimelineGuid = timeline.Guid;
        ctx.Dirty = true;
    }

    Result<std::string> AddTimelineTrack(EditorContext& ctx,
                                         std::string_view timelineGuid,
                                         TrackType type,
                                         std::string_view displayName) {
        const std::string guid(timelineGuid);
        auto timelineIt = ctx.Sequencer.Timelines.find(guid);
        if (timelineIt == ctx.Sequencer.Timelines.end()) {
            return Result<std::string>::Failure("Timeline.NotFound");
        }

        TimelineTrack track;
        track.TrackId = GenerateGuid("track");
        track.Type = type;
        track.DisplayName = displayName.empty() ? std::string(TrackTypeToString(type)) : std::string(displayName);
        track.SortOrder = static_cast<int>(timelineIt->second.Tracks.size());

        timelineIt->second.Tracks.push_back(track);
        ctx.Dirty = true;
        return Result<std::string>::Success(track.TrackId);
    }

    Result<std::string> AddTimelineClip(EditorContext& ctx, std::string_view trackId, const TimelineClipCreateInfo& clipInfo) {
        if (clipInfo.EndTime <= clipInfo.StartTime || clipInfo.StartTime < 0.0f) {
            return Result<std::string>::Failure("Timeline.InvalidRange");
        }

        auto trackResult = FindTimelineTrack(ctx, trackId);
        if (!trackResult.Ok || !trackResult.Value) {
            return Result<std::string>::Failure("Timeline.TrackNotFound");
        }

        TimelineTrack* track = trackResult.Value;
        TimelineClip clip;
        clip.ClipId = GenerateGuid("clip");
        clip.StartTime = clipInfo.StartTime;
        clip.EndTime = clipInfo.EndTime;
        clip.ClipType = clipInfo.ClipType;
        clip.Payload = clipInfo.Payload;
        clip.BlendInSeconds = std::max(0.0f, clipInfo.BlendInSeconds);
        clip.BlendOutSeconds = std::max(0.0f, clipInfo.BlendOutSeconds);
        clip.Easing = clipInfo.Easing.empty() ? "linear" : clipInfo.Easing;
        const std::string createdClipId = clip.ClipId;
        const float createdClipEnd = clip.EndTime;

        track->Clips.push_back(std::move(clip));
        std::sort(track->Clips.begin(), track->Clips.end(), [](const TimelineClip& lhs, const TimelineClip& rhs) {
            if (lhs.StartTime == rhs.StartTime) {
                return lhs.EndTime < rhs.EndTime;
            }
            return lhs.StartTime < rhs.StartTime;
        });

        for (auto& [_, timeline] : ctx.Sequencer.Timelines) {
            for (const auto& candidateTrack : timeline.Tracks) {
                if (candidateTrack.TrackId != track->TrackId) {
                    continue;
                }
                timeline.DurationSeconds = std::max(
                    timeline.DurationSeconds,
                    createdClipEnd);
                break;
            }
        }

        ctx.Dirty = true;
        return Result<std::string>::Success(createdClipId);
    }

    Result<TimelineEvaluationResult> EvaluateTimelineAtTime(EditorContext& ctx,
                                                            std::string_view timelineGuid,
                                                            float timeSeconds) {
        const std::string guid(timelineGuid);
        const auto timelineIt = ctx.Sequencer.Timelines.find(guid);
        if (timelineIt == ctx.Sequencer.Timelines.end()) {
            return Result<TimelineEvaluationResult>::Failure("Timeline.NotFound");
        }

        const TimelineAsset& timeline = timelineIt->second;
        const float clampedTime = ClampFloat(timeSeconds, 0.0f, std::max(0.0f, timeline.DurationSeconds));

        TimelineEvaluationResult evaluation;
        evaluation.Valid = true;
        evaluation.TimeSeconds = clampedTime;

        bool hasSoloTrack = false;
        for (const auto& track : timeline.Tracks) {
            if (track.Solo) {
                hasSoloTrack = true;
                break;
            }
        }

        for (const auto& track : timeline.Tracks) {
            if (track.Muted) {
                continue;
            }
            if (hasSoloTrack && !track.Solo) {
                continue;
            }

            for (const auto& clip : track.Clips) {
                if (clampedTime < clip.StartTime || clampedTime > clip.EndTime) {
                    continue;
                }

                const float duration = std::max(clip.EndTime - clip.StartTime, 0.0001f);
                const float normalizedTime = (clampedTime - clip.StartTime) / duration;

                evaluation.ActiveTrackIds.push_back(track.TrackId);
                evaluation.ActiveClipIds.push_back(clip.ClipId);
                evaluation.ResolvedPayload[track.TrackId].push_back(EditorJson{
                    {"clipId", clip.ClipId},
                    {"clipType", clip.ClipType},
                    {"normalizedTime", normalizedTime},
                    {"payload", clip.Payload},
                    {"easing", clip.Easing}
                });
            }
        }

        return Result<TimelineEvaluationResult>::Success(evaluation);
    }

    Result<std::string> RecordAnimationTake(EditorContext& ctx,
                                            std::string_view timelineGuid,
                                            entt::entity sourceEntity,
                                            const TakeRecordOptions& options) {
        if (!ctx.ActiveScene) {
            return Result<std::string>::Failure("Timeline.InvalidScene");
        }

        const std::string guid(timelineGuid);
        auto timelineIt = ctx.Sequencer.Timelines.find(guid);
        if (timelineIt == ctx.Sequencer.Timelines.end()) {
            return Result<std::string>::Failure("Timeline.NotFound");
        }

        auto& registry = ctx.ActiveScene->GetRegistry();
        if (!registry.valid(sourceEntity)) {
            return Result<std::string>::Failure("Editor.InvalidEntity");
        }

        const auto* transform = registry.try_get<ECS::TransformComponent>(sourceEntity);
        if (!transform) {
            return Result<std::string>::Failure("Timeline.SourceMissingTransform");
        }

        const float duration = std::max(0.01f, options.DurationSeconds);
        const float frameRate = std::max(1.0f, options.FrameRate);
        const uint32_t frameCount = static_cast<uint32_t>(std::ceil(duration * frameRate));

        EditorJson samples = EditorJson::array();
        for (uint32_t frame = 0; frame <= frameCount; ++frame) {
            const float time = static_cast<float>(frame) / frameRate;
            samples.push_back(EditorJson{
                {"time", time},
                {"position", Vec3ToJson(transform->Position)},
                {"rotation", Vec3ToJson(transform->Rotation)},
                {"scale", Vec3ToJson(transform->Scale)}
            });
        }

        TimelineTrack* animationTrack = nullptr;
        for (auto& track : timelineIt->second.Tracks) {
            if (track.Type == TrackType::Animation) {
                animationTrack = &track;
                break;
            }
        }
        if (!animationTrack) {
            auto trackResult = AddTimelineTrack(ctx, guid, TrackType::Animation, "Recorded Animation");
            if (!trackResult.Ok) {
                return Result<std::string>::Failure(trackResult.Error);
            }
            auto searchTrackResult = FindTimelineTrack(ctx, trackResult.Value);
            if (!searchTrackResult.Ok || !searchTrackResult.Value) {
                return Result<std::string>::Failure("Timeline.TrackNotFound");
            }
            animationTrack = searchTrackResult.Value;
        }

        const float startTime = std::max(0.0f, ctx.Panels.Sequencer.PlayheadSeconds);
        const float endTime = startTime + duration;
        const std::string takeId = GenerateGuid("take");

        TimelineClipCreateInfo clipInfo;
        clipInfo.StartTime = startTime;
        clipInfo.EndTime = endTime;
        clipInfo.ClipType = options.ClipType;
        clipInfo.Payload = EditorJson{
            {"takeId", takeId},
            {"sourceEntity", static_cast<uint32_t>(sourceEntity)},
            {"sampledFrames", samples}
        };
        clipInfo.BlendInSeconds = 0.0f;
        clipInfo.BlendOutSeconds = 0.0f;
        clipInfo.Easing = "linear";

        const auto clipResult = AddTimelineClip(ctx, animationTrack->TrackId, clipInfo);
        if (!clipResult.Ok) {
            return Result<std::string>::Failure(clipResult.Error);
        }

        return Result<std::string>::Success(takeId);
    }

} // namespace Editor
} // namespace Core

