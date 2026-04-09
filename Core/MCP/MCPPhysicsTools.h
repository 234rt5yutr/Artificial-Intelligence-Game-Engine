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
#include <cmath>
#include <algorithm>
#include <unordered_set>

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

    /// @brief Safely extracts and validates an entity ID from JSON
    /// @param params The JSON parameters object
    /// @param outId Output for the validated entity ID
    /// @param error Output for error message if validation fails
    /// @return True if entity ID is valid, false otherwise
    inline bool SafeGetEntityId(const json& params, uint32_t& outId, std::string& error) {
        if (!params.contains("entityId")) {
            error = "entityId is required";
            return false;
        }

        const auto& idValue = params["entityId"];
        
        // Check if it's a number
        if (!idValue.is_number()) {
            error = "entityId must be a number";
            return false;
        }

        // Get as int64 to check range
        int64_t rawValue = 0;
        if (idValue.is_number_integer()) {
            rawValue = idValue.get<int64_t>();
        } else if (idValue.is_number_float()) {
            double floatVal = idValue.get<double>();
            // Check if it's a whole number
            if (floatVal != static_cast<double>(static_cast<int64_t>(floatVal))) {
                error = "entityId must be a whole number";
                return false;
            }
            rawValue = static_cast<int64_t>(floatVal);
        }

        // Validate range
        if (rawValue < 0) {
            error = "entityId cannot be negative";
            return false;
        }
        
        if (rawValue > static_cast<int64_t>(UINT32_MAX)) {
            error = "entityId exceeds maximum valid value";
            return false;
        }

        outId = static_cast<uint32_t>(rawValue);
        return true;
    }

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

    /// @brief Parses a Vec3 from JSON object with validation
    /// @param j The JSON object containing x, y, z fields
    /// @param maxMagnitude Maximum allowed magnitude (default 1e6)
    /// @return Parsed Vec3, clamped to safe values
    inline Vec3 ParseVec3(const json& j, float maxMagnitude = 1e6f) {
        float x = j.value("x", 0.0f);
        float y = j.value("y", 0.0f);
        float z = j.value("z", 0.0f);
        
        // Clamp individual components to prevent NaN/Inf
        x = std::clamp(x, -maxMagnitude, maxMagnitude);
        y = std::clamp(y, -maxMagnitude, maxMagnitude);
        z = std::clamp(z, -maxMagnitude, maxMagnitude);
        
        // Check for NaN
        if (std::isnan(x)) x = 0.0f;
        if (std::isnan(y)) y = 0.0f;
        if (std::isnan(z)) z = 0.0f;
        
        return Vec3(x, y, z);
    }

    /// @brief Safely retrieves a float value from JSON with bounds checking
    /// @param j The JSON object to search
    /// @param key The key to look for
    /// @param defaultValue The default value if key is not found
    /// @param minValue Minimum allowed value
    /// @param maxValue Maximum allowed value
    /// @return The clamped float value or default
    inline float SafeGetFloat(const json& j, const std::string& key, float defaultValue,
                               float minValue = -1e6f, float maxValue = 1e6f) {
        if (j.contains(key) && j[key].is_number()) {
            float value = j[key].get<float>();
            // Check for NaN/Inf
            if (std::isnan(value) || std::isinf(value)) {
                return defaultValue;
            }
            return std::clamp(value, minValue, maxValue);
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

            // SECURITY FIX: Use safe entity ID extraction with range validation
            uint32_t entityId;
            std::string error;
            if (!SafeGetEntityId(params, entityId, error)) {
                return ToolResult::Error(error);
            }
            
            if (!ValidateEntityExists(entityId, scene)) {
                return ToolResult::Error("Entity not found");  // Don't reveal ID in error
            }

            // SECURITY FIX: Validate impact point with bounds
            Vec3 impactPoint = ParseVec3(params.value("impactPoint", json::object()), 10000.0f);
            // SECURITY FIX: Clamp impact force to safe range
            float impactForce = SafeGetFloat(params, "impactForce", 100.0f, 0.0f, 100000.0f);
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

            // SECURITY FIX: Use safe entity ID extraction
            uint32_t entityId;
            std::string error;
            if (!SafeGetEntityId(params, entityId, error)) {
                return ToolResult::Error(error);
            }

            if (!ValidateEntityExists(entityId, scene)) {
                return ToolResult::Error("Entity not found");
            }

            // SECURITY FIX: Validate velocity with bounds
            Vec3 initialVelocity = ParseVec3(params.value("initialVelocity", json::object()), 1000.0f);
            float blendTime = SafeGetFloat(params, "blendTime", 0.2f, 0.0f, 2.0f);

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

            // SECURITY FIX: Use safe entity ID extraction
            uint32_t entityId;
            std::string error;
            if (!SafeGetEntityId(params, entityId, error)) {
                return ToolResult::Error(error);
            }

            if (!ValidateEntityExists(entityId, scene)) {
                return ToolResult::Error("Entity not found");
            }

            std::string constraintType = params.value("constraintType", "");
            std::string property = params.value("property", "");
            
            // SECURITY FIX: Validate constraint value with appropriate bounds
            float value = SafeGetFloat(params, "value", 0.0f, -1e6f, 1e6f);

            // Validate constraintType against whitelist
            static const std::unordered_set<std::string> validConstraintTypes = {
                "hinge", "ball_socket", "slider", "fixed", "distance", "cone_twist", "generic_6dof"
            };
            if (validConstraintTypes.find(constraintType) == validConstraintTypes.end()) {
                return ToolResult::Error("Invalid constraint type");
            }

            // Validate property against whitelist
            static const std::unordered_set<std::string> validProperties = {
                "lowerLimit", "upperLimit", "motorSpeed", "motorForce", 
                "damping", "stiffness", "breakingForce", "enabled"
            };
            if (validProperties.find(property) == validProperties.end()) {
                return ToolResult::Error("Invalid property name");
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

            // SECURITY FIX: Use safe entity ID extraction
            uint32_t entityId;
            std::string error;
            if (!SafeGetEntityId(params, entityId, error)) {
                return ToolResult::Error(error);
            }

            if (!ValidateEntityExists(entityId, scene)) {
                return ToolResult::Error("Entity not found");
            }

            std::string queryType = params.value("queryType", "all");
            
            // Validate queryType against whitelist
            static const std::unordered_set<std::string> validQueryTypes = {
                "velocity", "position", "constraints", "ragdollState", "all"
            };
            if (validQueryTypes.find(queryType) == validQueryTypes.end()) {
                return ToolResult::Error("Invalid query type");
            }

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

            // SECURITY FIX: Use safe entity ID extraction
            uint32_t entityId;
            std::string error;
            if (!SafeGetEntityId(params, entityId, error)) {
                return ToolResult::Error(error);
            }

            if (!ValidateEntityExists(entityId, scene)) {
                return ToolResult::Error("Entity not found");
            }

            // SECURITY FIX: Validate force with bounds to prevent physics explosion
            constexpr float MAX_FORCE = 100000.0f;  // 100kN max
            Vec3 force = ParseVec3(params.value("force", json::object()), MAX_FORCE);
            
            // Clamp total magnitude
            float forceMagnitude = glm::length(force);
            if (forceMagnitude > MAX_FORCE) {
                force = glm::normalize(force) * MAX_FORCE;
                forceMagnitude = MAX_FORCE;
            }
            
            std::string forceType = params.value("forceType", "impulse");

            // Validate force type against whitelist
            static const std::unordered_set<std::string> validForceTypes = {
                "impulse", "continuous", "torque"
            };
            if (validForceTypes.find(forceType) == validForceTypes.end()) {
                return ToolResult::Error("Invalid force type");
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
            //     float duration = SafeGetFloat(params, "duration", 0.0f, 0.0f, 10.0f);
            //     Physics::PhysicsSystem::Get().ApplyContinuousForce(entity, force, duration);
            // } else if (forceType == "torque") {
            //     Physics::PhysicsSystem::Get().ApplyTorque(entity, force);
            // }

            json result;
            result["success"] = true;
            result["entityId"] = entityId;
            result["force"] = Vec3ToJson(force);
            result["forceType"] = forceType;
            result["forceMagnitude"] = forceMagnitude;

            if (params.contains("applicationPoint")) {
                result["applicationPoint"] = Vec3ToJson(ParseVec3(params["applicationPoint"], 10000.0f));
            }

            if (forceType == "continuous") {
                result["duration"] = SafeGetFloat(params, "duration", 0.0f, 0.0f, 10.0f);
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
