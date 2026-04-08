#pragma once

/**
 * @file ConstraintSerialization.h
 * @brief JSON serialization support for physics constraint components.
 *
 * Provides comprehensive serialization and deserialization functionality for
 * constraint components using the nlohmann::json library. This enables:
 * - Saving and loading constraint configurations to/from files
 * - Network transmission of constraint data
 * - Editor integration for constraint property persistence
 * - Scene serialization with physics constraints
 *
 * ## Design Principles
 * - **Separation of Concerns**: Serialization logic is decoupled from component definitions
 * - **Validation**: All deserialization includes validation with meaningful error handling
 * - **Versioning Support**: JSON keys are explicit to support future schema evolution
 * - **Round-trip Safety**: Serialization followed by deserialization preserves all data
 *
 * ## Error Handling
 * The FromJson functions return bool to indicate success/failure and populate
 * components only on successful validation. This prevents partial state corruption.
 *
 * @note Requires nlohmann/json.hpp to be available in the include path.
 *
 * @see HingeConstraintComponent
 * @see SliderConstraintComponent
 * @see SpringConstraintComponent
 */

#include "Core/ECS/Components/HingeConstraintComponent.h"
#include "Core/ECS/Components/SliderConstraintComponent.h"
#include "Core/ECS/Components/SpringConstraintComponent.h"
#include "Core/Physics/Constraints/ConstraintTypes.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <stdexcept>
#include <string>

namespace Core::Physics::Serialization
{

// =============================================================================
// JSON Schema Version
// =============================================================================

/**
 * @brief Current schema version for constraint serialization.
 *
 * Increment this when making breaking changes to the serialization format.
 * Deserialization can check this to handle backward compatibility.
 */
inline constexpr int CONSTRAINT_SCHEMA_VERSION = 1;

// =============================================================================
// JSON Key Constants
// =============================================================================

/**
 * @brief Namespace containing JSON key constants for type-safe serialization.
 *
 * Using constants prevents typos and enables IDE support for key names.
 */
namespace Keys
{
    // Schema metadata
    inline constexpr const char* SchemaVersion = "schemaVersion";
    inline constexpr const char* ConstraintType = "constraintType";

    // ConstraintBase keys
    inline constexpr const char* BodyA = "bodyA";
    inline constexpr const char* BodyB = "bodyB";
    inline constexpr const char* PivotA = "pivotA";
    inline constexpr const char* PivotB = "pivotB";
    inline constexpr const char* IsEnabled = "isEnabled";
    inline constexpr const char* IsBreakable = "isBreakable";
    inline constexpr const char* BreakForce = "breakForce";
    inline constexpr const char* BreakTorque = "breakTorque";
    inline constexpr const char* IsBroken = "isBroken";

    // Vec3 keys
    inline constexpr const char* X = "x";
    inline constexpr const char* Y = "y";
    inline constexpr const char* Z = "z";

    // Hinge constraint keys
    inline constexpr const char* HingeAxis = "hingeAxis";
    inline constexpr const char* NormalAxis = "normalAxis";
    inline constexpr const char* HasLimits = "hasLimits";
    inline constexpr const char* MinAngle = "minAngle";
    inline constexpr const char* MaxAngle = "maxAngle";
    inline constexpr const char* LimitSpring = "limitSpring";
    inline constexpr const char* LimitDamping = "limitDamping";
    inline constexpr const char* MotorEnabled = "motorEnabled";
    inline constexpr const char* MotorTargetVelocity = "motorTargetVelocity";
    inline constexpr const char* MotorMaxTorque = "motorMaxTorque";
    inline constexpr const char* CurrentAngle = "currentAngle";

    // Slider constraint keys
    inline constexpr const char* SliderAxis = "sliderAxis";
    inline constexpr const char* MinPosition = "minPosition";
    inline constexpr const char* MaxPosition = "maxPosition";
    inline constexpr const char* MotorMaxForce = "motorMaxForce";
    inline constexpr const char* PositionMotorEnabled = "positionMotorEnabled";
    inline constexpr const char* TargetPosition = "targetPosition";
    inline constexpr const char* PositionSpring = "positionSpring";
    inline constexpr const char* PositionDamping = "positionDamping";
    inline constexpr const char* CurrentPosition = "currentPosition";

    // Spring constraint keys
    inline constexpr const char* RestLength = "restLength";
    inline constexpr const char* MinLength = "minLength";
    inline constexpr const char* MaxLength = "maxLength";
    inline constexpr const char* Stiffness = "stiffness";
    inline constexpr const char* Damping = "damping";
    inline constexpr const char* UseFrequencyDamping = "useFrequencyDamping";
    inline constexpr const char* Frequency = "frequency";
    inline constexpr const char* DampingRatio = "dampingRatio";
    inline constexpr const char* CurrentLength = "currentLength";
    inline constexpr const char* CurrentForce = "currentForce";

} // namespace Keys

// =============================================================================
// Validation Error Information
// =============================================================================

/**
 * @brief Structure containing validation error details.
 *
 * Provides contextual information when deserialization fails,
 * enabling meaningful error messages for debugging.
 */
struct ValidationError
{
    std::string field;       ///< The field that failed validation
    std::string message;     ///< Human-readable error description
    std::string expected;    ///< Expected value or type (optional)
    std::string actual;      ///< Actual value received (optional)
};

// =============================================================================
// Vector Serialization
// =============================================================================

/**
 * @brief Serializes a Math::Vec3 to JSON format.
 *
 * Produces a JSON object with x, y, z components for maximum readability
 * and compatibility with external tools.
 *
 * @param vec The vector to serialize.
 * @return JSON object containing the vector components.
 *
 * @example
 * @code
 * Math::Vec3 position{1.0f, 2.0f, 3.0f};
 * auto json = SerializeVec3(position);
 * // Result: {"x": 1.0, "y": 2.0, "z": 3.0}
 * @endcode
 */
[[nodiscard]] inline nlohmann::json SerializeVec3(const Math::Vec3& vec)
{
    return nlohmann::json{
        {Keys::X, vec.x},
        {Keys::Y, vec.y},
        {Keys::Z, vec.z}
    };
}

/**
 * @brief Deserializes a Math::Vec3 from JSON format.
 *
 * Supports both object format {"x": 1, "y": 2, "z": 3} and array format [1, 2, 3]
 * for flexibility in handwritten JSON files.
 *
 * @param json The JSON value to deserialize.
 * @return The deserialized vector.
 *
 * @throws std::invalid_argument if the JSON format is invalid or missing components.
 *
 * @example
 * @code
 * auto json = R"({"x": 1.0, "y": 2.0, "z": 3.0})"_json;
 * Math::Vec3 vec = DeserializeVec3(json);
 * @endcode
 */
[[nodiscard]] inline Math::Vec3 DeserializeVec3(const nlohmann::json& json)
{
    // Support array format [x, y, z]
    if (json.is_array())
    {
        if (json.size() < 3)
        {
            throw std::invalid_argument(
                "Vec3 array must have at least 3 elements, got " + 
                std::to_string(json.size()));
        }
        return Math::Vec3{
            json[0].get<float>(),
            json[1].get<float>(),
            json[2].get<float>()
        };
    }

    // Support object format {x, y, z}
    if (json.is_object())
    {
        if (!json.contains(Keys::X) || !json.contains(Keys::Y) || !json.contains(Keys::Z))
        {
            throw std::invalid_argument(
                "Vec3 object must contain 'x', 'y', and 'z' fields");
        }
        return Math::Vec3{
            json[Keys::X].get<float>(),
            json[Keys::Y].get<float>(),
            json[Keys::Z].get<float>()
        };
    }

    throw std::invalid_argument(
        "Vec3 must be an object or array, got " + 
        std::string(json.type_name()));
}

/**
 * @brief Safely deserializes a Vec3 with default fallback.
 *
 * Non-throwing version that returns a default value if deserialization fails.
 *
 * @param json The JSON value to deserialize.
 * @param defaultValue Value to return on failure.
 * @return The deserialized vector or default value.
 */
[[nodiscard]] inline Math::Vec3 DeserializeVec3Safe(
    const nlohmann::json& json,
    const Math::Vec3& defaultValue = Math::Vec3{0.0f, 0.0f, 0.0f}) noexcept
{
    try
    {
        return DeserializeVec3(json);
    }
    catch (const std::exception&)
    {
        return defaultValue;
    }
}

// =============================================================================
// ConstraintBase Serialization
// =============================================================================

/**
 * @brief Serializes the common ConstraintBase fields to JSON.
 *
 * This function handles the base properties shared by all constraint types:
 * body references, pivot points, enabled state, and breakage configuration.
 *
 * @param base The ConstraintBase to serialize.
 * @return JSON object containing the base constraint fields.
 *
 * @note Entity IDs are serialized as integers. The actual entity mapping must be
 *       handled by the caller during scene serialization/deserialization.
 *
 * @note JoltConstraint pointer and NeedsSync flag are not serialized as they
 *       are runtime state that should be reconstructed.
 */
[[nodiscard]] inline nlohmann::json SerializeConstraintBase(const ConstraintBase& base)
{
    nlohmann::json json;

    // Entity references (serialized as raw integer values)
    // entt::null serializes as a specific value that can be detected on load
    json[Keys::BodyA] = static_cast<std::uint32_t>(base.BodyA);
    json[Keys::BodyB] = static_cast<std::uint32_t>(base.BodyB);

    // Pivot points
    json[Keys::PivotA] = SerializeVec3(base.PivotA);
    json[Keys::PivotB] = SerializeVec3(base.PivotB);

    // State flags
    json[Keys::IsEnabled] = base.IsEnabled;
    json[Keys::IsBreakable] = base.IsBreakable;
    json[Keys::IsBroken] = base.IsBroken;

    // Breakage thresholds
    json[Keys::BreakForce] = base.BreakForce;
    json[Keys::BreakTorque] = base.BreakTorque;

    return json;
}

/**
 * @brief Deserializes common ConstraintBase fields from JSON into a component.
 *
 * Populates the base constraint fields from JSON data. Performs validation
 * to ensure data integrity.
 *
 * @param json The JSON object containing constraint base data.
 * @param[out] base The ConstraintBase to populate.
 *
 * @throws std::invalid_argument if required fields are missing or invalid.
 *
 * @note Entity references are stored as integers and must be remapped to actual
 *       entities by the caller's scene loading logic.
 */
inline void DeserializeConstraintBase(const nlohmann::json& json, ConstraintBase& base)
{
    // Entity references
    if (json.contains(Keys::BodyA))
    {
        auto rawId = json[Keys::BodyA].get<std::uint32_t>();
        base.BodyA = static_cast<entt::entity>(rawId);
    }
    else
    {
        base.BodyA = entt::null;
    }

    if (json.contains(Keys::BodyB))
    {
        auto rawId = json[Keys::BodyB].get<std::uint32_t>();
        base.BodyB = static_cast<entt::entity>(rawId);
    }
    else
    {
        base.BodyB = entt::null;
    }

    // Pivot points
    if (json.contains(Keys::PivotA))
    {
        base.PivotA = DeserializeVec3(json[Keys::PivotA]);
    }

    if (json.contains(Keys::PivotB))
    {
        base.PivotB = DeserializeVec3(json[Keys::PivotB]);
    }

    // State flags with defaults
    base.IsEnabled = json.value(Keys::IsEnabled, true);
    base.IsBreakable = json.value(Keys::IsBreakable, false);
    base.IsBroken = json.value(Keys::IsBroken, false);

    // Breakage thresholds with defaults
    base.BreakForce = json.value(Keys::BreakForce, FLT_MAX);
    base.BreakTorque = json.value(Keys::BreakTorque, FLT_MAX);

    // Validate breakage configuration
    if (base.IsBreakable)
    {
        if (base.BreakForce <= 0.0f)
        {
            throw std::invalid_argument(
                "BreakForce must be positive for breakable constraints");
        }
        if (base.BreakTorque <= 0.0f)
        {
            throw std::invalid_argument(
                "BreakTorque must be positive for breakable constraints");
        }
    }

    // Reset runtime state
    base.JoltConstraint = nullptr;
    base.NeedsSync = true;
}

// =============================================================================
// HingeConstraintComponent Serialization
// =============================================================================

/**
 * @brief Serializes a HingeConstraintComponent to JSON format.
 *
 * Produces a complete JSON representation of the hinge constraint including
 * all base properties, axis configuration, limits, and motor settings.
 *
 * @param component The hinge constraint component to serialize.
 * @return Complete JSON representation of the constraint.
 *
 * @example
 * @code
 * auto& hinge = registry.get<HingeConstraintComponent>(entity);
 * nlohmann::json json = ToJson(hinge);
 * std::ofstream file("hinge.json");
 * file << json.dump(2);  // Pretty-print with 2-space indent
 * @endcode
 */
[[nodiscard]] inline nlohmann::json ToJson(const ECS::HingeConstraintComponent& component)
{
    nlohmann::json json = SerializeConstraintBase(component);

    // Schema metadata
    json[Keys::SchemaVersion] = CONSTRAINT_SCHEMA_VERSION;
    json[Keys::ConstraintType] = "hinge";

    // Axis configuration
    json[Keys::HingeAxis] = SerializeVec3(component.HingeAxis);
    json[Keys::NormalAxis] = SerializeVec3(component.NormalAxis);

    // Angular limits
    json[Keys::HasLimits] = component.HasLimits;
    json[Keys::MinAngle] = component.MinAngle;
    json[Keys::MaxAngle] = component.MaxAngle;
    json[Keys::LimitSpring] = component.LimitSpring;
    json[Keys::LimitDamping] = component.LimitDamping;

    // Motor configuration
    json[Keys::MotorEnabled] = component.MotorEnabled;
    json[Keys::MotorTargetVelocity] = component.MotorTargetVelocity;
    json[Keys::MotorMaxTorque] = component.MotorMaxTorque;

    // Runtime state (included for completeness, typically ignored on load)
    json[Keys::CurrentAngle] = component.CurrentAngle;

    return json;
}

/**
 * @brief Deserializes a HingeConstraintComponent from JSON format.
 *
 * Validates the JSON data and populates the component. Returns false if
 * validation fails, leaving the component in an undefined state.
 *
 * @param json The JSON object containing hinge constraint data.
 * @param[out] component The component to populate on success.
 * @return True if deserialization succeeded, false on validation failure.
 *
 * @note The component is only modified if validation succeeds. On failure,
 *       the component state is undefined and should not be used.
 *
 * @example
 * @code
 * ECS::HingeConstraintComponent hinge;
 * if (FromJson(json, hinge))
 * {
 *     registry.emplace<HingeConstraintComponent>(entity, hinge);
 * }
 * else
 * {
 *     LOG_ERROR("Failed to load hinge constraint");
 * }
 * @endcode
 */
[[nodiscard]] inline bool FromJson(
    const nlohmann::json& json,
    ECS::HingeConstraintComponent& component)
{
    try
    {
        // Validate constraint type if present
        if (json.contains(Keys::ConstraintType))
        {
            auto type = json[Keys::ConstraintType].get<std::string>();
            if (type != "hinge")
            {
                return false;  // Wrong constraint type
            }
        }

        // Deserialize base properties
        DeserializeConstraintBase(json, component);

        // Axis configuration
        if (json.contains(Keys::HingeAxis))
        {
            component.HingeAxis = DeserializeVec3(json[Keys::HingeAxis]);

            // Validate axis is normalized (with tolerance)
            float length = glm::length(component.HingeAxis);
            if (length < 0.99f || length > 1.01f)
            {
                // Auto-normalize if close, otherwise reject
                if (length > 0.001f)
                {
                    component.HingeAxis = glm::normalize(component.HingeAxis);
                }
                else
                {
                    return false;  // Zero-length axis is invalid
                }
            }
        }

        if (json.contains(Keys::NormalAxis))
        {
            component.NormalAxis = DeserializeVec3(json[Keys::NormalAxis]);

            float length = glm::length(component.NormalAxis);
            if (length < 0.99f || length > 1.01f)
            {
                if (length > 0.001f)
                {
                    component.NormalAxis = glm::normalize(component.NormalAxis);
                }
                else
                {
                    return false;
                }
            }
        }

        // Angular limits
        component.HasLimits = json.value(Keys::HasLimits, false);
        component.MinAngle = json.value(Keys::MinAngle, -ECS::PI);
        component.MaxAngle = json.value(Keys::MaxAngle, ECS::PI);
        component.LimitSpring = json.value(Keys::LimitSpring, 0.0f);
        component.LimitDamping = json.value(Keys::LimitDamping, 0.0f);

        // Validate limit configuration
        if (component.HasLimits)
        {
            if (component.MinAngle > component.MaxAngle)
            {
                return false;  // Invalid limit range
            }
            if (component.LimitSpring < 0.0f || component.LimitDamping < 0.0f)
            {
                return false;  // Negative spring/damping invalid
            }
        }

        // Motor configuration
        component.MotorEnabled = json.value(Keys::MotorEnabled, false);
        component.MotorTargetVelocity = json.value(Keys::MotorTargetVelocity, 0.0f);
        component.MotorMaxTorque = json.value(Keys::MotorMaxTorque, 1000.0f);

        // Validate motor configuration
        if (component.MotorMaxTorque < 0.0f)
        {
            return false;  // Negative torque limit invalid
        }

        // Runtime state (optional, typically recomputed)
        component.CurrentAngle = json.value(Keys::CurrentAngle, 0.0f);

        // Mark for sync with physics engine
        component.NeedsSync = true;

        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

// =============================================================================
// SliderConstraintComponent Serialization
// =============================================================================

/**
 * @brief Serializes a SliderConstraintComponent to JSON format.
 *
 * Produces a complete JSON representation of the slider constraint including
 * all base properties, axis configuration, limits, velocity motor, and
 * position motor settings.
 *
 * @param component The slider constraint component to serialize.
 * @return Complete JSON representation of the constraint.
 *
 * @example
 * @code
 * auto& slider = registry.get<SliderConstraintComponent>(entity);
 * auto json = ToJson(slider);
 * @endcode
 */
[[nodiscard]] inline nlohmann::json ToJson(const ECS::SliderConstraintComponent& component)
{
    nlohmann::json json = SerializeConstraintBase(component);

    // Schema metadata
    json[Keys::SchemaVersion] = CONSTRAINT_SCHEMA_VERSION;
    json[Keys::ConstraintType] = "slider";

    // Axis configuration
    json[Keys::SliderAxis] = SerializeVec3(component.SliderAxis);
    json[Keys::NormalAxis] = SerializeVec3(component.NormalAxis);

    // Linear limits
    json[Keys::HasLimits] = component.HasLimits;
    json[Keys::MinPosition] = component.MinPosition;
    json[Keys::MaxPosition] = component.MaxPosition;
    json[Keys::LimitSpring] = component.LimitSpring;
    json[Keys::LimitDamping] = component.LimitDamping;

    // Velocity motor configuration
    json[Keys::MotorEnabled] = component.MotorEnabled;
    json[Keys::MotorTargetVelocity] = component.MotorTargetVelocity;
    json[Keys::MotorMaxForce] = component.MotorMaxForce;

    // Position motor configuration
    json[Keys::PositionMotorEnabled] = component.PositionMotorEnabled;
    json[Keys::TargetPosition] = component.TargetPosition;
    json[Keys::PositionSpring] = component.PositionSpring;
    json[Keys::PositionDamping] = component.PositionDamping;

    // Runtime state
    json[Keys::CurrentPosition] = component.CurrentPosition;

    return json;
}

/**
 * @brief Deserializes a SliderConstraintComponent from JSON format.
 *
 * Validates the JSON data and populates the component. Handles both
 * velocity and position motor configurations.
 *
 * @param json The JSON object containing slider constraint data.
 * @param[out] component The component to populate on success.
 * @return True if deserialization succeeded, false on validation failure.
 */
[[nodiscard]] inline bool FromJson(
    const nlohmann::json& json,
    ECS::SliderConstraintComponent& component)
{
    try
    {
        // Validate constraint type if present
        if (json.contains(Keys::ConstraintType))
        {
            auto type = json[Keys::ConstraintType].get<std::string>();
            if (type != "slider")
            {
                return false;
            }
        }

        // Deserialize base properties
        DeserializeConstraintBase(json, component);

        // Axis configuration
        if (json.contains(Keys::SliderAxis))
        {
            component.SliderAxis = DeserializeVec3(json[Keys::SliderAxis]);

            float length = glm::length(component.SliderAxis);
            if (length < 0.99f || length > 1.01f)
            {
                if (length > 0.001f)
                {
                    component.SliderAxis = glm::normalize(component.SliderAxis);
                }
                else
                {
                    return false;
                }
            }
        }

        if (json.contains(Keys::NormalAxis))
        {
            component.NormalAxis = DeserializeVec3(json[Keys::NormalAxis]);

            float length = glm::length(component.NormalAxis);
            if (length < 0.99f || length > 1.01f)
            {
                if (length > 0.001f)
                {
                    component.NormalAxis = glm::normalize(component.NormalAxis);
                }
                else
                {
                    return false;
                }
            }
        }

        // Linear limits
        component.HasLimits = json.value(Keys::HasLimits, false);
        component.MinPosition = json.value(Keys::MinPosition, -1.0f);
        component.MaxPosition = json.value(Keys::MaxPosition, 1.0f);
        component.LimitSpring = json.value(Keys::LimitSpring, 0.0f);
        component.LimitDamping = json.value(Keys::LimitDamping, 0.0f);

        // Validate limit configuration
        if (component.HasLimits)
        {
            if (component.MinPosition > component.MaxPosition)
            {
                return false;
            }
            if (component.LimitSpring < 0.0f || component.LimitDamping < 0.0f)
            {
                return false;
            }
        }

        // Velocity motor configuration
        component.MotorEnabled = json.value(Keys::MotorEnabled, false);
        component.MotorTargetVelocity = json.value(Keys::MotorTargetVelocity, 0.0f);
        component.MotorMaxForce = json.value(Keys::MotorMaxForce, 10000.0f);

        if (component.MotorMaxForce < 0.0f)
        {
            return false;
        }

        // Position motor configuration
        component.PositionMotorEnabled = json.value(Keys::PositionMotorEnabled, false);
        component.TargetPosition = json.value(Keys::TargetPosition, 0.0f);
        component.PositionSpring = json.value(Keys::PositionSpring, 1000.0f);
        component.PositionDamping = json.value(Keys::PositionDamping, 100.0f);

        if (component.PositionSpring < 0.0f || component.PositionDamping < 0.0f)
        {
            return false;
        }

        // Runtime state
        component.CurrentPosition = json.value(Keys::CurrentPosition, 0.0f);

        component.NeedsSync = true;

        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

// =============================================================================
// SpringConstraintComponent Serialization
// =============================================================================

/**
 * @brief Serializes a SpringConstraintComponent to JSON format.
 *
 * Produces a complete JSON representation of the spring constraint including
 * all base properties, length configuration, and both traditional and
 * frequency-based spring parameters.
 *
 * @param component The spring constraint component to serialize.
 * @return Complete JSON representation of the constraint.
 *
 * @example
 * @code
 * auto& spring = registry.get<SpringConstraintComponent>(entity);
 * auto json = ToJson(spring);
 * @endcode
 */
[[nodiscard]] inline nlohmann::json ToJson(const ECS::SpringConstraintComponent& component)
{
    nlohmann::json json = SerializeConstraintBase(component);

    // Schema metadata
    json[Keys::SchemaVersion] = CONSTRAINT_SCHEMA_VERSION;
    json[Keys::ConstraintType] = "spring";

    // Length configuration
    json[Keys::RestLength] = component.RestLength;
    json[Keys::MinLength] = component.MinLength;
    json[Keys::MaxLength] = component.MaxLength;

    // Traditional spring parameters
    json[Keys::Stiffness] = component.Stiffness;
    json[Keys::Damping] = component.Damping;

    // Frequency-based parameters
    json[Keys::UseFrequencyDamping] = component.UseFrequencyDamping;
    json[Keys::Frequency] = component.Frequency;
    json[Keys::DampingRatio] = component.DampingRatio;

    // Runtime state
    json[Keys::CurrentLength] = component.CurrentLength;
    json[Keys::CurrentForce] = component.CurrentForce;

    return json;
}

/**
 * @brief Deserializes a SpringConstraintComponent from JSON format.
 *
 * Validates the JSON data and populates the component. Handles both
 * traditional stiffness/damping and frequency-based parameterization.
 *
 * @param json The JSON object containing spring constraint data.
 * @param[out] component The component to populate on success.
 * @return True if deserialization succeeded, false on validation failure.
 */
[[nodiscard]] inline bool FromJson(
    const nlohmann::json& json,
    ECS::SpringConstraintComponent& component)
{
    try
    {
        // Validate constraint type if present
        if (json.contains(Keys::ConstraintType))
        {
            auto type = json[Keys::ConstraintType].get<std::string>();
            if (type != "spring")
            {
                return false;
            }
        }

        // Deserialize base properties
        DeserializeConstraintBase(json, component);

        // Length configuration
        component.RestLength = json.value(Keys::RestLength, 1.0f);
        component.MinLength = json.value(Keys::MinLength, 0.0f);
        component.MaxLength = json.value(Keys::MaxLength, FLT_MAX);

        // Validate length configuration
        if (component.RestLength <= 0.0f)
        {
            return false;  // Rest length must be positive
        }
        if (component.MinLength < 0.0f)
        {
            return false;  // Min length cannot be negative
        }
        if (component.MinLength > component.RestLength)
        {
            return false;  // Min length cannot exceed rest length
        }
        if (component.MaxLength < component.RestLength)
        {
            return false;  // Max length cannot be less than rest length
        }

        // Traditional spring parameters
        component.Stiffness = json.value(Keys::Stiffness, 1000.0f);
        component.Damping = json.value(Keys::Damping, 100.0f);

        if (component.Stiffness < 0.0f || component.Damping < 0.0f)
        {
            return false;  // Negative stiffness/damping invalid
        }

        // Frequency-based parameters
        component.UseFrequencyDamping = json.value(Keys::UseFrequencyDamping, false);
        component.Frequency = json.value(Keys::Frequency, 1.0f);
        component.DampingRatio = json.value(Keys::DampingRatio, 0.5f);

        // Validate frequency-based parameters
        if (component.UseFrequencyDamping)
        {
            if (component.Frequency <= 0.0f)
            {
                return false;  // Frequency must be positive
            }
            if (component.DampingRatio < 0.0f)
            {
                return false;  // Damping ratio cannot be negative
            }
        }

        // Runtime state
        component.CurrentLength = json.value(Keys::CurrentLength, component.RestLength);
        component.CurrentForce = json.value(Keys::CurrentForce, 0.0f);

        component.NeedsSync = true;

        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

// =============================================================================
// Extended Validation Utilities
// =============================================================================

/**
 * @brief Validates JSON against the constraint schema without deserializing.
 *
 * Performs structural validation to check if the JSON could be deserialized
 * as a constraint. Useful for pre-validation before loading.
 *
 * @param json The JSON to validate.
 * @param[out] error Optional output for detailed error information.
 * @return True if the JSON appears to be a valid constraint format.
 */
[[nodiscard]] inline bool ValidateConstraintJson(
    const nlohmann::json& json,
    std::optional<ValidationError>* error = nullptr)
{
    auto setError = [error](const std::string& field, const std::string& message)
    {
        if (error)
        {
            *error = ValidationError{field, message, "", ""};
        }
    };

    // Must be an object
    if (!json.is_object())
    {
        setError("root", "Constraint JSON must be an object");
        return false;
    }

    // Check for constraint type
    if (json.contains(Keys::ConstraintType))
    {
        if (!json[Keys::ConstraintType].is_string())
        {
            setError(Keys::ConstraintType, "constraintType must be a string");
            return false;
        }

        auto type = json[Keys::ConstraintType].get<std::string>();
        if (type != "hinge" && type != "slider" && type != "spring")
        {
            setError(Keys::ConstraintType, 
                "Unknown constraint type: " + type + 
                " (expected: hinge, slider, or spring)");
            return false;
        }
    }

    // Validate pivot points if present
    for (const auto& key : {Keys::PivotA, Keys::PivotB, Keys::HingeAxis, 
                            Keys::NormalAxis, Keys::SliderAxis})
    {
        if (json.contains(key))
        {
            const auto& val = json[key];
            if (!val.is_object() && !val.is_array())
            {
                setError(key, std::string(key) + " must be an object or array");
                return false;
            }
        }
    }

    return true;
}

/**
 * @brief Determines the constraint type from JSON without full deserialization.
 *
 * @param json The JSON to inspect.
 * @return The constraint type if determinable, or std::nullopt.
 */
[[nodiscard]] inline std::optional<ConstraintType> GetConstraintTypeFromJson(
    const nlohmann::json& json)
{
    if (!json.contains(Keys::ConstraintType))
    {
        return std::nullopt;
    }

    auto typeStr = json[Keys::ConstraintType].get<std::string>();

    if (typeStr == "hinge") return ConstraintType::Hinge;
    if (typeStr == "slider") return ConstraintType::Slider;
    if (typeStr == "spring") return ConstraintType::Spring;

    return std::nullopt;
}

} // namespace Core::Physics::Serialization
