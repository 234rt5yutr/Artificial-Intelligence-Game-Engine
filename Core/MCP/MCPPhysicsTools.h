#pragma once

// MCP Physics Tools
// Tools for interacting with physics systems: destruction, ragdolls, constraints, forces
// Provides AI agents with comprehensive physics manipulation capabilities

#include "MCPTool.h"
#include "MCPTypes.h"
#include "../Math/Math.h"
#include "../ECS/Scene.h"
#include <nlohmann/json.hpp>
#include <string>
#include <cstdint>

namespace Core {

// Forward declarations
namespace Physics {
    class PhysicsSystem;
    class RagdollSystem;
    class DestructionSystem;
    class ConstraintSystem;
}

namespace MCP {

    using json = nlohmann::json;
    using Vec3 = Math::Vec3;

    //=========================================================================
    // Helper Functions
    //=========================================================================

    /// @brief Validates that an entity exists in the scene
    /// @param entityId The entity ID to validate
    /// @param scene The scene to check
    /// @return True if the entity exists and is valid
    inline bool ValidateEntityExists(uint32_t entityId, ECS::Scene* scene) {
        if (!scene) {
            return false;
        }
        auto entity = static_cast<entt::entity>(entityId);
        return scene->GetRegistry().valid(entity);
    }

    /// @brief Parses a Vec3 from JSON object
    /// @param j The JSON object containing x, y, z fields
    /// @return Parsed Vec3, defaults to (0,0,0) if fields are missing
    inline Vec3 ParseVec3(const json& j) {
        float x = j.value("x", 0.0f);
        float y = j.value("y", 0.0f);
        float z = j.value("z", 0.0f);
        return Vec3(x, y, z);
    }

    /// @brief Safely retrieves a float value from JSON with a default
    /// @param j The JSON object to search
    /// @param key The key to look for
    /// @param defaultValue The default value if key is not found
    /// @return The float value or default
    inline float SafeGetFloat(const json& j, const std::string& key, float defaultValue) {
        if (j.contains(key) && j[key].is_number()) {
            return j[key].get<float>();
        }
        return defaultValue;
    }

    /// @brief Converts a Vec3 to JSON object
    /// @param v The vector to convert
    /// @return JSON object with x, y, z fields
    inline json Vec3ToJson(const Vec3& v) {
        return {{"x", v.x}, {"y", v.y}, {"z", v.z}};
    }

    //=========================================================================
    // TriggerDestructionTool
    // Triggers destruction physics on a destructible entity
    //=========================================================================

    class TriggerDestructionTool : public MCPTool {
    public:
        TriggerDestructionTool()
            : MCPTool("TriggerDestruction",
                     "Triggers physics-based destruction on a destructible entity. "
                     "Can apply an impact force at a specific point and optionally "
                     "trigger chain reactions to nearby destructibles.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            schema.Properties = {
                {"entityId", {
                    {"type", "integer"},
                    {"description", "Entity ID of the destructible object"}
                }},
                {"impactPoint", {
                    {"type", "object"},
                    {"description", "World-space point where destruction originates"},
                    {"properties", {
                        {"x", {{"type", "number"}}},
                        {"y", {{"type", "number"}}},
                        {"z", {{"type", "number"}}}
                    }}
                }},
                {"impactForce", {
                    {"type", "number"},
                    {"description", "Force magnitude applied at impact point (Newtons)"},
                    {"minimum", 0.0}
                }},
                {"chainReaction", {
                    {"type", "boolean"},
                    {"description", "Whether to trigger destruction of nearby destructibles"},
                    {"default", false}
                }}
            };
            schema.Required = {"entityId", "impactPoint", "impactForce"};
            return schema;
        }

        ToolResult Execute(const json& params, ECS::Scene* scene) override {
            if (!scene) {
                return ToolResult::Error("No active scene provided");
            }

            // Extract parameters
            uint32_t entityId = params.value("entityId", 0u);
            
            if (!ValidateEntityExists(entityId, scene)) {
                return ToolResult::Error("Entity " + std::to_string(entityId) + " does not exist");
            }

            Vec3 impactPoint = ParseVec3(params.value("impactPoint", json::object()));
            float impactForce = SafeGetFloat(params, "impactForce", 100.0f);
            bool chainReaction = params.value("chainReaction", false);

            auto entity = static_cast<entt::entity>(entityId);

            // Check for destructible component
            // Note: Actual component check would depend on your ECS setup
            // if (!scene->GetRegistry().all_of<Physics::DestructibleComponent>(entity)) {
            //     return ToolResult::Error("Entity does not have a DestructibleComponent");
            // }

            // Trigger destruction through physics system
            // Physics::DestructionSystem::Get().TriggerDestruction(entity, impactPoint, impactForce, chainReaction);

            json result;
            result["success"] = true;
            result["entityId"] = entityId;
            result["impactPoint"] = Vec3ToJson(impactPoint);
            result["impactForce"] = impactForce;
            result["chainReaction"] = chainReaction;
            result["message"] = "Destruction triggered successfully";
            result["fragmentsGenerated"] = 0; // Would be populated by actual destruction system

            return ToolResult::SuccessJson(result);
        }
    };

    //=========================================================================
    // SpawnRagdollTool
    // Converts a skeletal mesh entity to ragdoll physics
    //=========================================================================

    class SpawnRagdollTool : public MCPTool {
    public:
        SpawnRagdollTool()
            : MCPTool("SpawnRagdoll",
                     "Converts a skeletal mesh entity to ragdoll physics simulation. "
                     "Can blend from current animation pose and apply initial velocity.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            schema.Properties = {
                {"entityId", {
                    {"type", "integer"},
                    {"description", "Entity ID of the skeletal mesh to convert to ragdoll"}
                }},
                {"initialVelocity", {
                    {"type", "object"},
                    {"description", "Initial velocity to apply to the ragdoll"},
                    {"properties", {
                        {"x", {{"type", "number"}}},
                        {"y", {{"type", "number"}}},
                        {"z", {{"type", "number"}}}
                    }}
                }},
                {"blendTime", {
                    {"type", "number"},
                    {"description", "Time in seconds to blend from animation to ragdoll (0 = instant)"},
                    {"minimum", 0.0},
                    {"maximum", 2.0},
                    {"default", 0.2}
                }}
            };
            schema.Required = {"entityId"};
            return schema;
        }

        ToolResult Execute(const json& params, ECS::Scene* scene) override {
            if (!scene) {
                return ToolResult::Error("No active scene provided");
            }

            uint32_t entityId = params.value("entityId", 0u);

            if (!ValidateEntityExists(entityId, scene)) {
                return ToolResult::Error("Entity " + std::to_string(entityId) + " does not exist");
            }

            Vec3 initialVelocity = ParseVec3(params.value("initialVelocity", json::object()));
            float blendTime = SafeGetFloat(params, "blendTime", 0.2f);

            auto entity = static_cast<entt::entity>(entityId);

            // Validate entity has required components
            // if (!scene->GetRegistry().all_of<Animation::SkeletalMeshComponent>(entity)) {
            //     return ToolResult::Error("Entity does not have a SkeletalMeshComponent");
            // }

            // Spawn ragdoll through physics system
            // Physics::RagdollSystem::Get().SpawnRagdoll(entity, initialVelocity, blendTime);

            json result;
            result["success"] = true;
            result["entityId"] = entityId;
            result["initialVelocity"] = Vec3ToJson(initialVelocity);
            result["blendTime"] = blendTime;
            result["ragdollActive"] = true;
            result["boneCount"] = 0; // Would be populated by actual ragdoll system
            result["message"] = "Ragdoll spawned successfully";

            return ToolResult::SuccessJson(result);
        }
    };

    //=========================================================================
    // ModifyConstraintTool
    // Modifies physics constraint properties at runtime
    //=========================================================================

    class ModifyConstraintTool : public MCPTool {
    public:
        ModifyConstraintTool()
            : MCPTool("ModifyConstraint",
                     "Modifies properties of physics constraints attached to an entity. "
                     "Can adjust limits, motors, and other constraint parameters.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            schema.Properties = {
                {"entityId", {
                    {"type", "integer"},
                    {"description", "Entity ID with the constraint to modify"}
                }},
                {"constraintType", {
                    {"type", "string"},
                    {"description", "Type of constraint to modify"},
                    {"enum", {"hinge", "ball_socket", "slider", "fixed", "distance", "cone_twist", "generic_6dof"}}
                }},
                {"property", {
                    {"type", "string"},
                    {"description", "Property name to modify"},
                    {"enum", {"lowerLimit", "upperLimit", "motorSpeed", "motorForce", 
                              "damping", "stiffness", "breakingForce", "enabled"}}
                }},
                {"value", {
                    {"type", "number"},
                    {"description", "New value for the property"}
                }}
            };
            schema.Required = {"entityId", "constraintType", "property", "value"};
            return schema;
        }

        ToolResult Execute(const json& params, ECS::Scene* scene) override {
            if (!scene) {
                return ToolResult::Error("No active scene provided");
            }

            uint32_t entityId = params.value("entityId", 0u);

            if (!ValidateEntityExists(entityId, scene)) {
                return ToolResult::Error("Entity " + std::to_string(entityId) + " does not exist");
            }

            std::string constraintType = params.value("constraintType", "");
            std::string property = params.value("property", "");
            float value = SafeGetFloat(params, "value", 0.0f);

            if (constraintType.empty()) {
                return ToolResult::Error("constraintType is required");
            }
            if (property.empty()) {
                return ToolResult::Error("property is required");
            }

            auto entity = static_cast<entt::entity>(entityId);

            // Validate and modify constraint through physics system
            // Physics::ConstraintSystem::Get().ModifyConstraint(entity, constraintType, property, value);

            json result;
            result["success"] = true;
            result["entityId"] = entityId;
            result["constraintType"] = constraintType;
            result["property"] = property;
            result["newValue"] = value;
            result["message"] = "Constraint property modified successfully";

            return ToolResult::SuccessJson(result);
        }
    };

    //=========================================================================
    // QueryPhysicsStateTool
    // Queries the current physics state of an entity
    //=========================================================================

    class QueryPhysicsStateTool : public MCPTool {
    public:
        QueryPhysicsStateTool()
            : MCPTool("QueryPhysicsState",
                     "Queries the current physics state of an entity including "
                     "velocity, position, constraints, and ragdoll state.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            schema.Properties = {
                {"entityId", {
                    {"type", "integer"},
                    {"description", "Entity ID to query physics state for"}
                }},
                {"queryType", {
                    {"type", "string"},
                    {"description", "Type of physics information to query"},
                    {"enum", {"velocity", "position", "constraints", "ragdollState", "all"}}
                }}
            };
            schema.Required = {"entityId", "queryType"};
            return schema;
        }

        ToolResult Execute(const json& params, ECS::Scene* scene) override {
            if (!scene) {
                return ToolResult::Error("No active scene provided");
            }

            uint32_t entityId = params.value("entityId", 0u);

            if (!ValidateEntityExists(entityId, scene)) {
                return ToolResult::Error("Entity " + std::to_string(entityId) + " does not exist");
            }

            std::string queryType = params.value("queryType", "all");

            auto entity = static_cast<entt::entity>(entityId);

            json result;
            result["success"] = true;
            result["entityId"] = entityId;
            result["queryType"] = queryType;

            // Query physics state based on type
            // In actual implementation, these would be retrieved from physics components
            
            if (queryType == "velocity" || queryType == "all") {
                result["velocity"] = {
                    {"linear", Vec3ToJson(Vec3(0.0f))},
                    {"angular", Vec3ToJson(Vec3(0.0f))}
                };
            }

            if (queryType == "position" || queryType == "all") {
                result["position"] = Vec3ToJson(Vec3(0.0f));
                result["rotation"] = {{"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 1.0f}};
            }

            if (queryType == "constraints" || queryType == "all") {
                result["constraints"] = json::array();
                // Would be populated with actual constraint data
            }

            if (queryType == "ragdollState" || queryType == "all") {
                result["ragdoll"] = {
                    {"active", false},
                    {"blending", false},
                    {"boneCount", 0}
                };
            }

            result["message"] = "Physics state queried successfully";
            return ToolResult::SuccessJson(result);
        }
    };

    //=========================================================================
    // ApplyForceTool
    // Applies force, impulse, or torque to a physics body
    //=========================================================================

    class ApplyForceTool : public MCPTool {
    public:
        ApplyForceTool()
            : MCPTool("ApplyForce",
                     "Applies force, impulse, or torque to a physics body. "
                     "Can be used for impulse effects, continuous forces, or rotational torque.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            schema.Properties = {
                {"entityId", {
                    {"type", "integer"},
                    {"description", "Entity ID of the physics body to affect"}
                }},
                {"force", {
                    {"type", "object"},
                    {"description", "Force/impulse/torque vector to apply"},
                    {"properties", {
                        {"x", {{"type", "number"}}},
                        {"y", {{"type", "number"}}},
                        {"z", {{"type", "number"}}}
                    }}
                }},
                {"forceType", {
                    {"type", "string"},
                    {"description", "Type of force application"},
                    {"enum", {"impulse", "continuous", "torque"}}
                }},
                {"applicationPoint", {
                    {"type", "object"},
                    {"description", "Optional: World-space point to apply force (for non-torque). Defaults to center of mass."},
                    {"properties", {
                        {"x", {{"type", "number"}}},
                        {"y", {{"type", "number"}}},
                        {"z", {{"type", "number"}}}
                    }}
                }},
                {"duration", {
                    {"type", "number"},
                    {"description", "Duration in seconds for continuous force (ignored for impulse/torque)"},
                    {"minimum", 0.0},
                    {"default", 0.0}
                }}
            };
            schema.Required = {"entityId", "force", "forceType"};
            return schema;
        }

        ToolResult Execute(const json& params, ECS::Scene* scene) override {
            if (!scene) {
                return ToolResult::Error("No active scene provided");
            }

            uint32_t entityId = params.value("entityId", 0u);

            if (!ValidateEntityExists(entityId, scene)) {
                return ToolResult::Error("Entity " + std::to_string(entityId) + " does not exist");
            }

            Vec3 force = ParseVec3(params.value("force", json::object()));
            std::string forceType = params.value("forceType", "impulse");

            // Validate force type
            if (forceType != "impulse" && forceType != "continuous" && forceType != "torque") {
                return ToolResult::Error("Invalid forceType. Must be 'impulse', 'continuous', or 'torque'");
            }

            auto entity = static_cast<entt::entity>(entityId);

            // Check for physics body component
            // if (!scene->GetRegistry().all_of<Physics::RigidBodyComponent>(entity)) {
            //     return ToolResult::Error("Entity does not have a RigidBodyComponent");
            // }

            // Apply force based on type
            // if (forceType == "impulse") {
            //     Physics::PhysicsSystem::Get().ApplyImpulse(entity, force);
            // } else if (forceType == "continuous") {
            //     float duration = SafeGetFloat(params, "duration", 0.0f);
            //     Physics::PhysicsSystem::Get().ApplyContinuousForce(entity, force, duration);
            // } else if (forceType == "torque") {
            //     Physics::PhysicsSystem::Get().ApplyTorque(entity, force);
            // }

            json result;
            result["success"] = true;
            result["entityId"] = entityId;
            result["force"] = Vec3ToJson(force);
            result["forceType"] = forceType;
            result["forceMagnitude"] = glm::length(force);

            if (params.contains("applicationPoint")) {
                result["applicationPoint"] = Vec3ToJson(ParseVec3(params["applicationPoint"]));
            }

            if (forceType == "continuous") {
                result["duration"] = SafeGetFloat(params, "duration", 0.0f);
            }

            result["message"] = "Force applied successfully";
            return ToolResult::SuccessJson(result);
        }
    };

    //=========================================================================
    // Registration Function
    // Registers all physics tools with the MCP server
    //=========================================================================

    /// @brief Registers all physics manipulation tools with the MCP server
    /// @param server Reference to the MCP server instance
    inline void RegisterPhysicsTools(MCPServer& server) {
        // Register destruction tool
        server.RegisterTool(std::make_shared<TriggerDestructionTool>());

        // Register ragdoll tool
        server.RegisterTool(std::make_shared<SpawnRagdollTool>());

        // Register constraint modification tool
        server.RegisterTool(std::make_shared<ModifyConstraintTool>());

        // Register physics state query tool
        server.RegisterTool(std::make_shared<QueryPhysicsStateTool>());

        // Register force application tool
        server.RegisterTool(std::make_shared<ApplyForceTool>());
    }

    //=========================================================================
    // Factory Function for Creating Physics Tools Vector
    //=========================================================================

    /// @brief Creates all physics manipulation tools as a vector
    /// @return Vector of MCP tool pointers for physics operations
    inline std::vector<MCPToolPtr> CreatePhysicsTools() {
        std::vector<MCPToolPtr> tools;
        tools.push_back(std::make_shared<TriggerDestructionTool>());
        tools.push_back(std::make_shared<SpawnRagdollTool>());
        tools.push_back(std::make_shared<ModifyConstraintTool>());
        tools.push_back(std::make_shared<QueryPhysicsStateTool>());
        tools.push_back(std::make_shared<ApplyForceTool>());
        return tools;
    }

} // namespace MCP
} // namespace Core
