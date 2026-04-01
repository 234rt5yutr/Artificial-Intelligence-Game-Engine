#pragma once

// ============================================================================
// MCPParticleTools.h
// MCP tools for AI agents to spawn and control particle effects
// 
// Tools provided:
// - SpawnParticleEffect: Create particle emitters at specified locations
// - ModifyEmitter: Modify existing particle emitter parameters
// ============================================================================

#include "MCPTool.h"
#include "MCPTypes.h"
#include "Core/ECS/Scene.h"
#include "Core/ECS/Entity.h"
#include "Core/ECS/Components/Components.h"
#include "Core/ECS/Components/ParticleEmitterComponent.h"
#include "Core/Log.h"

#include <sstream>
#include <algorithm>
#include <string>
#include <unordered_map>

namespace Core {
namespace MCP {

    // ========================================================================
    // SpawnParticleEffect Tool
    // ========================================================================
    // Allows AI agents to spawn particle emitters at specified locations

    class SpawnParticleEffectTool : public MCPTool {
    public:
        SpawnParticleEffectTool()
            : MCPTool("SpawnParticleEffect",
                      "Spawn a particle emitter at a specified location in the world. "
                      "Supports preset effect types (fire, smoke, explosion, sparks, magic, "
                      "rain, snow, dust) or fully custom particle configurations. Can attach "
                      "to existing entities to follow their movement. Returns the entity ID "
                      "of the created emitter for later modification.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            schema.Properties = {
                {"effect_type", {
                    {"type", "string"},
                    {"enum", Json::array({"fire", "smoke", "explosion", "sparks", "magic", 
                                          "rain", "snow", "dust", "custom"})},
                    {"description", "Type of particle effect: 'fire' (flames with upward motion), "
                                    "'smoke' (billowing smoke clouds), 'explosion' (burst of debris), "
                                    "'sparks' (bright particles with gravity), 'magic' (glowing sparkles), "
                                    "'rain' (falling droplets), 'snow' (drifting flakes), "
                                    "'dust' (scattered particles), 'custom' (define all parameters)"}
                }},
                {"position", {
                    {"type", "object"},
                    {"description", "World position to spawn the particle emitter"},
                    {"properties", {
                        {"x", {{"type", "number"}}},
                        {"y", {{"type", "number"}}},
                        {"z", {{"type", "number"}}}
                    }},
                    {"required", Json::array({"x", "y", "z"})}
                }},
                {"rotation", {
                    {"type", "object"},
                    {"description", "Rotation of the emitter in euler angles (degrees)"},
                    {"properties", {
                        {"x", {{"type", "number"}, {"default", 0}}},
                        {"y", {{"type", "number"}, {"default", 0}}},
                        {"z", {{"type", "number"}, {"default", 0}}}
                    }}
                }},
                {"scale", {
                    {"type", "number"},
                    {"minimum", 0.01},
                    {"maximum", 100.0},
                    {"description", "Scale multiplier for the effect size (affects particle sizes and emission area)"},
                    {"default", 1.0}
                }},
                {"duration", {
                    {"type", "number"},
                    {"minimum", 0.0},
                    {"maximum", 3600.0},
                    {"description", "Duration in seconds the effect should last (0 = infinite/looping)"},
                    {"default", 5.0}
                }},
                {"intensity", {
                    {"type", "number"},
                    {"minimum", 0.01},
                    {"maximum", 10.0},
                    {"description", "Intensity multiplier affecting emission rate and particle count"},
                    {"default", 1.0}
                }},
                {"color_override", {
                    {"type", "object"},
                    {"description", "Override the effect's color (RGBA, values 0-1)"},
                    {"properties", {
                        {"r", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                        {"g", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                        {"b", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                        {"a", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}, {"default", 1}}}
                    }}
                }},
                {"attach_to_entity", {
                    {"type", "string"},
                    {"description", "Entity name to attach the emitter to (emitter follows entity)"}
                }},
                {"custom_params", {
                    {"type", "object"},
                    {"description", "Custom parameters for 'custom' effect_type"},
                    {"properties", {
                        {"emission_rate", {{"type", "number"}, {"minimum", 0}, {"maximum", 10000},
                                           {"description", "Particles per second"}}},
                        {"max_particles", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100000},
                                           {"description", "Maximum alive particles"}}},
                        {"lifetime_min", {{"type", "number"}, {"minimum", 0.01},
                                          {"description", "Minimum particle lifetime (seconds)"}}},
                        {"lifetime_max", {{"type", "number"}, {"minimum", 0.01},
                                          {"description", "Maximum particle lifetime (seconds)"}}},
                        {"size_start_min", {{"type", "number"}, {"minimum", 0.001},
                                            {"description", "Minimum starting size"}}},
                        {"size_start_max", {{"type", "number"}, {"minimum", 0.001},
                                            {"description", "Maximum starting size"}}},
                        {"size_end_min", {{"type", "number"}, {"minimum", 0},
                                          {"description", "Minimum ending size"}}},
                        {"size_end_max", {{"type", "number"}, {"minimum", 0},
                                          {"description", "Maximum ending size"}}},
                        {"speed_min", {{"type", "number"}, {"minimum", 0},
                                       {"description", "Minimum initial speed"}}},
                        {"speed_max", {{"type", "number"}, {"minimum", 0},
                                       {"description", "Maximum initial speed"}}},
                        {"gravity", {{"type", "number"},
                                     {"description", "Gravity modifier (-9.81 = normal downward)"}}},
                        {"drag", {{"type", "number"}, {"minimum", 0}, {"maximum", 1},
                                  {"description", "Air resistance (0-1)"}}},
                        {"shape", {{"type", "string"},
                                   {"enum", Json::array({"point", "sphere", "box", "cone", "ring"})},
                                   {"description", "Emitter shape"}}},
                        {"color_start", {{"type", "object"},
                                         {"properties", {
                                             {"r", {{"type", "number"}}},
                                             {"g", {{"type", "number"}}},
                                             {"b", {{"type", "number"}}},
                                             {"a", {{"type", "number"}}}
                                         }}}},
                        {"color_end", {{"type", "object"},
                                       {"properties", {
                                           {"r", {{"type", "number"}}},
                                           {"g", {{"type", "number"}}},
                                           {"b", {{"type", "number"}}},
                                           {"a", {{"type", "number"}}}
                                       }}}},
                        {"looping", {{"type", "boolean"}, {"description", "Whether effect loops"}}}
                    }}
                }}
            };
            schema.Required = {"effect_type", "position"};
            return schema;
        }

        ToolResult Execute(const Json& arguments, ECS::Scene* scene) override {
            if (!scene) {
                return ToolResult::Error("Scene is not available");
            }

            // Parse required arguments
            std::string effectType = arguments.value("effect_type", "");
            if (effectType.empty()) {
                return ToolResult::Error("effect_type is required");
            }

            // Parse position
            if (!arguments.contains("position") || !arguments["position"].is_object()) {
                return ToolResult::Error("position object with x, y, z is required");
            }

            const auto& posJson = arguments["position"];
            if (!posJson.contains("x") || !posJson.contains("y") || !posJson.contains("z")) {
                return ToolResult::Error("position must contain x, y, and z coordinates");
            }

            glm::vec3 position(
                posJson.value("x", 0.0f),
                posJson.value("y", 0.0f),
                posJson.value("z", 0.0f)
            );

            // Parse optional rotation
            glm::vec3 rotation(0.0f);
            if (arguments.contains("rotation") && arguments["rotation"].is_object()) {
                const auto& rotJson = arguments["rotation"];
                rotation.x = glm::radians(rotJson.value("x", 0.0f));
                rotation.y = glm::radians(rotJson.value("y", 0.0f));
                rotation.z = glm::radians(rotJson.value("z", 0.0f));
            }

            // Parse optional parameters
            float scale = std::clamp(arguments.value("scale", 1.0f), 0.01f, 100.0f);
            float duration = std::clamp(arguments.value("duration", 5.0f), 0.0f, 3600.0f);
            float intensity = std::clamp(arguments.value("intensity", 1.0f), 0.01f, 10.0f);

            // Create the particle emitter component based on effect type
            ECS::ParticleEmitterComponent emitter;
            
            if (!CreateEmitterFromType(effectType, emitter, arguments)) {
                return ToolResult::Error("Invalid effect_type: " + effectType);
            }

            // Apply scale
            ApplyScale(emitter, scale);

            // Apply intensity
            emitter.EmissionRate *= intensity;
            emitter.MaxParticles = static_cast<uint32_t>(emitter.MaxParticles * intensity);

            // Apply duration
            if (duration > 0.0f) {
                emitter.Looping = false;
                emitter.Duration = duration;
            } else {
                emitter.Looping = true;
            }

            // Apply color override if specified
            if (arguments.contains("color_override") && arguments["color_override"].is_object()) {
                ApplyColorOverride(emitter, arguments["color_override"]);
            }

            // Create entity name
            static uint32_t effectCounter = 0;
            std::string entityName = "ParticleEffect_" + effectType + "_" + std::to_string(++effectCounter);

            // Check for entity attachment
            std::string attachToEntity;
            if (arguments.contains("attach_to_entity") && arguments["attach_to_entity"].is_string()) {
                attachToEntity = arguments["attach_to_entity"].get<std::string>();
                
                auto parentEntity = scene->FindEntityByName(attachToEntity);
                if (!parentEntity.IsValid()) {
                    return ToolResult::Error("attach_to_entity '" + attachToEntity + "' not found");
                }

                // Get parent position to use as offset base
                if (parentEntity.HasComponent<ECS::TransformComponent>()) {
                    const auto& parentTransform = parentEntity.GetComponent<ECS::TransformComponent>();
                    // Position becomes offset from parent
                    position = position - parentTransform.Position;
                }
            }

            // Create entity
            auto entity = scene->CreateEntity(entityName);

            // Add transform component
            auto& transform = entity.AddComponent<ECS::TransformComponent>();
            transform.Position = position;
            transform.Rotation = glm::quat(rotation);
            transform.Scale = glm::vec3(1.0f);  // Scale applied to particle params, not transform

            // Add particle emitter component
            entity.AddComponent<ECS::ParticleEmitterComponent>(emitter);

            // Set up parent-child relationship if attached
            if (!attachToEntity.empty()) {
                auto parentEntity = scene->FindEntityByName(attachToEntity);
                if (parentEntity.IsValid()) {
                    scene->SetParent(entity, parentEntity);
                }
            }

            // Get entity ID
            uint32_t entityId = static_cast<uint32_t>(entity.GetID());

            // Build result JSON
            Json resultData;
            resultData["entity_id"] = entityId;
            resultData["entity_name"] = entityName;
            resultData["effect_type"] = effectType;
            resultData["position"] = {
                {"x", transform.Position.x},
                {"y", transform.Position.y},
                {"z", transform.Position.z}
            };
            resultData["emission_rate"] = emitter.EmissionRate;
            resultData["max_particles"] = emitter.MaxParticles;
            resultData["duration"] = emitter.Looping ? "infinite" : std::to_string(emitter.Duration) + "s";
            resultData["looping"] = emitter.Looping;
            if (!attachToEntity.empty()) {
                resultData["attached_to"] = attachToEntity;
            }

            ENGINE_CORE_INFO("MCP SpawnParticleEffect: Created {} effect at ({}, {}, {}) [Entity: {}]",
                effectType, position.x, position.y, position.z, entityName);

            std::ostringstream ss;
            ss << "Spawned " << effectType << " particle effect at ("
               << position.x << ", " << position.y << ", " << position.z << ")";
            ss << " [Entity: " << entityName << ", ID: " << entityId << "]";
            if (!attachToEntity.empty()) {
                ss << " attached to '" << attachToEntity << "'";
            }

            return ToolResult::Success(ss.str(), resultData);
        }

    private:
        /**
         * @brief Create emitter configuration based on effect type
         */
        bool CreateEmitterFromType(const std::string& type, 
                                   ECS::ParticleEmitterComponent& emitter,
                                   const Json& arguments) {
            if (type == "fire") {
                emitter = ECS::ParticleEmitterComponent::CreateFire(1.0f);
            }
            else if (type == "smoke") {
                emitter = ECS::ParticleEmitterComponent::CreateSmoke(1.0f);
            }
            else if (type == "explosion") {
                emitter = ECS::ParticleEmitterComponent::CreateExplosion(2.0f);
            }
            else if (type == "sparks") {
                emitter = ECS::ParticleEmitterComponent::CreateSparks(1.0f);
            }
            else if (type == "magic") {
                Math::Vec3 color(0.5f, 0.8f, 1.0f);  // Default blue-white magic
                emitter = ECS::ParticleEmitterComponent::CreateMagic(color);
            }
            else if (type == "rain") {
                emitter = ECS::ParticleEmitterComponent::CreateRain(1.0f);
            }
            else if (type == "snow") {
                emitter = CreateSnowEffect();
            }
            else if (type == "dust") {
                emitter = CreateDustEffect();
            }
            else if (type == "custom") {
                emitter = CreateCustomEmitter(arguments);
            }
            else {
                return false;
            }
            return true;
        }

        /**
         * @brief Create snow particle effect
         */
        ECS::ParticleEmitterComponent CreateSnowEffect() {
            ECS::ParticleEmitterComponent emitter;
            emitter.EmissionRate = 100.0f;
            emitter.MaxParticles = 1000;
            emitter.Lifetime = {3.0f, 6.0f};

            emitter.Shape = ECS::EmitterShape::Box;
            emitter.ShapeParams.BoxDimensions = {15.0f, 0.1f, 15.0f};

            emitter.Velocity.SpeedMin = 1.0f;
            emitter.Velocity.SpeedMax = 2.0f;
            emitter.Velocity.Direction = {0.2f, -1.0f, 0.1f};
            emitter.Velocity.ConeAngle = 15.0f;

            emitter.GravityModifier = 0.0f;  // Constant drift

            emitter.ColorOverTime.ColorKeys.clear();
            emitter.ColorOverTime.AddKey(0.0f, {1.0f, 1.0f, 1.0f, 0.8f});
            emitter.ColorOverTime.AddKey(0.8f, {1.0f, 1.0f, 1.0f, 0.6f});
            emitter.ColorOverTime.AddKey(1.0f, {1.0f, 1.0f, 1.0f, 0.0f});

            emitter.SizeOverTime = {0.03f, 0.06f, 0.02f, 0.04f};

            emitter.Rotation.AngularVelocityMin = -45.0f;
            emitter.Rotation.AngularVelocityMax = 45.0f;

            return emitter;
        }

        /**
         * @brief Create dust particle effect
         */
        ECS::ParticleEmitterComponent CreateDustEffect() {
            ECS::ParticleEmitterComponent emitter;
            emitter.EmissionRate = 30.0f;
            emitter.MaxParticles = 150;
            emitter.Lifetime = {2.0f, 5.0f};

            emitter.Shape = ECS::EmitterShape::Sphere;
            emitter.ShapeParams.Radius = 1.0f;
            emitter.ShapeParams.RadiusThickness = 1.0f;

            emitter.Velocity.SpeedMin = 0.1f;
            emitter.Velocity.SpeedMax = 0.5f;
            emitter.Velocity.ConeAngle = 180.0f;

            emitter.Gravity = {0.0f, 0.1f, 0.0f};  // Slight upward drift
            emitter.UseGlobalGravity = false;
            emitter.Drag = 0.2f;

            emitter.ColorOverTime.ColorKeys.clear();
            emitter.ColorOverTime.AddKey(0.0f, {0.6f, 0.5f, 0.4f, 0.0f});
            emitter.ColorOverTime.AddKey(0.2f, {0.6f, 0.5f, 0.4f, 0.4f});
            emitter.ColorOverTime.AddKey(0.8f, {0.7f, 0.6f, 0.5f, 0.3f});
            emitter.ColorOverTime.AddKey(1.0f, {0.7f, 0.6f, 0.5f, 0.0f});

            emitter.SizeOverTime = {0.05f, 0.15f, 0.2f, 0.4f};

            emitter.Rotation.AngularVelocityMin = -20.0f;
            emitter.Rotation.AngularVelocityMax = 20.0f;

            return emitter;
        }

        /**
         * @brief Create custom emitter from JSON parameters
         */
        ECS::ParticleEmitterComponent CreateCustomEmitter(const Json& arguments) {
            ECS::ParticleEmitterComponent emitter;

            // Check for custom_params
            if (!arguments.contains("custom_params") || !arguments["custom_params"].is_object()) {
                // Return default emitter if no custom params
                return emitter;
            }

            const auto& params = arguments["custom_params"];

            // Emission parameters
            emitter.EmissionRate = params.value("emission_rate", 50.0f);
            emitter.MaxParticles = params.value("max_particles", 500);

            // Lifetime
            float lifetimeMin = params.value("lifetime_min", 1.0f);
            float lifetimeMax = params.value("lifetime_max", 3.0f);
            emitter.Lifetime = {lifetimeMin, lifetimeMax};

            // Size over time
            emitter.SizeOverTime.StartSizeMin = params.value("size_start_min", 0.1f);
            emitter.SizeOverTime.StartSizeMax = params.value("size_start_max", 0.3f);
            emitter.SizeOverTime.EndSizeMin = params.value("size_end_min", 0.0f);
            emitter.SizeOverTime.EndSizeMax = params.value("size_end_max", 0.1f);

            // Velocity
            emitter.Velocity.SpeedMin = params.value("speed_min", 1.0f);
            emitter.Velocity.SpeedMax = params.value("speed_max", 3.0f);

            // Gravity
            if (params.contains("gravity")) {
                float gravity = params["gravity"].get<float>();
                emitter.Gravity = {0.0f, gravity, 0.0f};
                emitter.UseGlobalGravity = false;
            }

            // Drag
            emitter.Drag = params.value("drag", 0.0f);

            // Emitter shape
            if (params.contains("shape") && params["shape"].is_string()) {
                std::string shapeStr = params["shape"].get<std::string>();
                if (shapeStr == "point") {
                    emitter.Shape = ECS::EmitterShape::Point;
                } else if (shapeStr == "sphere") {
                    emitter.Shape = ECS::EmitterShape::Sphere;
                } else if (shapeStr == "box") {
                    emitter.Shape = ECS::EmitterShape::Box;
                } else if (shapeStr == "cone") {
                    emitter.Shape = ECS::EmitterShape::Cone;
                } else if (shapeStr == "ring") {
                    emitter.Shape = ECS::EmitterShape::Ring;
                }
            }

            // Color gradient
            emitter.ColorOverTime.ColorKeys.clear();
            
            Math::Vec4 colorStart(1.0f, 1.0f, 1.0f, 1.0f);
            Math::Vec4 colorEnd(1.0f, 1.0f, 1.0f, 0.0f);

            if (params.contains("color_start") && params["color_start"].is_object()) {
                const auto& cs = params["color_start"];
                colorStart.x = cs.value("r", 1.0f);
                colorStart.y = cs.value("g", 1.0f);
                colorStart.z = cs.value("b", 1.0f);
                colorStart.w = cs.value("a", 1.0f);
            }

            if (params.contains("color_end") && params["color_end"].is_object()) {
                const auto& ce = params["color_end"];
                colorEnd.x = ce.value("r", 1.0f);
                colorEnd.y = ce.value("g", 1.0f);
                colorEnd.z = ce.value("b", 1.0f);
                colorEnd.w = ce.value("a", 0.0f);
            }

            emitter.ColorOverTime.AddKey(0.0f, colorStart);
            emitter.ColorOverTime.AddKey(1.0f, colorEnd);

            // Looping
            emitter.Looping = params.value("looping", true);

            return emitter;
        }

        /**
         * @brief Apply scale multiplier to emitter
         */
        void ApplyScale(ECS::ParticleEmitterComponent& emitter, float scale) {
            // Scale sizes
            emitter.SizeOverTime.StartSizeMin *= scale;
            emitter.SizeOverTime.StartSizeMax *= scale;
            emitter.SizeOverTime.EndSizeMin *= scale;
            emitter.SizeOverTime.EndSizeMax *= scale;

            // Scale emission volume
            emitter.ShapeParams.Radius *= scale;
            emitter.ShapeParams.BoxDimensions *= scale;
            emitter.ShapeParams.ConeRadius *= scale;
            emitter.ShapeParams.ConeLength *= scale;
            emitter.ShapeParams.RingRadius *= scale;

            // Scale velocities
            emitter.Velocity.SpeedMin *= scale;
            emitter.Velocity.SpeedMax *= scale;
        }

        /**
         * @brief Apply color override to emitter
         */
        void ApplyColorOverride(ECS::ParticleEmitterComponent& emitter, const Json& colorJson) {
            float r = colorJson.value("r", 1.0f);
            float g = colorJson.value("g", 1.0f);
            float b = colorJson.value("b", 1.0f);
            float a = colorJson.value("a", 1.0f);

            // Modulate existing colors
            for (auto& key : emitter.ColorOverTime.ColorKeys) {
                key.second.x *= r;
                key.second.y *= g;
                key.second.z *= b;
                key.second.w *= a;
            }
        }
    };

    // ========================================================================
    // ModifyEmitter Tool
    // ========================================================================
    // Allows AI agents to modify existing particle emitters

    class ModifyEmitterTool : public MCPTool {
    public:
        ModifyEmitterTool()
            : MCPTool("ModifyEmitter",
                      "Modify parameters of an existing particle emitter. Can change emission rate, "
                      "colors, sizes, velocities, enable/disable the emitter, or trigger burst "
                      "emissions. Use entity_id returned from SpawnParticleEffect or query the "
                      "scene for particle emitter entities.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            schema.Properties = {
                {"entity_id", {
                    {"type", "integer"},
                    {"description", "Entity ID of the particle emitter to modify"}
                }},
                {"entity_name", {
                    {"type", "string"},
                    {"description", "Entity name of the particle emitter (alternative to entity_id)"}
                }},
                {"modifications", {
                    {"type", "object"},
                    {"description", "Modifications to apply to the emitter"},
                    {"properties", {
                        {"emission_rate", {
                            {"type", "number"},
                            {"minimum", 0},
                            {"maximum", 10000},
                            {"description", "New emission rate (particles per second)"}
                        }},
                        {"enabled", {
                            {"type", "boolean"},
                            {"description", "Enable or disable the emitter"}
                        }},
                        {"playing", {
                            {"type", "boolean"},
                            {"description", "Start or pause particle emission"}
                        }},
                        {"color_start", {
                            {"type", "object"},
                            {"description", "Color at particle birth (RGBA 0-1)"},
                            {"properties", {
                                {"r", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                                {"g", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                                {"b", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                                {"a", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}}
                            }}
                        }},
                        {"color_end", {
                            {"type", "object"},
                            {"description", "Color at particle death (RGBA 0-1)"},
                            {"properties", {
                                {"r", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                                {"g", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                                {"b", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                                {"a", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}}
                            }}
                        }},
                        {"size_start", {
                            {"type", "number"},
                            {"minimum", 0.001},
                            {"maximum", 100},
                            {"description", "Starting particle size (average)"}
                        }},
                        {"size_end", {
                            {"type", "number"},
                            {"minimum", 0},
                            {"maximum", 100},
                            {"description", "Ending particle size (average)"}
                        }},
                        {"lifetime_min", {
                            {"type", "number"},
                            {"minimum", 0.01},
                            {"maximum", 60},
                            {"description", "Minimum particle lifetime (seconds)"}
                        }},
                        {"lifetime_max", {
                            {"type", "number"},
                            {"minimum", 0.01},
                            {"maximum", 60},
                            {"description", "Maximum particle lifetime (seconds)"}
                        }},
                        {"velocity_scale", {
                            {"type", "number"},
                            {"minimum", 0},
                            {"maximum", 10},
                            {"description", "Velocity multiplier (1.0 = unchanged)"}
                        }},
                        {"gravity", {
                            {"type", "number"},
                            {"minimum", -100},
                            {"maximum", 100},
                            {"description", "Y-axis gravity (-9.81 = Earth gravity downward)"}
                        }},
                        {"drag", {
                            {"type", "number"},
                            {"minimum", 0},
                            {"maximum", 1},
                            {"description", "Air resistance (0 = none, 1 = maximum)"}
                        }},
                        {"looping", {
                            {"type", "boolean"},
                            {"description", "Whether the effect loops"}
                        }},
                        {"max_particles", {
                            {"type", "integer"},
                            {"minimum", 1},
                            {"maximum", 100000},
                            {"description", "Maximum number of alive particles"}
                        }},
                        {"trigger_burst", {
                            {"type", "integer"},
                            {"minimum", 1},
                            {"maximum", 10000},
                            {"description", "Immediately spawn this many particles as a burst"}
                        }},
                        {"reset", {
                            {"type", "boolean"},
                            {"description", "Reset the emitter to its initial state"}
                        }}
                    }}
                }}
            };
            schema.Required = {"modifications"};
            return schema;
        }

        ToolResult Execute(const Json& arguments, ECS::Scene* scene) override {
            if (!scene) {
                return ToolResult::Error("Scene is not available");
            }

            // Find the entity
            ECS::Entity entity;
            std::string entityIdentifier;

            if (arguments.contains("entity_id") && arguments["entity_id"].is_number_integer()) {
                uint32_t entityId = arguments["entity_id"].get<uint32_t>();
                entity = scene->GetEntityByID(static_cast<entt::entity>(entityId));
                entityIdentifier = "ID:" + std::to_string(entityId);
            }
            else if (arguments.contains("entity_name") && arguments["entity_name"].is_string()) {
                std::string name = arguments["entity_name"].get<std::string>();
                entity = scene->FindEntityByName(name);
                entityIdentifier = "'" + name + "'";
            }
            else {
                return ToolResult::Error("Either entity_id or entity_name is required");
            }

            if (!entity.IsValid()) {
                return ToolResult::Error("Entity " + entityIdentifier + " not found");
            }

            // Check for ParticleEmitterComponent
            if (!entity.HasComponent<ECS::ParticleEmitterComponent>()) {
                return ToolResult::Error("Entity " + entityIdentifier + " does not have a ParticleEmitterComponent");
            }

            auto& emitter = entity.GetComponent<ECS::ParticleEmitterComponent>();

            // Parse and apply modifications
            if (!arguments.contains("modifications") || !arguments["modifications"].is_object()) {
                return ToolResult::Error("modifications object is required");
            }

            const auto& mods = arguments["modifications"];
            std::vector<std::string> appliedChanges;

            // Emission rate
            if (mods.contains("emission_rate")) {
                float rate = std::clamp(mods["emission_rate"].get<float>(), 0.0f, 10000.0f);
                emitter.EmissionRate = rate;
                appliedChanges.push_back("emission_rate=" + std::to_string(static_cast<int>(rate)));
            }

            // Enabled
            if (mods.contains("enabled")) {
                emitter.Enabled = mods["enabled"].get<bool>();
                appliedChanges.push_back(emitter.Enabled ? "enabled" : "disabled");
            }

            // Playing
            if (mods.contains("playing")) {
                emitter.Playing = mods["playing"].get<bool>();
                appliedChanges.push_back(emitter.Playing ? "playing" : "paused");
            }

            // Color start
            if (mods.contains("color_start") && mods["color_start"].is_object()) {
                const auto& cs = mods["color_start"];
                Math::Vec4 colorStart(
                    cs.value("r", 1.0f),
                    cs.value("g", 1.0f),
                    cs.value("b", 1.0f),
                    cs.value("a", 1.0f)
                );
                
                // Update first color key
                if (!emitter.ColorOverTime.ColorKeys.empty()) {
                    emitter.ColorOverTime.ColorKeys[0].second = colorStart;
                }
                appliedChanges.push_back("color_start");
            }

            // Color end
            if (mods.contains("color_end") && mods["color_end"].is_object()) {
                const auto& ce = mods["color_end"];
                Math::Vec4 colorEnd(
                    ce.value("r", 1.0f),
                    ce.value("g", 1.0f),
                    ce.value("b", 1.0f),
                    ce.value("a", 0.0f)
                );
                
                // Update last color key
                if (!emitter.ColorOverTime.ColorKeys.empty()) {
                    emitter.ColorOverTime.ColorKeys.back().second = colorEnd;
                }
                appliedChanges.push_back("color_end");
            }

            // Size start
            if (mods.contains("size_start")) {
                float size = std::clamp(mods["size_start"].get<float>(), 0.001f, 100.0f);
                emitter.SizeOverTime.StartSizeMin = size * 0.8f;
                emitter.SizeOverTime.StartSizeMax = size * 1.2f;
                appliedChanges.push_back("size_start=" + std::to_string(size));
            }

            // Size end
            if (mods.contains("size_end")) {
                float size = std::clamp(mods["size_end"].get<float>(), 0.0f, 100.0f);
                emitter.SizeOverTime.EndSizeMin = size * 0.8f;
                emitter.SizeOverTime.EndSizeMax = size * 1.2f;
                appliedChanges.push_back("size_end=" + std::to_string(size));
            }

            // Lifetime
            if (mods.contains("lifetime_min")) {
                emitter.Lifetime.x = std::clamp(mods["lifetime_min"].get<float>(), 0.01f, 60.0f);
                appliedChanges.push_back("lifetime_min=" + std::to_string(emitter.Lifetime.x));
            }

            if (mods.contains("lifetime_max")) {
                emitter.Lifetime.y = std::clamp(mods["lifetime_max"].get<float>(), 0.01f, 60.0f);
                appliedChanges.push_back("lifetime_max=" + std::to_string(emitter.Lifetime.y));
            }

            // Ensure lifetime_min <= lifetime_max
            if (emitter.Lifetime.x > emitter.Lifetime.y) {
                std::swap(emitter.Lifetime.x, emitter.Lifetime.y);
            }

            // Velocity scale
            if (mods.contains("velocity_scale")) {
                float scale = std::clamp(mods["velocity_scale"].get<float>(), 0.0f, 10.0f);
                emitter.Velocity.SpeedMin *= scale;
                emitter.Velocity.SpeedMax *= scale;
                appliedChanges.push_back("velocity_scale=" + std::to_string(scale));
            }

            // Gravity
            if (mods.contains("gravity")) {
                float gravity = std::clamp(mods["gravity"].get<float>(), -100.0f, 100.0f);
                emitter.Gravity = {0.0f, gravity, 0.0f};
                emitter.UseGlobalGravity = false;
                appliedChanges.push_back("gravity=" + std::to_string(gravity));
            }

            // Drag
            if (mods.contains("drag")) {
                emitter.Drag = std::clamp(mods["drag"].get<float>(), 0.0f, 1.0f);
                appliedChanges.push_back("drag=" + std::to_string(emitter.Drag));
            }

            // Looping
            if (mods.contains("looping")) {
                emitter.Looping = mods["looping"].get<bool>();
                appliedChanges.push_back(emitter.Looping ? "looping=true" : "looping=false");
            }

            // Max particles
            if (mods.contains("max_particles")) {
                emitter.MaxParticles = std::clamp(
                    mods["max_particles"].get<uint32_t>(), 1u, 100000u
                );
                appliedChanges.push_back("max_particles=" + std::to_string(emitter.MaxParticles));
            }

            // Trigger burst
            if (mods.contains("trigger_burst")) {
                uint32_t burstCount = std::clamp(
                    mods["trigger_burst"].get<uint32_t>(), 1u, 10000u
                );
                
                // Configure burst
                emitter.Burst.Enabled = true;
                emitter.Burst.Count = burstCount;
                emitter.Burst.Cycles = 1;
                emitter.Burst.Time = 0.0f;
                emitter.BurstTimer = 0.0f;  // Trigger immediately
                emitter.BurstCycleCount = 0;
                
                appliedChanges.push_back("burst=" + std::to_string(burstCount));
            }

            // Reset
            if (mods.contains("reset") && mods["reset"].get<bool>()) {
                emitter.Reset();
                appliedChanges.push_back("reset");
            }

            // Build result
            Json resultData;
            resultData["entity_id"] = static_cast<uint32_t>(entity.GetID());
            resultData["changes_applied"] = appliedChanges;
            resultData["current_state"] = {
                {"enabled", emitter.Enabled},
                {"playing", emitter.Playing},
                {"emission_rate", emitter.EmissionRate},
                {"max_particles", emitter.MaxParticles},
                {"looping", emitter.Looping}
            };

            ENGINE_CORE_INFO("MCP ModifyEmitter: Modified emitter on entity {} - {} changes",
                entityIdentifier, appliedChanges.size());

            std::ostringstream ss;
            ss << "Modified emitter on entity " << entityIdentifier << ": ";
            for (size_t i = 0; i < appliedChanges.size(); ++i) {
                if (i > 0) ss << ", ";
                ss << appliedChanges[i];
            }

            return ToolResult::Success(ss.str(), resultData);
        }
    };

    // ========================================================================
    // GetParticleInfo Tool
    // ========================================================================
    // Query particle emitter state and information

    class GetParticleInfoTool : public MCPTool {
    public:
        GetParticleInfoTool()
            : MCPTool("GetParticleInfo",
                      "Query information about particle emitters in the scene. Can get details "
                      "about a specific emitter or list all particle emitters. Returns current "
                      "state, configuration, and runtime statistics.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            schema.Properties = {
                {"entity_id", {
                    {"type", "integer"},
                    {"description", "Entity ID to query (omit to list all emitters)"}
                }},
                {"entity_name", {
                    {"type", "string"},
                    {"description", "Entity name to query (alternative to entity_id)"}
                }},
                {"include_config", {
                    {"type", "boolean"},
                    {"description", "Include full emitter configuration in response"},
                    {"default", false}
                }}
            };
            return schema;
        }

        ToolResult Execute(const Json& arguments, ECS::Scene* scene) override {
            if (!scene) {
                return ToolResult::Error("Scene is not available");
            }

            bool includeConfig = arguments.value("include_config", false);

            // Check if querying specific entity
            if (arguments.contains("entity_id") || arguments.contains("entity_name")) {
                return QuerySingleEmitter(arguments, scene, includeConfig);
            }

            // List all particle emitters
            return ListAllEmitters(scene, includeConfig);
        }

    private:
        ToolResult QuerySingleEmitter(const Json& arguments, ECS::Scene* scene, bool includeConfig) {
            ECS::Entity entity;
            std::string entityIdentifier;

            if (arguments.contains("entity_id") && arguments["entity_id"].is_number_integer()) {
                uint32_t entityId = arguments["entity_id"].get<uint32_t>();
                entity = scene->GetEntityByID(static_cast<entt::entity>(entityId));
                entityIdentifier = "ID:" + std::to_string(entityId);
            }
            else if (arguments.contains("entity_name") && arguments["entity_name"].is_string()) {
                std::string name = arguments["entity_name"].get<std::string>();
                entity = scene->FindEntityByName(name);
                entityIdentifier = "'" + name + "'";
            }

            if (!entity.IsValid()) {
                return ToolResult::Error("Entity " + entityIdentifier + " not found");
            }

            if (!entity.HasComponent<ECS::ParticleEmitterComponent>()) {
                return ToolResult::Error("Entity " + entityIdentifier + " does not have a ParticleEmitterComponent");
            }

            const auto& emitter = entity.GetComponent<ECS::ParticleEmitterComponent>();
            
            Json resultData = BuildEmitterInfo(entity, emitter, includeConfig);

            std::ostringstream ss;
            ss << "Emitter " << entityIdentifier << ": ";
            ss << (emitter.IsActive() ? "active" : "inactive");
            ss << ", rate=" << emitter.EmissionRate << "/s";
            ss << ", max=" << emitter.MaxParticles;
            ss << ", elapsed=" << emitter.ElapsedTime << "s";

            return ToolResult::Success(ss.str(), resultData);
        }

        ToolResult ListAllEmitters(ECS::Scene* scene, bool includeConfig) {
            Json emitters = Json::array();
            int count = 0;

            scene->ForEach<ECS::ParticleEmitterComponent>([&](ECS::Entity entity, 
                                                               ECS::ParticleEmitterComponent& emitter) {
                Json info = BuildEmitterInfo(entity, emitter, includeConfig);
                emitters.push_back(info);
                count++;
            });

            Json resultData;
            resultData["emitter_count"] = count;
            resultData["emitters"] = emitters;

            std::ostringstream ss;
            ss << "Found " << count << " particle emitter(s) in scene";

            return ToolResult::Success(ss.str(), resultData);
        }

        Json BuildEmitterInfo(ECS::Entity entity, 
                              const ECS::ParticleEmitterComponent& emitter,
                              bool includeConfig) {
            Json info;
            
            info["entity_id"] = static_cast<uint32_t>(entity.GetID());
            
            if (entity.HasComponent<ECS::NameComponent>()) {
                info["entity_name"] = entity.GetComponent<ECS::NameComponent>().Name;
            }

            // Runtime state
            info["state"] = {
                {"enabled", emitter.Enabled},
                {"playing", emitter.Playing},
                {"active", emitter.IsActive()},
                {"finished", emitter.IsFinished()},
                {"looping", emitter.Looping},
                {"elapsed_time", emitter.ElapsedTime},
                {"estimated_particles", emitter.EstimateParticleCount()}
            };

            // Position if available
            if (entity.HasComponent<ECS::TransformComponent>()) {
                const auto& transform = entity.GetComponent<ECS::TransformComponent>();
                info["position"] = {
                    {"x", transform.Position.x},
                    {"y", transform.Position.y},
                    {"z", transform.Position.z}
                };
            }

            if (includeConfig) {
                info["config"] = {
                    {"emission_rate", emitter.EmissionRate},
                    {"max_particles", emitter.MaxParticles},
                    {"duration", emitter.Duration},
                    {"lifetime", {
                        {"min", emitter.Lifetime.x},
                        {"max", emitter.Lifetime.y}
                    }},
                    {"size", {
                        {"start_min", emitter.SizeOverTime.StartSizeMin},
                        {"start_max", emitter.SizeOverTime.StartSizeMax},
                        {"end_min", emitter.SizeOverTime.EndSizeMin},
                        {"end_max", emitter.SizeOverTime.EndSizeMax}
                    }},
                    {"velocity", {
                        {"speed_min", emitter.Velocity.SpeedMin},
                        {"speed_max", emitter.Velocity.SpeedMax},
                        {"cone_angle", emitter.Velocity.ConeAngle}
                    }},
                    {"gravity_modifier", emitter.GravityModifier},
                    {"drag", emitter.Drag},
                    {"burst_enabled", emitter.Burst.Enabled}
                };

                // Shape info
                std::string shapeStr;
                switch (emitter.Shape) {
                    case ECS::EmitterShape::Point: shapeStr = "point"; break;
                    case ECS::EmitterShape::Sphere: shapeStr = "sphere"; break;
                    case ECS::EmitterShape::Box: shapeStr = "box"; break;
                    case ECS::EmitterShape::Cone: shapeStr = "cone"; break;
                    case ECS::EmitterShape::Ring: shapeStr = "ring"; break;
                }
                info["config"]["shape"] = shapeStr;
            }

            return info;
        }
    };

    // ========================================================================
    // Tool Registration Helper
    // ========================================================================

    /**
     * @brief Register all particle MCP tools with the server
     * @param server The MCP server to register tools with
     */
    inline void RegisterParticleTools(MCPServer& server) {
        server.RegisterTool(std::make_shared<SpawnParticleEffectTool>());
        server.RegisterTool(std::make_shared<ModifyEmitterTool>());
        server.RegisterTool(std::make_shared<GetParticleInfoTool>());
        
        ENGINE_CORE_INFO("MCP: Registered particle tools (SpawnParticleEffect, ModifyEmitter, GetParticleInfo)");
    }

    /**
     * @brief Create all particle MCP tools as a vector
     * @return Vector of particle tool pointers
     */
    inline std::vector<MCPToolPtr> CreateParticleTools() {
        return {
            std::make_shared<SpawnParticleEffectTool>(),
            std::make_shared<ModifyEmitterTool>(),
            std::make_shared<GetParticleInfoTool>()
        };
    }

} // namespace MCP
} // namespace Core
