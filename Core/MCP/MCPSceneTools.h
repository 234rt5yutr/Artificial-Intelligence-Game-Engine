#pragma once

// MCP Scene Tools
// Tools for querying and inspecting the game scene via MCP

#include "MCPTool.h"
#include "MCPTypes.h"
#include "SceneSerialization.h"
#include "Core/ECS/Scene.h"
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
    // Factory function to create all scene tools
    // ============================================================================

    inline std::vector<MCPToolPtr> CreateSceneTools() {
        return {
            std::make_shared<GetSceneContextTool>()
        };
    }

} // namespace MCP
} // namespace Core
