#pragma once

// MCP Scene Tools
// Tools for querying, modifying, and spawning entities in the game scene via MCP

#include "MCPTool.h"
#include "MCPTypes.h"
#include "SceneSerialization.h"
#include "Core/ECS/Scene.h"
#include "Core/ECS/Entity.h"
#include "Core/ECS/Components/Components.h"
#include "Core/Log.h"

#include <sstream>
#include <algorithm>

namespace Core {
namespace MCP {

    // ============================================================================
    // GetSceneContext Tool
    // ============================================================================
    // Dumps the current world state, active cameras, and level layout
    // in human/LLM-readable text format or JSON format

    class GetSceneContextTool : public MCPTool {
    public:
        GetSceneContextTool()
            : MCPTool("GetSceneContext", 
                      "Get the current game scene context including all entities, "
                      "components, active camera, and level layout. Returns data in "
                      "either human-readable text or structured JSON format.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            schema.Properties = {
                {"format", {
                    {"type", "string"},
                    {"enum", Json::array({"text", "json", "summary"})},
                    {"description", "Output format: 'text' for human/LLM-readable, "
                                    "'json' for structured data, 'summary' for brief overview"},
                    {"default", "text"}
                }},
                {"filter", {
                    {"type", "object"},
                    {"description", "Optional filters to limit what entities are returned"},
                    {"properties", {
                        {"components", {
                            {"type", "array"},
                            {"items", {{"type", "string"}}},
                            {"description", "Only include entities with these components "
                                            "(e.g., ['light', 'mesh', 'camera'])"}
                        }},
                        {"namePattern", {
                            {"type", "string"},
                            {"description", "Filter entities by name pattern (substring match)"}
                        }},
                        {"maxEntities", {
                            {"type", "integer"},
                            {"minimum", 1},
                            {"maximum", 1000},
                            {"description", "Maximum number of entities to return"}
                        }}
                    }}
                }},
                {"includeWorldMatrix", {
                    {"type", "boolean"},
                    {"description", "Include computed world matrices (verbose)"},
                    {"default", false}
                }}
            };
            return schema;
        }

        ToolResult Execute(const Json& arguments, ECS::Scene* scene) override {
            if (!scene) {
                return ToolResult::Error("No active scene available");
            }

            // Parse arguments
            std::string format = arguments.value("format", "text");
            bool includeWorldMatrix = arguments.value("includeWorldMatrix", false);

            // Parse filters
            std::vector<std::string> componentFilters;
            std::string namePattern;
            int maxEntities = -1;

            if (arguments.contains("filter") && arguments["filter"].is_object()) {
                const auto& filter = arguments["filter"];
                
                if (filter.contains("components") && filter["components"].is_array()) {
                    for (const auto& comp : filter["components"]) {
                        if (comp.is_string()) {
                            componentFilters.push_back(comp.get<std::string>());
                        }
                    }
                }
                
                namePattern = filter.value("namePattern", "");
                maxEntities = filter.value("maxEntities", -1);
            }

            // Generate output based on format
            if (format == "summary") {
                return GenerateSummary(*scene);
            } else if (format == "json") {
                return GenerateJson(*scene, componentFilters, namePattern, 
                                     maxEntities, includeWorldMatrix);
            } else {
                return GenerateText(*scene, componentFilters, namePattern, maxEntities);
            }
        }

    private:
        // Generate brief summary
        ToolResult GenerateSummary(const ECS::Scene& scene) const {
            const auto& registry = scene.GetRegistry();
            std::ostringstream ss;

            // Count entities and components
            size_t entityCount = scene.GetEntityCount();
            size_t lightCount = 0, meshCount = 0, cameraCount = 0;
            size_t rigidBodyCount = 0, colliderCount = 0;
            entt::entity activeCamera = entt::null;

            registry.view<ECS::LightComponent>().each([&](auto) { ++lightCount; });
            registry.view<ECS::MeshComponent>().each([&](auto) { ++meshCount; });
            registry.view<ECS::CameraComponent>().each([&](entt::entity e, const ECS::CameraComponent& cam) {
                ++cameraCount;
                if (cam.IsActive) activeCamera = e;
            });
            registry.view<ECS::RigidBodyComponent>().each([&](auto) { ++rigidBodyCount; });
            registry.view<ECS::ColliderComponent>().each([&](auto) { ++colliderCount; });

            ss << "Scene Summary: " << scene.GetName() << "\n";
            ss << "═══════════════════════════════════════\n";
            ss << "Total Entities: " << entityCount << "\n\n";
            ss << "Component Breakdown:\n";
            ss << "  • Meshes: " << meshCount << "\n";
            ss << "  • Lights: " << lightCount << "\n";
            ss << "  • Cameras: " << cameraCount << "\n";
            ss << "  • RigidBodies: " << rigidBodyCount << "\n";
            ss << "  • Colliders: " << colliderCount << "\n\n";

            if (activeCamera != entt::null) {
                ss << "Active Camera: Entity #" << static_cast<uint32_t>(activeCamera);
                if (auto* name = registry.try_get<NameComponent>(activeCamera)) {
                    ss << " \"" << name->Name << "\"";
                }
                ss << "\n";

                // Add camera position info
                if (auto* transform = registry.try_get<ECS::TransformComponent>(activeCamera)) {
                    ss << "  Position: (" << transform->Position.x << ", "
                       << transform->Position.y << ", " << transform->Position.z << ")\n";
                }
                if (auto* cam = registry.try_get<ECS::CameraComponent>(activeCamera)) {
                    ss << "  FOV: " << cam->FieldOfView << "°, "
                       << "Near: " << cam->NearPlane << ", Far: " << cam->FarPlane << "\n";
                }
            } else {
                ss << "Active Camera: None\n";
            }

            return ToolResult::Success(ss.str());
        }

        // Generate human-readable text output
        ToolResult GenerateText(const ECS::Scene& scene,
                                const std::vector<std::string>& componentFilters,
                                const std::string& namePattern,
                                int maxEntities) const {
            const auto& registry = scene.GetRegistry();
            std::ostringstream ss;

            ss << "╔═══════════════════════════════════════════════════════════════╗\n";
            ss << "║  SCENE CONTEXT: " << scene.GetName() << "\n";
            ss << "╚═══════════════════════════════════════════════════════════════╝\n\n";

            // Scene overview
            ss << "【Overview】\n";
            ss << "  Entity Count: " << scene.GetEntityCount() << "\n";

            // Active camera info
            entt::entity activeCamera = entt::null;
            registry.view<ECS::CameraComponent>().each([&](entt::entity e, const ECS::CameraComponent& cam) {
                if (cam.IsActive) activeCamera = e;
            });

            if (activeCamera != entt::null) {
                ss << "  Active Camera: Entity #" << static_cast<uint32_t>(activeCamera);
                if (auto* name = registry.try_get<NameComponent>(activeCamera)) {
                    ss << " \"" << name->Name << "\"";
                }
                ss << "\n";
            }
            ss << "\n";

            // Light summary
            ss << "【Lighting】\n";
            int lightIndex = 0;
            registry.view<ECS::LightComponent, ECS::TransformComponent>().each(
                [&](entt::entity e, const ECS::LightComponent& light, const ECS::TransformComponent& transform) {
                    if (!light.Enabled) return;
                    
                    ss << "  [" << lightIndex++ << "] " << LightTypeToString(light.Type) 
                       << " Light (Entity #" << static_cast<uint32_t>(e) << ")\n";
                    ss << "      Color: RGB(" << light.Color.r << ", " << light.Color.g 
                       << ", " << light.Color.b << "), Intensity: " << light.Intensity << "\n";
                    ss << "      Position: (" << transform.Position.x << ", " 
                       << transform.Position.y << ", " << transform.Position.z << ")\n";
                    if (light.Type != ECS::LightType::Directional) {
                        ss << "      Radius: " << light.Radius << "\n";
                    }
                });
            if (lightIndex == 0) {
                ss << "  (No active lights)\n";
            }
            ss << "\n";

            // Entities section
            ss << "【Entities】\n";
            int entityIndex = 0;
            int shownCount = 0;

            registry.each([&](entt::entity entity) {
                // Apply max entities limit
                if (maxEntities > 0 && shownCount >= maxEntities) return;

                // Apply name filter
                if (!namePattern.empty()) {
                    if (auto* name = registry.try_get<NameComponent>(entity)) {
                        if (name->Name.find(namePattern) == std::string::npos) {
                            return;
                        }
                    } else {
                        return; // No name, skip if filtering by name
                    }
                }

                // Apply component filters
                if (!componentFilters.empty()) {
                    bool hasRequiredComponent = false;
                    for (const auto& compName : componentFilters) {
                        if (compName == "transform" && registry.all_of<ECS::TransformComponent>(entity)) {
                            hasRequiredComponent = true; break;
                        }
                        if (compName == "light" && registry.all_of<ECS::LightComponent>(entity)) {
                            hasRequiredComponent = true; break;
                        }
                        if (compName == "mesh" && registry.all_of<ECS::MeshComponent>(entity)) {
                            hasRequiredComponent = true; break;
                        }
                        if (compName == "camera" && registry.all_of<ECS::CameraComponent>(entity)) {
                            hasRequiredComponent = true; break;
                        }
                        if (compName == "collider" && registry.all_of<ECS::ColliderComponent>(entity)) {
                            hasRequiredComponent = true; break;
                        }
                        if (compName == "rigidBody" && registry.all_of<ECS::RigidBodyComponent>(entity)) {
                            hasRequiredComponent = true; break;
                        }
                    }
                    if (!hasRequiredComponent) return;
                }

                // Generate entity text
                ss << EntityToText(entity, registry, 1);
                ++shownCount;
            });

            if (shownCount == 0) {
                ss << "  (No entities match the specified filters)\n";
            } else if (maxEntities > 0 && shownCount >= maxEntities) {
                ss << "\n  ... (output limited to " << maxEntities << " entities)\n";
            }

            return ToolResult::Success(ss.str());
        }

        // Generate structured JSON output
        ToolResult GenerateJson(const ECS::Scene& scene,
                                const std::vector<std::string>& componentFilters,
                                const std::string& namePattern,
                                int maxEntities,
                                bool includeWorldMatrix) const {
            const auto& registry = scene.GetRegistry();

            Json result;
            result["sceneName"] = scene.GetName();
            result["entityCount"] = scene.GetEntityCount();

            // Active camera
            entt::entity activeCamera = entt::null;
            registry.view<ECS::CameraComponent>().each([&](entt::entity e, const ECS::CameraComponent& cam) {
                if (cam.IsActive) activeCamera = e;
            });

            if (activeCamera != entt::null) {
                result["activeCamera"] = {
                    {"entityId", static_cast<uint32_t>(activeCamera)}
                };
                if (auto* transform = registry.try_get<ECS::TransformComponent>(activeCamera)) {
                    result["activeCamera"]["position"] = SerializeVec3(transform->Position);
                    result["activeCamera"]["rotation"] = SerializeEulerDegrees(transform->Rotation);
                }
            }

            // Entity statistics
            Json stats;
            size_t lightCount = 0, meshCount = 0, cameraCount = 0;
            size_t rigidBodyCount = 0, colliderCount = 0;
            registry.view<ECS::LightComponent>().each([&](auto) { ++lightCount; });
            registry.view<ECS::MeshComponent>().each([&](auto) { ++meshCount; });
            registry.view<ECS::CameraComponent>().each([&](auto) { ++cameraCount; });
            registry.view<ECS::RigidBodyComponent>().each([&](auto) { ++rigidBodyCount; });
            registry.view<ECS::ColliderComponent>().each([&](auto) { ++colliderCount; });

            stats["meshes"] = meshCount;
            stats["lights"] = lightCount;
            stats["cameras"] = cameraCount;
            stats["rigidBodies"] = rigidBodyCount;
            stats["colliders"] = colliderCount;
            result["statistics"] = stats;

            // Entities array
            Json entities = Json::array();
            int shownCount = 0;

            registry.each([&](entt::entity entity) {
                if (maxEntities > 0 && shownCount >= maxEntities) return;

                // Apply name filter
                if (!namePattern.empty()) {
                    if (auto* name = registry.try_get<NameComponent>(entity)) {
                        if (name->Name.find(namePattern) == std::string::npos) {
                            return;
                        }
                    } else {
                        return;
                    }
                }

                // Apply component filters
                if (!componentFilters.empty()) {
                    bool hasRequiredComponent = false;
                    for (const auto& compName : componentFilters) {
                        if (compName == "transform" && registry.all_of<ECS::TransformComponent>(entity)) {
                            hasRequiredComponent = true; break;
                        }
                        if (compName == "light" && registry.all_of<ECS::LightComponent>(entity)) {
                            hasRequiredComponent = true; break;
                        }
                        if (compName == "mesh" && registry.all_of<ECS::MeshComponent>(entity)) {
                            hasRequiredComponent = true; break;
                        }
                        if (compName == "camera" && registry.all_of<ECS::CameraComponent>(entity)) {
                            hasRequiredComponent = true; break;
                        }
                        if (compName == "collider" && registry.all_of<ECS::ColliderComponent>(entity)) {
                            hasRequiredComponent = true; break;
                        }
                        if (compName == "rigidBody" && registry.all_of<ECS::RigidBodyComponent>(entity)) {
                            hasRequiredComponent = true; break;
                        }
                    }
                    if (!hasRequiredComponent) return;
                }

                // Serialize entity
                Json entityJson = SerializeEntity(entity, registry);

                // Optionally include world matrix
                if (includeWorldMatrix) {
                    if (auto* transform = registry.try_get<ECS::TransformComponent>(entity)) {
                        const auto& m = transform->WorldMatrix;
                        entityJson["components"]["transform"]["worldMatrix"] = {
                            {m[0][0], m[0][1], m[0][2], m[0][3]},
                            {m[1][0], m[1][1], m[1][2], m[1][3]},
                            {m[2][0], m[2][1], m[2][2], m[2][3]},
                            {m[3][0], m[3][1], m[3][2], m[3][3]}
                        };
                    }
                }

                entities.push_back(entityJson);
                ++shownCount;
            });

            result["entities"] = entities;
            result["returnedCount"] = shownCount;
            if (maxEntities > 0) {
                result["maxEntities"] = maxEntities;
                result["truncated"] = (shownCount >= maxEntities);
            }

            return ToolResult::SuccessJson(result);
        }
    };

    // ============================================================================
    // SpawnEntity Tool
    // ============================================================================
    // Allows AI agents to instantiate new entities with meshes, colliders, lights,
    // and other components programmatically

    class SpawnEntityTool : public MCPTool {
    public:
        SpawnEntityTool()
            : MCPTool("SpawnEntity",
                      "Spawn a new entity in the scene with specified components. "
                      "Can create meshes, lights, cameras, physics objects, or empty "
                      "entities. Returns the new entity ID and its configuration.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            schema.Properties = {
                {"name", {
                    {"type", "string"},
                    {"description", "Name for the new entity"},
                    {"default", "Entity"}
                }},
                {"template", {
                    {"type", "string"},
                    {"enum", Json::array({"empty", "mesh", "light", "camera", 
                                          "physicsBox", "physicsSphere", "trigger"})},
                    {"description", "Entity template to use as starting point"},
                    {"default", "empty"}
                }},
                {"transform", {
                    {"type", "object"},
                    {"description", "Initial transform for the entity"},
                    {"properties", {
                        {"position", {
                            {"type", "object"},
                            {"properties", {
                                {"x", {{"type", "number"}}},
                                {"y", {{"type", "number"}}},
                                {"z", {{"type", "number"}}}
                            }}
                        }},
                        {"rotation", {
                            {"type", "object"},
                            {"description", "Rotation in degrees (pitch, yaw, roll)"},
                            {"properties", {
                                {"pitch", {{"type", "number"}}},
                                {"yaw", {{"type", "number"}}},
                                {"roll", {{"type", "number"}}}
                            }}
                        }},
                        {"scale", {
                            {"type", "object"},
                            {"properties", {
                                {"x", {{"type", "number"}, {"default", 1.0}}},
                                {"y", {{"type", "number"}, {"default", 1.0}}},
                                {"z", {{"type", "number"}, {"default", 1.0}}}
                            }}
                        }}
                    }}
                }},
                {"components", {
                    {"type", "object"},
                    {"description", "Additional components to add"},
                    {"properties", {
                        {"mesh", {
                            {"type", "object"},
                            {"properties", {
                                {"path", {{"type", "string"}, {"description", "Path to mesh asset"}}},
                                {"visible", {{"type", "boolean"}, {"default", true}}},
                                {"castShadows", {{"type", "boolean"}, {"default", true}}}
                            }}
                        }},
                        {"light", {
                            {"type", "object"},
                            {"properties", {
                                {"type", {{"type", "string"}, {"enum", Json::array({"directional", "point", "spot"})}}},
                                {"color", {
                                    {"type", "object"},
                                    {"properties", {
                                        {"r", {{"type", "number"}}},
                                        {"g", {{"type", "number"}}},
                                        {"b", {{"type", "number"}}}
                                    }}
                                }},
                                {"intensity", {{"type", "number"}, {"default", 1.0}}},
                                {"radius", {{"type", "number"}, {"default", 10.0}}},
                                {"castShadows", {{"type", "boolean"}, {"default", false}}}
                            }}
                        }},
                        {"camera", {
                            {"type", "object"},
                            {"properties", {
                                {"projection", {{"type", "string"}, {"enum", Json::array({"perspective", "orthographic"})}}},
                                {"fieldOfView", {{"type", "number"}, {"default", 60.0}}},
                                {"nearPlane", {{"type", "number"}, {"default", 0.1}}},
                                {"farPlane", {{"type", "number"}, {"default", 1000.0}}},
                                {"isActive", {{"type", "boolean"}, {"default", false}}}
                            }}
                        }},
                        {"collider", {
                            {"type", "object"},
                            {"properties", {
                                {"type", {{"type", "string"}, {"enum", Json::array({"box", "sphere", "capsule"})}}},
                                {"halfExtents", {{"type", "object"}}},
                                {"radius", {{"type", "number"}}},
                                {"halfHeight", {{"type", "number"}}},
                                {"isSensor", {{"type", "boolean"}, {"default", false}}}
                            }}
                        }},
                        {"rigidBody", {
                            {"type", "object"},
                            {"properties", {
                                {"motionType", {{"type", "string"}, {"enum", Json::array({"static", "kinematic", "dynamic"})}}},
                                {"mass", {{"type", "number"}, {"default", 1.0}}},
                                {"gravityEnabled", {{"type", "boolean"}, {"default", true}}}
                            }}
                        }}
                    }}
                }},
                {"parent", {
                    {"type", "integer"},
                    {"description", "Entity ID of parent (for hierarchy)"}
                }}
            };
            schema.Required = {};  // No required fields, defaults will be used
            return schema;
        }

        ToolResult Execute(const Json& arguments, ECS::Scene* scene) override {
            if (!scene) {
                return ToolResult::Error("No active scene available");
            }

            // Parse entity name
            std::string name = arguments.value("name", "Entity");
            std::string templateType = arguments.value("template", "empty");

            // Create the entity
            ECS::Entity entity = scene->CreateEntity(name);
            auto& registry = scene->GetRegistry();
            entt::entity entityHandle = entity.GetHandle();

            // Add NameComponent
            registry.emplace<NameComponent>(entityHandle, NameComponent{name});

            // Apply transform (always added)
            ECS::TransformComponent transform;
            if (arguments.contains("transform")) {
                transform = DeserializeTransform(arguments["transform"]);
            }
            registry.emplace<ECS::TransformComponent>(entityHandle, transform);

            // Apply template
            ApplyTemplate(registry, entityHandle, templateType, arguments);

            // Apply additional components from arguments
            if (arguments.contains("components")) {
                ApplyComponents(registry, entityHandle, arguments["components"]);
            }

            // Handle parent relationship
            if (arguments.contains("parent")) {
                uint32_t parentId = arguments["parent"].get<uint32_t>();
                entt::entity parentEntity = static_cast<entt::entity>(parentId);
                
                if (registry.valid(parentEntity)) {
                    // Add hierarchy component to child
                    auto& childHierarchy = registry.emplace_or_replace<ECS::HierarchyComponent>(entityHandle);
                    childHierarchy.Parent = parentEntity;

                    // Update parent's children list
                    auto& parentHierarchy = registry.get_or_emplace<ECS::HierarchyComponent>(parentEntity);
                    parentHierarchy.Children.push_back(entityHandle);
                    childHierarchy.Depth = parentHierarchy.Depth + 1;
                }
            }

            // Build result
            Json result;
            result["success"] = true;
            result["entityId"] = static_cast<uint32_t>(entityHandle);
            result["name"] = name;
            result["template"] = templateType;

            // Include created components list
            Json componentsCreated = Json::array();
            componentsCreated.push_back("transform");
            
            if (registry.all_of<ECS::LightComponent>(entityHandle)) componentsCreated.push_back("light");
            if (registry.all_of<ECS::MeshComponent>(entityHandle)) componentsCreated.push_back("mesh");
            if (registry.all_of<ECS::CameraComponent>(entityHandle)) componentsCreated.push_back("camera");
            if (registry.all_of<ECS::ColliderComponent>(entityHandle)) componentsCreated.push_back("collider");
            if (registry.all_of<ECS::RigidBodyComponent>(entityHandle)) componentsCreated.push_back("rigidBody");
            if (registry.all_of<ECS::HierarchyComponent>(entityHandle)) componentsCreated.push_back("hierarchy");

            result["components"] = componentsCreated;

            // Include serialized entity data
            result["entity"] = SerializeEntity(entityHandle, registry);

            ENGINE_CORE_INFO("MCP SpawnEntity: Created '{}' (ID: {}) with template '{}'",
                             name, static_cast<uint32_t>(entityHandle), templateType);

            return ToolResult::SuccessJson(result);
        }

    private:
        void ApplyTemplate(entt::registry& registry, entt::entity entity, 
                           const std::string& templateType, const Json& arguments) {
            
            if (templateType == "mesh") {
                // Create a basic mesh entity
                ECS::MeshComponent mesh;
                if (arguments.contains("components") && arguments["components"].contains("mesh")) {
                    mesh = DeserializeMesh(arguments["components"]["mesh"]);
                }
                registry.emplace<ECS::MeshComponent>(entity, mesh);
            }
            else if (templateType == "light") {
                // Create a point light by default
                ECS::LightComponent light;
                if (arguments.contains("components") && arguments["components"].contains("light")) {
                    light = DeserializeLight(arguments["components"]["light"]);
                } else {
                    light = ECS::LightComponent::CreatePoint(
                        Math::Vec3(1.0f, 1.0f, 1.0f), 1.0f, 10.0f);
                }
                registry.emplace<ECS::LightComponent>(entity, light);
            }
            else if (templateType == "camera") {
                // Create a perspective camera
                ECS::CameraComponent camera;
                if (arguments.contains("components") && arguments["components"].contains("camera")) {
                    camera = DeserializeCamera(arguments["components"]["camera"]);
                } else {
                    camera = ECS::CameraComponent::CreatePerspective(60.0f, 0.1f, 1000.0f);
                    camera.IsActive = false;  // Don't activate by default
                }
                registry.emplace<ECS::CameraComponent>(entity, camera);
            }
            else if (templateType == "physicsBox") {
                // Create a dynamic box with collider
                ECS::ColliderComponent collider = ECS::ColliderComponent::CreateBox(
                    Math::Vec3(0.5f, 0.5f, 0.5f));
                registry.emplace<ECS::ColliderComponent>(entity, collider);

                ECS::RigidBodyComponent rigidBody = ECS::RigidBodyComponent::CreateDynamic(1.0f);
                registry.emplace<ECS::RigidBodyComponent>(entity, rigidBody);
            }
            else if (templateType == "physicsSphere") {
                // Create a dynamic sphere with collider
                ECS::ColliderComponent collider = ECS::ColliderComponent::CreateSphere(0.5f);
                registry.emplace<ECS::ColliderComponent>(entity, collider);

                ECS::RigidBodyComponent rigidBody = ECS::RigidBodyComponent::CreateDynamic(1.0f);
                registry.emplace<ECS::RigidBodyComponent>(entity, rigidBody);
            }
            else if (templateType == "trigger") {
                // Create a trigger volume (sensor)
                ECS::ColliderComponent collider = ECS::ColliderComponent::CreateSensor(
                    Math::Vec3(1.0f, 1.0f, 1.0f));
                registry.emplace<ECS::ColliderComponent>(entity, collider);
            }
            // "empty" template adds nothing extra
        }

        void ApplyComponents(entt::registry& registry, entt::entity entity, const Json& components) {
            // Apply mesh component
            if (components.contains("mesh") && !registry.all_of<ECS::MeshComponent>(entity)) {
                auto mesh = DeserializeMesh(components["mesh"]);
                registry.emplace<ECS::MeshComponent>(entity, mesh);
            }

            // Apply light component
            if (components.contains("light") && !registry.all_of<ECS::LightComponent>(entity)) {
                auto light = DeserializeLight(components["light"]);
                registry.emplace<ECS::LightComponent>(entity, light);
            }

            // Apply camera component
            if (components.contains("camera") && !registry.all_of<ECS::CameraComponent>(entity)) {
                auto camera = DeserializeCamera(components["camera"]);
                registry.emplace<ECS::CameraComponent>(entity, camera);
            }

            // Apply collider component
            if (components.contains("collider") && !registry.all_of<ECS::ColliderComponent>(entity)) {
                auto collider = DeserializeCollider(components["collider"]);
                registry.emplace<ECS::ColliderComponent>(entity, collider);
            }

            // Apply rigidBody component
            if (components.contains("rigidBody") && !registry.all_of<ECS::RigidBodyComponent>(entity)) {
                auto rigidBody = DeserializeRigidBody(components["rigidBody"]);
                registry.emplace<ECS::RigidBodyComponent>(entity, rigidBody);
            }
        }
    };

    // ============================================================================
    // Factory function to create all scene tools
    // ============================================================================

    inline std::vector<MCPToolPtr> CreateSceneTools() {
        return {
            std::make_shared<GetSceneContextTool>(),
            std::make_shared<SpawnEntityTool>()
        };
    }

} // namespace MCP
} // namespace Core
