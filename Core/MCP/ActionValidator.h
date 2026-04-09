#pragma once

// MCP Action Validator
// Sandboxed validation layer to ensure AI agents cannot crash the engine
// with invalid transforms, extreme allocations, or malicious inputs

#include "MCPTypes.h"
#include "Core/Log.h"

#include <string>
#include <vector>
#include <cmath>
#include <limits>
#include <regex>
#include <unordered_set>

namespace Core {
namespace MCP {

    // ============================================================================
    // Validation Configuration
    // ============================================================================

    struct ValidationLimits {
        // Transform limits
        float MaxPosition = 1000000.0f;        // Max absolute position value
        float MinScale = 0.0001f;              // Min scale value (prevent zero/negative)
        float MaxScale = 10000.0f;             // Max scale value
        float MaxRotationDegrees = 360000.0f;  // Max rotation (1000 full rotations)

        // Physics limits
        float MaxMass = 1000000.0f;            // Max mass in kg
        float MinMass = 0.0001f;               // Min mass (prevent zero)
        float MaxVelocity = 10000.0f;          // Max velocity magnitude
        float MaxForce = 1000000.0f;           // Max force magnitude
        float MaxImpulse = 100000.0f;          // Max impulse magnitude

        // Light limits
        float MaxLightIntensity = 1000000.0f;  // Max light intensity
        float MaxLightRadius = 100000.0f;      // Max light radius

        // Camera limits
        float MinFOV = 1.0f;                   // Min field of view (degrees)
        float MaxFOV = 179.0f;                 // Max field of view (degrees)
        float MinNearPlane = 0.0001f;          // Min near plane distance
        float MaxFarPlane = 1000000.0f;        // Max far plane distance

        // Collider limits
        float MinColliderExtent = 0.0001f;     // Min collider dimension
        float MaxColliderExtent = 100000.0f;   // Max collider dimension

        // Entity limits
        size_t MaxEntitiesPerOperation = 1000; // Max entities to spawn/modify at once
        size_t MaxTotalEntities = 100000;      // Max total entities in scene
        size_t MaxComponentsPerEntity = 50;    // Max components per entity

        // String limits
        size_t MaxStringLength = 10000;        // Max string length (names, paths)
        size_t MaxScriptLength = 100000;       // Max script length

        // Array limits
        size_t MaxArraySize = 10000;           // Max array elements
        size_t MaxJSONDepth = 20;              // Max nested JSON depth

        // Resource limits
        size_t MaxMeshVertices = 10000000;     // Max mesh vertices
        size_t MaxTextureSize = 16384;         // Max texture dimension
    };

    // ============================================================================
    // Validation Result
    // ============================================================================

    struct ValidationResult {
        bool IsValid = true;
        std::vector<std::string> Errors;
        std::vector<std::string> Warnings;
        Json SanitizedInput;  // Cleaned/clamped input

        void AddError(const std::string& error) {
            IsValid = false;
            Errors.push_back(error);
        }

        void AddWarning(const std::string& warning) {
            Warnings.push_back(warning);
        }

        std::string GetErrorSummary() const {
            std::string summary;
            for (const auto& err : Errors) {
                if (!summary.empty()) summary += "; ";
                summary += err;
            }
            return summary;
        }
    };

    // ============================================================================
    // Action Validator
    // ============================================================================

    class ActionValidator {
    public:
        ActionValidator() = default;
        explicit ActionValidator(const ValidationLimits& limits) : m_Limits(limits) {}

        // Set validation limits
        void SetLimits(const ValidationLimits& limits) { m_Limits = limits; }
        const ValidationLimits& GetLimits() const { return m_Limits; }

        // ========================================================================
        // Core Validation Functions
        // ========================================================================

        // Validate a float value within range
        bool ValidateFloat(float value, float minVal, float maxVal, 
                          const std::string& fieldName, ValidationResult& result) const {
            if (std::isnan(value)) {
                result.AddError(fieldName + " is NaN");
                return false;
            }
            if (std::isinf(value)) {
                result.AddError(fieldName + " is infinite");
                return false;
            }
            if (value < minVal || value > maxVal) {
                result.AddError(fieldName + " value " + std::to_string(value) + 
                              " out of range [" + std::to_string(minVal) + ", " + 
                              std::to_string(maxVal) + "]");
                return false;
            }
            return true;
        }

        // Validate and clamp a float value (sanitize rather than reject)
        float ClampFloat(float value, float minVal, float maxVal, 
                        const std::string& fieldName, ValidationResult& result) const {
            if (std::isnan(value) || std::isinf(value)) {
                result.AddWarning(fieldName + " was invalid, defaulting to " + 
                                 std::to_string(minVal));
                return minVal;
            }
            if (value < minVal) {
                result.AddWarning(fieldName + " clamped from " + std::to_string(value) + 
                                 " to " + std::to_string(minVal));
                return minVal;
            }
            if (value > maxVal) {
                result.AddWarning(fieldName + " clamped from " + std::to_string(value) + 
                                 " to " + std::to_string(maxVal));
                return maxVal;
            }
            return value;
        }

        // Validate string length and content
        bool ValidateString(const std::string& value, size_t maxLength,
                           const std::string& fieldName, ValidationResult& result) const {
            if (value.length() > maxLength) {
                result.AddError(fieldName + " exceeds max length of " + std::to_string(maxLength));
                return false;
            }
            // Check for null bytes (could cause issues in C strings)
            if (value.find('\0') != std::string::npos) {
                result.AddError(fieldName + " contains null bytes");
                return false;
            }
            return true;
        }

        // Validate JSON structure depth
        bool ValidateJSONDepth(const Json& json, size_t maxDepth, size_t currentDepth,
                              ValidationResult& result) const {
            if (currentDepth > maxDepth) {
                result.AddError("JSON structure exceeds max depth of " + std::to_string(maxDepth));
                return false;
            }
            
            if (json.is_object()) {
                for (auto& [key, value] : json.items()) {
                    if (!ValidateJSONDepth(value, maxDepth, currentDepth + 1, result)) {
                        return false;
                    }
                }
            } else if (json.is_array()) {
                if (json.size() > m_Limits.MaxArraySize) {
                    result.AddError("JSON array size " + std::to_string(json.size()) + 
                                  " exceeds limit of " + std::to_string(m_Limits.MaxArraySize));
                    return false;
                }
                for (const auto& item : json) {
                    if (!ValidateJSONDepth(item, maxDepth, currentDepth + 1, result)) {
                        return false;
                    }
                }
            }
            return true;
        }

        // ========================================================================
        // Transform Validation
        // ========================================================================

        ValidationResult ValidateTransform(const Json& transform) const {
            ValidationResult result;
            result.SanitizedInput = transform;

            if (!transform.is_object()) {
                result.AddError("Transform must be an object");
                return result;
            }

            // Validate position
            if (transform.contains("position")) {
                auto& pos = transform["position"];
                if (pos.is_object()) {
                    auto& sanitized = result.SanitizedInput["position"];
                    if (pos.contains("x")) {
                        sanitized["x"] = ClampFloat(pos.value("x", 0.0f), 
                            -m_Limits.MaxPosition, m_Limits.MaxPosition, "position.x", result);
                    }
                    if (pos.contains("y")) {
                        sanitized["y"] = ClampFloat(pos.value("y", 0.0f), 
                            -m_Limits.MaxPosition, m_Limits.MaxPosition, "position.y", result);
                    }
                    if (pos.contains("z")) {
                        sanitized["z"] = ClampFloat(pos.value("z", 0.0f), 
                            -m_Limits.MaxPosition, m_Limits.MaxPosition, "position.z", result);
                    }
                }
            }

            // Validate rotation
            if (transform.contains("rotation")) {
                auto& rot = transform["rotation"];
                if (rot.is_object()) {
                    auto& sanitized = result.SanitizedInput["rotation"];
                    if (rot.contains("pitch")) {
                        sanitized["pitch"] = ClampFloat(rot.value("pitch", 0.0f), 
                            -m_Limits.MaxRotationDegrees, m_Limits.MaxRotationDegrees, 
                            "rotation.pitch", result);
                    }
                    if (rot.contains("yaw")) {
                        sanitized["yaw"] = ClampFloat(rot.value("yaw", 0.0f), 
                            -m_Limits.MaxRotationDegrees, m_Limits.MaxRotationDegrees, 
                            "rotation.yaw", result);
                    }
                    if (rot.contains("roll")) {
                        sanitized["roll"] = ClampFloat(rot.value("roll", 0.0f), 
                            -m_Limits.MaxRotationDegrees, m_Limits.MaxRotationDegrees, 
                            "rotation.roll", result);
                    }
                }
            }

            // Validate scale
            if (transform.contains("scale")) {
                auto& scale = transform["scale"];
                if (scale.is_object()) {
                    auto& sanitized = result.SanitizedInput["scale"];
                    if (scale.contains("x")) {
                        sanitized["x"] = ClampFloat(scale.value("x", 1.0f), 
                            m_Limits.MinScale, m_Limits.MaxScale, "scale.x", result);
                    }
                    if (scale.contains("y")) {
                        sanitized["y"] = ClampFloat(scale.value("y", 1.0f), 
                            m_Limits.MinScale, m_Limits.MaxScale, "scale.y", result);
                    }
                    if (scale.contains("z")) {
                        sanitized["z"] = ClampFloat(scale.value("z", 1.0f), 
                            m_Limits.MinScale, m_Limits.MaxScale, "scale.z", result);
                    }
                }
            }

            return result;
        }

        // ========================================================================
        // Light Validation
        // ========================================================================

        ValidationResult ValidateLight(const Json& light) const {
            ValidationResult result;
            result.SanitizedInput = light;

            if (!light.is_object()) {
                result.AddError("Light must be an object");
                return result;
            }

            // Validate color (RGB 0-1)
            if (light.contains("color")) {
                auto& color = light["color"];
                if (color.is_object()) {
                    auto& sanitized = result.SanitizedInput["color"];
                    if (color.contains("r")) {
                        sanitized["r"] = ClampFloat(color.value("r", 1.0f), 0.0f, 1.0f, "color.r", result);
                    }
                    if (color.contains("g")) {
                        sanitized["g"] = ClampFloat(color.value("g", 1.0f), 0.0f, 1.0f, "color.g", result);
                    }
                    if (color.contains("b")) {
                        sanitized["b"] = ClampFloat(color.value("b", 1.0f), 0.0f, 1.0f, "color.b", result);
                    }
                }
            }

            // Validate intensity
            if (light.contains("intensity")) {
                result.SanitizedInput["intensity"] = ClampFloat(
                    light.value("intensity", 1.0f), 0.0f, m_Limits.MaxLightIntensity, 
                    "intensity", result);
            }

            // Validate radius
            if (light.contains("radius")) {
                result.SanitizedInput["radius"] = ClampFloat(
                    light.value("radius", 10.0f), 0.01f, m_Limits.MaxLightRadius, 
                    "radius", result);
            }

            // Validate cone angles (degrees)
            if (light.contains("innerConeAngle")) {
                result.SanitizedInput["innerConeAngle"] = ClampFloat(
                    light.value("innerConeAngle", 30.0f), 0.0f, 180.0f, 
                    "innerConeAngle", result);
            }
            if (light.contains("outerConeAngle")) {
                result.SanitizedInput["outerConeAngle"] = ClampFloat(
                    light.value("outerConeAngle", 45.0f), 0.0f, 180.0f, 
                    "outerConeAngle", result);
            }

            return result;
        }

        // ========================================================================
        // Physics Validation
        // ========================================================================

        ValidationResult ValidateRigidBody(const Json& rigidBody) const {
            ValidationResult result;
            result.SanitizedInput = rigidBody;

            if (!rigidBody.is_object()) {
                result.AddError("RigidBody must be an object");
                return result;
            }

            // Validate mass
            if (rigidBody.contains("mass")) {
                result.SanitizedInput["mass"] = ClampFloat(
                    rigidBody.value("mass", 1.0f), m_Limits.MinMass, m_Limits.MaxMass, 
                    "mass", result);
            }

            // Validate damping
            if (rigidBody.contains("linearDamping")) {
                result.SanitizedInput["linearDamping"] = ClampFloat(
                    rigidBody.value("linearDamping", 0.0f), 0.0f, 1.0f, 
                    "linearDamping", result);
            }
            if (rigidBody.contains("angularDamping")) {
                result.SanitizedInput["angularDamping"] = ClampFloat(
                    rigidBody.value("angularDamping", 0.0f), 0.0f, 1.0f, 
                    "angularDamping", result);
            }

            // Validate velocities
            if (rigidBody.contains("linearVelocity")) {
                auto& vel = rigidBody["linearVelocity"];
                if (vel.is_object()) {
                    auto& sanitized = result.SanitizedInput["linearVelocity"];
                    if (vel.contains("x")) {
                        sanitized["x"] = ClampFloat(vel.value("x", 0.0f), 
                            -m_Limits.MaxVelocity, m_Limits.MaxVelocity, "linearVelocity.x", result);
                    }
                    if (vel.contains("y")) {
                        sanitized["y"] = ClampFloat(vel.value("y", 0.0f), 
                            -m_Limits.MaxVelocity, m_Limits.MaxVelocity, "linearVelocity.y", result);
                    }
                    if (vel.contains("z")) {
                        sanitized["z"] = ClampFloat(vel.value("z", 0.0f), 
                            -m_Limits.MaxVelocity, m_Limits.MaxVelocity, "linearVelocity.z", result);
                    }
                }
            }

            if (rigidBody.contains("angularVelocity")) {
                auto& vel = rigidBody["angularVelocity"];
                if (vel.is_object()) {
                    auto& sanitized = result.SanitizedInput["angularVelocity"];
                    if (vel.contains("x")) {
                        sanitized["x"] = ClampFloat(vel.value("x", 0.0f), 
                            -m_Limits.MaxVelocity, m_Limits.MaxVelocity, "angularVelocity.x", result);
                    }
                    if (vel.contains("y")) {
                        sanitized["y"] = ClampFloat(vel.value("y", 0.0f), 
                            -m_Limits.MaxVelocity, m_Limits.MaxVelocity, "angularVelocity.y", result);
                    }
                    if (vel.contains("z")) {
                        sanitized["z"] = ClampFloat(vel.value("z", 0.0f), 
                            -m_Limits.MaxVelocity, m_Limits.MaxVelocity, "angularVelocity.z", result);
                    }
                }
            }

            return result;
        }

        // ========================================================================
        // Camera Validation
        // ========================================================================

        ValidationResult ValidateCamera(const Json& camera) const {
            ValidationResult result;
            result.SanitizedInput = camera;

            if (!camera.is_object()) {
                result.AddError("Camera must be an object");
                return result;
            }

            // Validate FOV
            if (camera.contains("fieldOfView")) {
                result.SanitizedInput["fieldOfView"] = ClampFloat(
                    camera.value("fieldOfView", 60.0f), m_Limits.MinFOV, m_Limits.MaxFOV, 
                    "fieldOfView", result);
            }

            // Validate near/far planes
            if (camera.contains("nearPlane")) {
                result.SanitizedInput["nearPlane"] = ClampFloat(
                    camera.value("nearPlane", 0.1f), m_Limits.MinNearPlane, m_Limits.MaxFarPlane, 
                    "nearPlane", result);
            }
            if (camera.contains("farPlane")) {
                float nearPlane = camera.value("nearPlane", 0.1f);
                float farPlane = camera.value("farPlane", 1000.0f);
                if (farPlane <= nearPlane) {
                    result.AddWarning("farPlane must be greater than nearPlane, adjusting");
                    farPlane = nearPlane + 1.0f;
                }
                result.SanitizedInput["farPlane"] = ClampFloat(
                    farPlane, nearPlane + 0.001f, m_Limits.MaxFarPlane, "farPlane", result);
            }

            return result;
        }

        // ========================================================================
        // Collider Validation
        // ========================================================================

        ValidationResult ValidateCollider(const Json& collider) const {
            ValidationResult result;
            result.SanitizedInput = collider;

            if (!collider.is_object()) {
                result.AddError("Collider must be an object");
                return result;
            }

            // Validate half extents (for box collider)
            if (collider.contains("halfExtents")) {
                auto& ext = collider["halfExtents"];
                if (ext.is_object()) {
                    auto& sanitized = result.SanitizedInput["halfExtents"];
                    if (ext.contains("x")) {
                        sanitized["x"] = ClampFloat(ext.value("x", 0.5f), 
                            m_Limits.MinColliderExtent, m_Limits.MaxColliderExtent, 
                            "halfExtents.x", result);
                    }
                    if (ext.contains("y")) {
                        sanitized["y"] = ClampFloat(ext.value("y", 0.5f), 
                            m_Limits.MinColliderExtent, m_Limits.MaxColliderExtent, 
                            "halfExtents.y", result);
                    }
                    if (ext.contains("z")) {
                        sanitized["z"] = ClampFloat(ext.value("z", 0.5f), 
                            m_Limits.MinColliderExtent, m_Limits.MaxColliderExtent, 
                            "halfExtents.z", result);
                    }
                }
            }

            // Validate radius (for sphere/capsule)
            if (collider.contains("radius")) {
                result.SanitizedInput["radius"] = ClampFloat(
                    collider.value("radius", 0.5f), 
                    m_Limits.MinColliderExtent, m_Limits.MaxColliderExtent, 
                    "radius", result);
            }

            // Validate half height (for capsule)
            if (collider.contains("halfHeight")) {
                result.SanitizedInput["halfHeight"] = ClampFloat(
                    collider.value("halfHeight", 0.5f), 
                    m_Limits.MinColliderExtent, m_Limits.MaxColliderExtent, 
                    "halfHeight", result);
            }

            return result;
        }

        // ========================================================================
        // Entity Name Validation
        // ========================================================================

        ValidationResult ValidateEntityName(const std::string& name) const {
            ValidationResult result;

            if (name.empty()) {
                result.AddWarning("Entity name is empty, using default");
                result.SanitizedInput = "Entity";
                return result;
            }

            if (!ValidateString(name, m_Limits.MaxStringLength, "name", result)) {
                result.SanitizedInput = name.substr(0, m_Limits.MaxStringLength);
                result.IsValid = true;  // Truncation is acceptable
                result.AddWarning("Entity name truncated to max length");
            } else {
                result.SanitizedInput = name;
            }

            return result;
        }

        // ========================================================================
        // Script Validation
        // ========================================================================

        ValidationResult ValidateScript(const std::string& script) const {
            ValidationResult result;

            if (script.empty()) {
                result.AddError("Script cannot be empty");
                return result;
            }

            if (script.length() > m_Limits.MaxScriptLength) {
                result.AddError("Script exceeds max length of " + 
                               std::to_string(m_Limits.MaxScriptLength));
                return result;
            }

            // Check for potentially dangerous patterns
            static const std::vector<std::string> dangerousPatterns = {
                "os.execute",
                "os.remove",
                "os.rename",
                "io.open",
                "io.popen",
                "loadfile",
                "dofile",
                "require",
                "package",
                "debug.getinfo",
                "debug.setfenv"
            };

            std::string lowerScript = script;
            std::transform(lowerScript.begin(), lowerScript.end(), 
                          lowerScript.begin(), ::tolower);

            for (const auto& pattern : dangerousPatterns) {
                if (lowerScript.find(pattern) != std::string::npos) {
                    // SECURITY FIX: Block dangerous patterns instead of just warning
                    result.AddError("Script contains blocked pattern: " + pattern);
                    return result;  // Reject immediately - do not allow execution
                }
            }

            // Check for obfuscation attempts (Unicode homoglyphs, string concatenation tricks)
            static const std::vector<std::string> obfuscationPatterns = {
                "_g[",       // Accessing _G table via indexing
                "getfenv",   // Environment manipulation
                "setfenv",   // Environment manipulation
                "rawget",    // Raw table access
                "rawset",    // Raw table manipulation
                "getmetatable", // Metatable access
                "setmetatable", // Metatable manipulation
                "load(",     // Dynamic code loading
                "loadstring", // Dynamic string loading
                "string.dump", // Can serialize bytecode
                "pcall",     // Can suppress errors from blocked functions
                "xpcall",    // Same as pcall
                "_env",      // Lua 5.2+ environment access
                "coroutine", // Control flow attacks
                "\\x",       // Hex escape sequences for obfuscation
                "\\u"        // Unicode escape sequences for obfuscation
            };

            for (const auto& pattern : obfuscationPatterns) {
                if (lowerScript.find(pattern) != std::string::npos) {
                    result.AddError("Script contains blocked obfuscation pattern: " + pattern);
                    return result;
                }
            }

            result.SanitizedInput = script;
            return result;
        }

        // ========================================================================
        // Full Tool Input Validation
        // ========================================================================

        ValidationResult ValidateSpawnEntityInput(const Json& input) const {
            ValidationResult result;
            result.SanitizedInput = input;

            // Validate JSON depth
            if (!ValidateJSONDepth(input, m_Limits.MaxJSONDepth, 0, result)) {
                return result;
            }

            // Validate name
            if (input.contains("name")) {
                auto nameResult = ValidateEntityName(input["name"].get<std::string>());
                if (!nameResult.Errors.empty()) {
                    result.Errors.insert(result.Errors.end(), 
                                        nameResult.Errors.begin(), nameResult.Errors.end());
                }
                result.Warnings.insert(result.Warnings.end(), 
                                       nameResult.Warnings.begin(), nameResult.Warnings.end());
                result.SanitizedInput["name"] = nameResult.SanitizedInput;
            }

            // Validate transform
            if (input.contains("transform")) {
                auto transResult = ValidateTransform(input["transform"]);
                result.Errors.insert(result.Errors.end(), 
                                    transResult.Errors.begin(), transResult.Errors.end());
                result.Warnings.insert(result.Warnings.end(), 
                                       transResult.Warnings.begin(), transResult.Warnings.end());
                result.SanitizedInput["transform"] = transResult.SanitizedInput;
            }

            // Validate components
            if (input.contains("components")) {
                auto& components = input["components"];
                auto& sanitizedComps = result.SanitizedInput["components"];

                if (components.contains("light")) {
                    auto lightResult = ValidateLight(components["light"]);
                    result.Errors.insert(result.Errors.end(), 
                                        lightResult.Errors.begin(), lightResult.Errors.end());
                    result.Warnings.insert(result.Warnings.end(), 
                                           lightResult.Warnings.begin(), lightResult.Warnings.end());
                    sanitizedComps["light"] = lightResult.SanitizedInput;
                }

                if (components.contains("rigidBody")) {
                    auto rbResult = ValidateRigidBody(components["rigidBody"]);
                    result.Errors.insert(result.Errors.end(), 
                                        rbResult.Errors.begin(), rbResult.Errors.end());
                    result.Warnings.insert(result.Warnings.end(), 
                                           rbResult.Warnings.begin(), rbResult.Warnings.end());
                    sanitizedComps["rigidBody"] = rbResult.SanitizedInput;
                }

                if (components.contains("camera")) {
                    auto camResult = ValidateCamera(components["camera"]);
                    result.Errors.insert(result.Errors.end(), 
                                        camResult.Errors.begin(), camResult.Errors.end());
                    result.Warnings.insert(result.Warnings.end(), 
                                           camResult.Warnings.begin(), camResult.Warnings.end());
                    sanitizedComps["camera"] = camResult.SanitizedInput;
                }

                if (components.contains("collider")) {
                    auto colResult = ValidateCollider(components["collider"]);
                    result.Errors.insert(result.Errors.end(), 
                                        colResult.Errors.begin(), colResult.Errors.end());
                    result.Warnings.insert(result.Warnings.end(), 
                                           colResult.Warnings.begin(), colResult.Warnings.end());
                    sanitizedComps["collider"] = colResult.SanitizedInput;
                }
            }

            result.IsValid = result.Errors.empty();
            return result;
        }

        ValidationResult ValidateModifyComponentInput(const Json& input) const {
            ValidationResult result;
            result.SanitizedInput = input;

            // Validate JSON depth
            if (!ValidateJSONDepth(input, m_Limits.MaxJSONDepth, 0, result)) {
                return result;
            }

            // Same validation as spawn but applied to modification targets
            if (input.contains("transform")) {
                auto transResult = ValidateTransform(input["transform"]);
                result.Errors.insert(result.Errors.end(), 
                                    transResult.Errors.begin(), transResult.Errors.end());
                result.Warnings.insert(result.Warnings.end(), 
                                       transResult.Warnings.begin(), transResult.Warnings.end());
                result.SanitizedInput["transform"] = transResult.SanitizedInput;
            }

            if (input.contains("light")) {
                auto lightResult = ValidateLight(input["light"]);
                result.Errors.insert(result.Errors.end(), 
                                    lightResult.Errors.begin(), lightResult.Errors.end());
                result.Warnings.insert(result.Warnings.end(), 
                                       lightResult.Warnings.begin(), lightResult.Warnings.end());
                result.SanitizedInput["light"] = lightResult.SanitizedInput;
            }

            if (input.contains("rigidBody")) {
                auto rbResult = ValidateRigidBody(input["rigidBody"]);
                result.Errors.insert(result.Errors.end(), 
                                    rbResult.Errors.begin(), rbResult.Errors.end());
                result.Warnings.insert(result.Warnings.end(), 
                                       rbResult.Warnings.begin(), rbResult.Warnings.end());
                result.SanitizedInput["rigidBody"] = rbResult.SanitizedInput;
            }

            if (input.contains("camera")) {
                auto camResult = ValidateCamera(input["camera"]);
                result.Errors.insert(result.Errors.end(), 
                                    camResult.Errors.begin(), camResult.Errors.end());
                result.Warnings.insert(result.Warnings.end(), 
                                       camResult.Warnings.begin(), camResult.Warnings.end());
                result.SanitizedInput["camera"] = camResult.SanitizedInput;
            }

            if (input.contains("collider")) {
                auto colResult = ValidateCollider(input["collider"]);
                result.Errors.insert(result.Errors.end(), 
                                    colResult.Errors.begin(), colResult.Errors.end());
                result.Warnings.insert(result.Warnings.end(), 
                                       colResult.Warnings.begin(), colResult.Warnings.end());
                result.SanitizedInput["collider"] = colResult.SanitizedInput;
            }

            result.IsValid = result.Errors.empty();
            return result;
        }

        ValidationResult ValidateExecuteScriptInput(const Json& input) const {
            ValidationResult result;
            result.SanitizedInput = input;

            if (!input.contains("script")) {
                result.AddError("Missing required 'script' parameter");
                return result;
            }

            auto scriptResult = ValidateScript(input["script"].get<std::string>());
            result.Errors.insert(result.Errors.end(), 
                                scriptResult.Errors.begin(), scriptResult.Errors.end());
            result.Warnings.insert(result.Warnings.end(), 
                                   scriptResult.Warnings.begin(), scriptResult.Warnings.end());

            // Validate timeout
            if (input.contains("timeout")) {
                result.SanitizedInput["timeout"] = ClampFloat(
                    input.value("timeout", 1000.0f), 100.0f, 5000.0f, "timeout", result);
            }

            // Validate max instructions
            if (input.contains("maxInstructions")) {
                int maxInst = input.value("maxInstructions", 100000);
                maxInst = std::clamp(maxInst, 1000, 1000000);
                result.SanitizedInput["maxInstructions"] = maxInst;
            }

            result.IsValid = result.Errors.empty();
            return result;
        }

        // ========================================================================
        // Logging Helpers
        // ========================================================================

        void LogValidationResult(const ValidationResult& result, const std::string& operation) const {
            if (!result.IsValid) {
                ENGINE_CORE_WARN("ActionValidator: {} validation FAILED", operation);
                for (const auto& error : result.Errors) {
                    ENGINE_CORE_WARN("  Error: {}", error);
                }
            }
            for (const auto& warning : result.Warnings) {
                ENGINE_CORE_WARN("ActionValidator: {} - {}", operation, warning);
            }
        }

    private:
        ValidationLimits m_Limits;
    };

    // ============================================================================
    // Global Validator Instance
    // ============================================================================

    inline ActionValidator& GetGlobalValidator() {
        static ActionValidator s_Validator;
        return s_Validator;
    }

} // namespace MCP
} // namespace Core
