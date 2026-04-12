#pragma once

// Scene Serialization for MCP
// Defines JSON schemas and serialization functions for EnTT Registry and Game Objects
// Used by MCP tools to communicate scene state to AI agents

#include "JsonSerialization.h"
#include "Core/Math/Math.h"
#include "Core/ECS/Scene.h"
#include "Core/ECS/Entity.h"
#include "Core/ECS/Components/Components.h"
#include "Core/UI/Anchoring.h"

#include <entt/entt.hpp>
#include <string>
#include <vector>
#include <optional>

namespace Core {
namespace MCP {

    // ============================================================================
    // JSON Schema Definitions (JSON Schema Draft-07 compatible)
    // ============================================================================

    namespace Schema {

        // Vec3 schema
        inline Json Vec3Schema() {
            return {
                {"type", "object"},
                {"description", "3D vector with x, y, z components"},
                {"properties", {
                    {"x", {{"type", "number"}, {"description", "X component"}}},
                    {"y", {{"type", "number"}, {"description", "Y component"}}},
                    {"z", {{"type", "number"}, {"description", "Z component"}}}
                }},
                {"required", Json::array({"x", "y", "z"})}
            };
        }

        // Vec4 / Quaternion schema
        inline Json Vec4Schema() {
            return {
                {"type", "object"},
                {"description", "4D vector or quaternion with x, y, z, w components"},
                {"properties", {
                    {"x", {{"type", "number"}}},
                    {"y", {{"type", "number"}}},
                    {"z", {{"type", "number"}}},
                    {"w", {{"type", "number"}}}
                }},
                {"required", Json::array({"x", "y", "z", "w"})}
            };
        }

        // Color schema (RGB)
        inline Json ColorSchema() {
            return {
                {"type", "object"},
                {"description", "RGB color with values 0.0-1.0"},
                {"properties", {
                    {"r", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}}},
                    {"g", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}}},
                    {"b", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}}}
                }},
                {"required", Json::array({"r", "g", "b"})}
            };
        }

        // TransformComponent schema
        inline Json TransformSchema() {
            return {
                {"type", "object"},
                {"description", "Transform component with position, rotation, and scale"},
                {"properties", {
                    {"position", Vec3Schema()},
                    {"rotation", {
                        {"type", "object"},
                        {"description", "Euler angles in degrees (pitch, yaw, roll)"},
                        {"properties", {
                            {"pitch", {{"type", "number"}, {"description", "Rotation around X axis"}}},
                            {"yaw", {{"type", "number"}, {"description", "Rotation around Y axis"}}},
                            {"roll", {{"type", "number"}, {"description", "Rotation around Z axis"}}}
                        }}
                    }},
                    {"scale", Vec3Schema()}
                }}
            };
        }

        // LightComponent schema
        inline Json LightSchema() {
            return {
                {"type", "object"},
                {"description", "Light component for scene illumination"},
                {"properties", {
                    {"type", {
                        {"type", "string"},
                        {"enum", Json::array({"directional", "point", "spot"})},
                        {"description", "Type of light source"}
                    }},
                    {"color", ColorSchema()},
                    {"intensity", {{"type", "number"}, {"minimum", 0.0}, {"description", "Light intensity multiplier"}}},
                    {"radius", {{"type", "number"}, {"minimum", 0.0}, {"description", "Attenuation radius for point/spot lights"}}},
                    {"innerCutoff", {{"type", "number"}, {"description", "Spot light inner cone angle in degrees"}}},
                    {"outerCutoff", {{"type", "number"}, {"description", "Spot light outer cone angle in degrees"}}},
                    {"castShadows", {{"type", "boolean"}}},
                    {"enabled", {{"type", "boolean"}}}
                }},
                {"required", Json::array({"type", "color", "intensity"})}
            };
        }

        // MeshComponent schema
        inline Json MeshSchema() {
            return {
                {"type", "object"},
                {"description", "Mesh component for 3D geometry rendering"},
                {"properties", {
                    {"meshPath", {{"type", "string"}, {"description", "Path to mesh asset file"}}},
                    {"materialIndex", {{"type", "integer"}, {"minimum", 0}}},
                    {"visible", {{"type", "boolean"}}},
                    {"castShadows", {{"type", "boolean"}}},
                    {"receiveShadows", {{"type", "boolean"}}}
                }}
            };
        }

        // CameraComponent schema
        inline Json CameraSchema() {
            return {
                {"type", "object"},
                {"description", "Camera component for viewing the scene"},
                {"properties", {
                    {"projection", {
                        {"type", "string"},
                        {"enum", Json::array({"perspective", "orthographic"})}
                    }},
                    {"fieldOfView", {{"type", "number"}, {"minimum", 1.0}, {"maximum", 179.0}, {"description", "Vertical FOV in degrees (perspective only)"}}},
                    {"nearPlane", {{"type", "number"}, {"minimum", 0.001}}},
                    {"farPlane", {{"type", "number"}}},
                    {"orthoSize", {{"type", "number"}, {"description", "Half-height of orthographic view"}}},
                    {"isActive", {{"type", "boolean"}}}
                }}
            };
        }

        // ColliderComponent schema
        inline Json ColliderSchema() {
            return {
                {"type", "object"},
                {"description", "Physics collider component"},
                {"properties", {
                    {"type", {
                        {"type", "string"},
                        {"enum", Json::array({"box", "sphere", "capsule", "mesh"})}
                    }},
                    {"halfExtents", Vec3Schema()},       // For box
                    {"radius", {{"type", "number"}}},    // For sphere/capsule
                    {"halfHeight", {{"type", "number"}}},// For capsule
                    {"offset", Vec3Schema()},
                    {"friction", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}}},
                    {"restitution", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}}},
                    {"isSensor", {{"type", "boolean"}}}
                }}
            };
        }

        // RigidBodyComponent schema
        inline Json RigidBodySchema() {
            return {
                {"type", "object"},
                {"description", "Physics rigid body component"},
                {"properties", {
                    {"motionType", {
                        {"type", "string"},
                        {"enum", Json::array({"static", "kinematic", "dynamic"})}
                    }},
                    {"mass", {{"type", "number"}, {"minimum", 0.0}}},
                    {"linearDamping", {{"type", "number"}, {"minimum", 0.0}}},
                    {"angularDamping", {{"type", "number"}, {"minimum", 0.0}}},
                    {"linearVelocity", Vec3Schema()},
                    {"angularVelocity", Vec3Schema()},
                    {"gravityEnabled", {{"type", "boolean"}}},
                    {"useCCD", {{"type", "boolean"}, {"description", "Continuous collision detection"}}}
                }}
            };
        }

        // Vec2 schema for UI
        inline Json Vec2Schema() {
            return {
                {"type", "object"},
                {"description", "2D vector with x, y components"},
                {"properties", {
                    {"x", {{"type", "number"}, {"description", "X component"}}},
                    {"y", {{"type", "number"}, {"description", "Y component"}}}
                }},
                {"required", Json::array({"x", "y"})}
            };
        }

        // RGBA color schema
        inline Json ColorRGBASchema() {
            return {
                {"type", "object"},
                {"description", "RGBA color with alpha"},
                {"properties", {
                    {"r", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}}},
                    {"g", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}}},
                    {"b", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}}},
                    {"a", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}}}
                }}
            };
        }

        // UIComponent schema
        inline Json UIComponentSchema() {
            return {
                {"type", "object"},
                {"description", "Screen-space anchored UI widget"},
                {"properties", {
                    {"anchor", {
                        {"type", "string"},
                        {"enum", Json::array({"top_left", "top_center", "top_right", 
                                               "middle_left", "middle_center", "middle_right",
                                               "bottom_left", "bottom_center", "bottom_right"})}
                    }},
                    {"offset", Vec2Schema()},
                    {"size", Vec2Schema()},
                    {"pivot", {{"type", "string"}}},
                    {"type", {
                        {"type", "string"},
                        {"enum", Json::array({"none", "label", "health_bar", "progress_bar", 
                                               "panel", "crosshair", "objective_marker",
                                               "mini_map", "message_box", "alert_box", "custom"})}
                    }},
                    {"widgetId", {{"type", "string"}}},
                    {"text", {{"type", "string"}}},
                    {"color", ColorRGBASchema()},
                    {"backgroundColor", ColorRGBASchema()},
                    {"fontSize", {{"type", "number"}, {"minimum", 1.0}}},
                    {"fontFamily", {{"type", "string"}}},
                    {"visible", {{"type", "boolean"}}},
                    {"interactive", {{"type", "boolean"}}},
                    {"zOrder", {{"type", "integer"}}},
                    {"progress", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}}},
                    {"fadeIn", {{"type", "boolean"}}},
                    {"fadeOut", {{"type", "boolean"}}},
                    {"fadeDuration", {{"type", "number"}, {"minimum", 0.0}}}
                }}
            };
        }

        // WorldUIComponent schema
        inline Json WorldUIComponentSchema() {
            return {
                {"type", "object"},
                {"description", "3D world-space positioned UI widget"},
                {"properties", {
                    {"localOffset", Vec3Schema()},
                    {"screenOffset", Vec2Schema()},
                    {"size", Vec2Schema()},
                    {"scaleWithDistance", {{"type", "boolean"}}},
                    {"minScale", {{"type", "number"}, {"minimum", 0.0}}},
                    {"maxScale", {{"type", "number"}, {"minimum", 0.0}}},
                    {"referenceDistance", {{"type", "number"}, {"minimum", 0.0}}},
                    {"billboard", {
                        {"type", "string"},
                        {"enum", Json::array({"none", "face_camera", "face_camera_y", "face_camera_up"})}
                    }},
                    {"type", {{"type", "string"}}},
                    {"widgetId", {{"type", "string"}}},
                    {"text", {{"type", "string"}}},
                    {"color", ColorRGBASchema()},
                    {"backgroundColor", ColorRGBASchema()},
                    {"fontSize", {{"type", "number"}, {"minimum", 1.0}}},
                    {"fontFamily", {{"type", "string"}}},
                    {"enableDistanceFade", {{"type", "boolean"}}},
                    {"fadeStartDistance", {{"type", "number"}, {"minimum", 0.0}}},
                    {"fadeEndDistance", {{"type", "number"}, {"minimum", 0.0}}},
                    {"occludeByGeometry", {{"type", "boolean"}}},
                    {"clampToScreen", {{"type", "boolean"}}},
                    {"visible", {{"type", "boolean"}}},
                    {"progress", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}}},
                    {"zOrder", {{"type", "integer"}}}
                }}
            };
        }

        // Entity schema
        inline Json EntitySchema() {
            return {
                {"type", "object"},
                {"description", "Game entity with components"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Unique entity identifier"}}},
                    {"name", {{"type", "string"}, {"description", "Entity display name"}}},
                    {"components", {
                        {"type", "object"},
                        {"description", "Map of component type to component data"},
                        {"properties", {
                            {"transform", TransformSchema()},
                            {"light", LightSchema()},
                            {"mesh", MeshSchema()},
                            {"camera", CameraSchema()},
                            {"collider", ColliderSchema()},
                            {"rigidBody", RigidBodySchema()},
                            {"ui", UIComponentSchema()},
                            {"worldUI", WorldUIComponentSchema()},
                            {"prefabInstance", {
                                {"type", "object"},
                                {"properties", {
                                    {"prefabGuid", {{"type", "string"}}},
                                    {"instanceGuid", {{"type", "string"}}},
                                    {"hasLocalOverrides", {{"type", "boolean"}}},
                                    {"overrides", {{"type", "array"}}}
                                }}
                            }}
                        }}
                    }},
                    {"parent", {{"type", "integer"}, {"description", "Parent entity ID, null if root"}}},
                    {"children", {
                        {"type", "array"},
                        {"items", {{"type", "integer"}}},
                        {"description", "Child entity IDs"}
                    }}
                }},
                {"required", Json::array({"id"})}
            };
        }

        // Scene schema
        inline Json SceneSchema() {
            return {
                {"type", "object"},
                {"description", "Complete scene with entities"},
                {"properties", {
                    {"name", {{"type", "string"}}},
                    {"entityCount", {{"type", "integer"}}},
                    {"entities", {
                        {"type", "array"},
                        {"items", EntitySchema()}
                    }},
                    {"activeCamera", {{"type", "integer"}, {"description", "Active camera entity ID"}}}
                }}
            };
        }

    } // namespace Schema

    // ============================================================================
    // Serialization Functions
    // ============================================================================

    // Vec3 serialization
    inline Json SerializeVec3(const Math::Vec3& v) {
        return {{"x", v.x}, {"y", v.y}, {"z", v.z}};
    }

    inline Math::Vec3 DeserializeVec3(const Json& j, const Math::Vec3& defaultVal = Math::Vec3(0.0f)) {
        if (!j.is_object()) return defaultVal;
        return Math::Vec3(
            j.value("x", defaultVal.x),
            j.value("y", defaultVal.y),
            j.value("z", defaultVal.z)
        );
    }

    // Vec4/Quat serialization
    inline Json SerializeVec4(const Math::Vec4& v) {
        return {{"x", v.x}, {"y", v.y}, {"z", v.z}, {"w", v.w}};
    }

    inline Json SerializeQuat(const Math::Quat& q) {
        return {{"x", q.x}, {"y", q.y}, {"z", q.z}, {"w", q.w}};
    }

    inline Math::Quat DeserializeQuat(const Json& j, const Math::Quat& defaultVal = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f)) {
        if (!j.is_object()) return defaultVal;
        return Math::Quat(
            j.value("w", defaultVal.w),
            j.value("x", defaultVal.x),
            j.value("y", defaultVal.y),
            j.value("z", defaultVal.z)
        );
    }

    // Color serialization (Vec3 as RGB)
    inline Json SerializeColor(const Math::Vec3& c) {
        return {{"r", c.r}, {"g", c.g}, {"b", c.b}};
    }

    inline Math::Vec3 DeserializeColor(const Json& j, const Math::Vec3& defaultVal = Math::Vec3(1.0f)) {
        if (!j.is_object()) return defaultVal;
        return Math::Vec3(
            j.value("r", defaultVal.r),
            j.value("g", defaultVal.g),
            j.value("b", defaultVal.b)
        );
    }

    // Euler angles serialization (radians to degrees for readability)
    inline Json SerializeEulerDegrees(const Math::Vec3& radians) {
        return {
            {"pitch", glm::degrees(radians.x)},
            {"yaw", glm::degrees(radians.y)},
            {"roll", glm::degrees(radians.z)}
        };
    }

    inline Math::Vec3 DeserializeEulerDegrees(const Json& j, const Math::Vec3& defaultDegrees = Math::Vec3(0.0f)) {
        if (!j.is_object()) return glm::radians(defaultDegrees);
        return Math::Vec3(
            glm::radians(j.value("pitch", defaultDegrees.x)),
            glm::radians(j.value("yaw", defaultDegrees.y)),
            glm::radians(j.value("roll", defaultDegrees.z))
        );
    }

    // ============================================================================
    // Component Serialization
    // ============================================================================

    // TransformComponent
    inline Json SerializeTransform(const ECS::TransformComponent& t) {
        return {
            {"position", SerializeVec3(t.Position)},
            {"rotation", SerializeEulerDegrees(t.Rotation)},
            {"scale", SerializeVec3(t.Scale)}
        };
    }

    inline ECS::TransformComponent DeserializeTransform(const Json& j) {
        ECS::TransformComponent t;
        if (j.contains("position")) t.Position = DeserializeVec3(j["position"]);
        if (j.contains("rotation")) t.Rotation = DeserializeEulerDegrees(j["rotation"]);
        if (j.contains("scale")) t.Scale = DeserializeVec3(j["scale"], Math::Vec3(1.0f));
        t.IsDirty = true;
        return t;
    }

    // LightComponent
    inline std::string LightTypeToString(ECS::LightType type) {
        switch (type) {
            case ECS::LightType::Directional: return "directional";
            case ECS::LightType::Point: return "point";
            case ECS::LightType::Spot: return "spot";
            default: return "point";
        }
    }

    inline ECS::LightType StringToLightType(const std::string& s) {
        if (s == "directional") return ECS::LightType::Directional;
        if (s == "spot") return ECS::LightType::Spot;
        return ECS::LightType::Point;
    }

    inline Json SerializeLight(const ECS::LightComponent& l) {
        Json j = {
            {"type", LightTypeToString(l.Type)},
            {"color", SerializeColor(l.Color)},
            {"intensity", l.Intensity},
            {"enabled", l.Enabled},
            {"castShadows", l.CastShadows}
        };

        if (l.Type == ECS::LightType::Point || l.Type == ECS::LightType::Spot) {
            j["radius"] = l.Radius;
        }

        if (l.Type == ECS::LightType::Spot) {
            j["innerCutoff"] = glm::degrees(l.InnerCutoff);
            j["outerCutoff"] = glm::degrees(l.OuterCutoff);
        }

        return j;
    }

    inline ECS::LightComponent DeserializeLight(const Json& j) {
        ECS::LightComponent l;
        l.Type = StringToLightType(j.value("type", "point"));
        l.Color = DeserializeColor(j.value("color", Json::object()));
        l.Intensity = j.value("intensity", 1.0f);
        l.Enabled = j.value("enabled", true);
        l.CastShadows = j.value("castShadows", false);
        l.Radius = j.value("radius", 10.0f);
        l.InnerCutoff = glm::radians(j.value("innerCutoff", 0.0f));
        l.OuterCutoff = glm::radians(j.value("outerCutoff", 0.0f));
        return l;
    }

    // MeshComponent
    inline Json SerializeMesh(const ECS::MeshComponent& m) {
        return {
            {"meshPath", m.MeshPath},
            {"materialIndex", m.MaterialIndex},
            {"visible", m.Visible},
            {"castShadows", m.CastShadows},
            {"receiveShadows", m.ReceiveShadows}
        };
    }

    inline ECS::MeshComponent DeserializeMesh(const Json& j) {
        ECS::MeshComponent m;
        m.MeshPath = j.value("meshPath", "");
        m.MaterialIndex = j.value("materialIndex", 0);
        m.Visible = j.value("visible", true);
        m.CastShadows = j.value("castShadows", true);
        m.ReceiveShadows = j.value("receiveShadows", true);
        return m;
    }

    // CameraComponent
    inline std::string ProjectionTypeToString(ECS::ProjectionType type) {
        switch (type) {
            case ECS::ProjectionType::Orthographic: return "orthographic";
            default: return "perspective";
        }
    }

    inline ECS::ProjectionType StringToProjectionType(const std::string& s) {
        if (s == "orthographic") return ECS::ProjectionType::Orthographic;
        return ECS::ProjectionType::Perspective;
    }

    inline Json SerializeCamera(const ECS::CameraComponent& c) {
        Json j = {
            {"projection", ProjectionTypeToString(c.Projection)},
            {"fieldOfView", c.FieldOfView},
            {"nearPlane", c.NearPlane},
            {"farPlane", c.FarPlane},
            {"aspectRatio", c.AspectRatio},
            {"isActive", c.IsActive}
        };

        if (c.Projection == ECS::ProjectionType::Orthographic) {
            j["orthoSize"] = c.OrthoSize;
        }

        return j;
    }

    inline ECS::CameraComponent DeserializeCamera(const Json& j) {
        ECS::CameraComponent c;
        c.Projection = StringToProjectionType(j.value("projection", "perspective"));
        c.FieldOfView = j.value("fieldOfView", 60.0f);
        c.NearPlane = j.value("nearPlane", 0.1f);
        c.FarPlane = j.value("farPlane", 1000.0f);
        c.AspectRatio = j.value("aspectRatio", 16.0f / 9.0f);
        c.OrthoSize = j.value("orthoSize", 10.0f);
        c.IsActive = j.value("isActive", false);
        c.IsDirty = true;
        return c;
    }

    // ColliderComponent
    inline std::string ColliderTypeToString(ECS::ColliderType type) {
        switch (type) {
            case ECS::ColliderType::Box: return "box";
            case ECS::ColliderType::Sphere: return "sphere";
            case ECS::ColliderType::Capsule: return "capsule";
            case ECS::ColliderType::Mesh: return "mesh";
            default: return "box";
        }
    }

    inline ECS::ColliderType StringToColliderType(const std::string& s) {
        if (s == "sphere") return ECS::ColliderType::Sphere;
        if (s == "capsule") return ECS::ColliderType::Capsule;
        if (s == "mesh") return ECS::ColliderType::Mesh;
        return ECS::ColliderType::Box;
    }

    inline Json SerializeCollider(const ECS::ColliderComponent& c) {
        Json j = {
            {"type", ColliderTypeToString(c.Type)},
            {"offset", SerializeVec3(c.Offset)},
            {"friction", c.Friction},
            {"restitution", c.Restitution},
            {"isSensor", c.IsSensor}
        };

        switch (c.Type) {
            case ECS::ColliderType::Box: {
                const auto& box = std::get<ECS::BoxColliderData>(c.ShapeData);
                j["halfExtents"] = SerializeVec3(box.HalfExtents);
                break;
            }
            case ECS::ColliderType::Sphere: {
                const auto& sphere = std::get<ECS::SphereColliderData>(c.ShapeData);
                j["radius"] = sphere.Radius;
                break;
            }
            case ECS::ColliderType::Capsule: {
                const auto& capsule = std::get<ECS::CapsuleColliderData>(c.ShapeData);
                j["radius"] = capsule.Radius;
                j["halfHeight"] = capsule.HalfHeight;
                break;
            }
            default:
                break;
        }

        return j;
    }

    inline ECS::ColliderComponent DeserializeCollider(const Json& j) {
        ECS::ColliderComponent c;
        c.Type = StringToColliderType(j.value("type", "box"));
        c.Offset = DeserializeVec3(j.value("offset", Json::object()));
        c.Friction = j.value("friction", 0.5f);
        c.Restitution = j.value("restitution", 0.3f);
        c.IsSensor = j.value("isSensor", false);

        switch (c.Type) {
            case ECS::ColliderType::Box: {
                ECS::BoxColliderData box;
                box.HalfExtents = DeserializeVec3(j.value("halfExtents", Json::object()), 
                                                   Math::Vec3(0.5f));
                c.ShapeData = box;
                break;
            }
            case ECS::ColliderType::Sphere: {
                ECS::SphereColliderData sphere;
                sphere.Radius = j.value("radius", 0.5f);
                c.ShapeData = sphere;
                break;
            }
            case ECS::ColliderType::Capsule: {
                ECS::CapsuleColliderData capsule;
                capsule.Radius = j.value("radius", 0.5f);
                capsule.HalfHeight = j.value("halfHeight", 0.5f);
                c.ShapeData = capsule;
                break;
            }
            default:
                c.ShapeData = ECS::BoxColliderData{};
                break;
        }

        return c;
    }

    // RigidBodyComponent
    inline std::string MotionTypeToString(ECS::MotionType type) {
        switch (type) {
            case ECS::MotionType::Static: return "static";
            case ECS::MotionType::Kinematic: return "kinematic";
            case ECS::MotionType::Dynamic: return "dynamic";
            default: return "dynamic";
        }
    }

    inline ECS::MotionType StringToMotionType(const std::string& s) {
        if (s == "static") return ECS::MotionType::Static;
        if (s == "kinematic") return ECS::MotionType::Kinematic;
        return ECS::MotionType::Dynamic;
    }

    inline Json SerializeRigidBody(const ECS::RigidBodyComponent& rb) {
        return {
            {"motionType", MotionTypeToString(rb.Type)},
            {"mass", rb.Mass},
            {"linearDamping", rb.LinearDamping},
            {"angularDamping", rb.AngularDamping},
            {"linearVelocity", SerializeVec3(rb.LinearVelocity)},
            {"angularVelocity", SerializeVec3(rb.AngularVelocity)},
            {"gravityEnabled", rb.GravityEnabled},
            {"useCCD", rb.UseCCD}
        };
    }

    inline ECS::RigidBodyComponent DeserializeRigidBody(const Json& j) {
        ECS::RigidBodyComponent rb;
        rb.Type = StringToMotionType(j.value("motionType", "dynamic"));
        rb.Mass = j.value("mass", 1.0f);
        rb.LinearDamping = j.value("linearDamping", 0.05f);
        rb.AngularDamping = j.value("angularDamping", 0.05f);
        rb.LinearVelocity = DeserializeVec3(j.value("linearVelocity", Json::object()));
        rb.AngularVelocity = DeserializeVec3(j.value("angularVelocity", Json::object()));
        rb.GravityEnabled = j.value("gravityEnabled", true);
        rb.UseCCD = j.value("useCCD", false);
        rb.NeedsSync = true;
        return rb;
    }

    // ============================================================================
    // UIComponent Serialization
    // ============================================================================

    // Anchor serialization
    inline std::string AnchorToString(UI::Anchor anchor) {
        switch (anchor) {
            case UI::Anchor::TopLeft: return "top_left";
            case UI::Anchor::TopCenter: return "top_center";
            case UI::Anchor::TopRight: return "top_right";
            case UI::Anchor::MiddleLeft: return "middle_left";
            case UI::Anchor::MiddleCenter: return "middle_center";
            case UI::Anchor::MiddleRight: return "middle_right";
            case UI::Anchor::BottomLeft: return "bottom_left";
            case UI::Anchor::BottomCenter: return "bottom_center";
            case UI::Anchor::BottomRight: return "bottom_right";
            default: return "top_left";
        }
    }

    inline UI::Anchor StringToAnchor(const std::string& s) {
        if (s == "top_center") return UI::Anchor::TopCenter;
        if (s == "top_right") return UI::Anchor::TopRight;
        if (s == "middle_left") return UI::Anchor::MiddleLeft;
        if (s == "middle_center") return UI::Anchor::MiddleCenter;
        if (s == "middle_right") return UI::Anchor::MiddleRight;
        if (s == "bottom_left") return UI::Anchor::BottomLeft;
        if (s == "bottom_center") return UI::Anchor::BottomCenter;
        if (s == "bottom_right") return UI::Anchor::BottomRight;
        return UI::Anchor::TopLeft;
    }

    // Vec2 serialization for UI
    inline Json SerializeVec2(const glm::vec2& v) {
        return {{"x", v.x}, {"y", v.y}};
    }

    inline glm::vec2 DeserializeVec2(const Json& j, const glm::vec2& defaultVal = glm::vec2(0.0f)) {
        if (!j.is_object()) return defaultVal;
        return glm::vec2(
            j.value("x", defaultVal.x),
            j.value("y", defaultVal.y)
        );
    }

    // Vec4 deserialization (for colors with alpha)
    inline glm::vec4 DeserializeVec4(const Json& j, const glm::vec4& defaultVal = glm::vec4(1.0f)) {
        if (!j.is_object()) return defaultVal;
        return glm::vec4(
            j.value("r", defaultVal.r),
            j.value("g", defaultVal.g),
            j.value("b", defaultVal.b),
            j.value("a", defaultVal.a)
        );
    }

    // Color with alpha serialization (Vec4 as RGBA)
    inline Json SerializeColorRGBA(const glm::vec4& c) {
        return {{"r", c.r}, {"g", c.g}, {"b", c.b}, {"a", c.a}};
    }

    inline Json SerializeUIComponent(const ECS::UIComponent& ui) {
        return {
            {"anchor", AnchorToString(ui.Anchor)},
            {"offset", SerializeVec2(ui.Offset)},
            {"size", SerializeVec2(ui.Size)},
            {"pivot", AnchorToString(ui.Pivot)},
            {"type", ECS::WidgetTypeToString(ui.Type)},
            {"widgetId", ui.WidgetId},
            {"text", ui.Text},
            {"color", SerializeColorRGBA(ui.Color)},
            {"backgroundColor", SerializeColorRGBA(ui.BackgroundColor)},
            {"fontSize", ui.FontSize},
            {"fontFamily", ui.FontFamily},
            {"visible", ui.Visible},
            {"interactive", ui.Interactive},
            {"zOrder", ui.ZOrder},
            {"progress", ui.Progress},
            {"fadeIn", ui.FadeIn},
            {"fadeOut", ui.FadeOut},
            {"fadeDuration", ui.FadeDuration}
        };
    }

    inline ECS::UIComponent DeserializeUIComponent(const Json& j) {
        ECS::UIComponent ui;
        ui.Anchor = StringToAnchor(j.value("anchor", "top_left"));
        ui.Offset = DeserializeVec2(j.value("offset", Json::object()));
        ui.Size = DeserializeVec2(j.value("size", Json::object()), glm::vec2(100.0f, 100.0f));
        ui.Pivot = StringToAnchor(j.value("pivot", "top_left"));
        ui.Type = ECS::StringToWidgetType(j.value("type", "none"));
        ui.WidgetId = j.value("widgetId", "");
        ui.Text = j.value("text", "");
        ui.Color = DeserializeVec4(j.value("color", Json::object()));
        ui.BackgroundColor = DeserializeVec4(j.value("backgroundColor", Json::object()), 
                                              glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));
        ui.FontSize = j.value("fontSize", 16.0f);
        ui.FontFamily = j.value("fontFamily", "default");
        ui.Visible = j.value("visible", true);
        ui.Interactive = j.value("interactive", false);
        ui.ZOrder = j.value("zOrder", 0);
        ui.Progress = j.value("progress", 1.0f);
        ui.FadeIn = j.value("fadeIn", false);
        ui.FadeOut = j.value("fadeOut", false);
        ui.FadeDuration = j.value("fadeDuration", 0.3f);
        ui.IsDirty = true;
        return ui;
    }

    // ============================================================================
    // WorldUIComponent Serialization
    // ============================================================================

    inline std::string BillboardModeToJsonString(ECS::BillboardMode mode) {
        switch (mode) {
            case ECS::BillboardMode::None: return "none";
            case ECS::BillboardMode::FaceCamera: return "face_camera";
            case ECS::BillboardMode::FaceCameraY: return "face_camera_y";
            case ECS::BillboardMode::FaceCameraUp: return "face_camera_up";
            default: return "face_camera";
        }
    }

    inline ECS::BillboardMode JsonStringToBillboardMode(const std::string& s) {
        if (s == "none") return ECS::BillboardMode::None;
        if (s == "face_camera_y") return ECS::BillboardMode::FaceCameraY;
        if (s == "face_camera_up") return ECS::BillboardMode::FaceCameraUp;
        return ECS::BillboardMode::FaceCamera;
    }

    inline Json SerializeWorldUIComponent(const ECS::WorldUIComponent& wui) {
        return {
            {"localOffset", SerializeVec3(Math::Vec3(wui.LocalOffset))},
            {"screenOffset", SerializeVec2(wui.ScreenOffset)},
            {"size", SerializeVec2(wui.Size)},
            {"scaleWithDistance", wui.ScaleWithDistance},
            {"minScale", wui.MinScale},
            {"maxScale", wui.MaxScale},
            {"referenceDistance", wui.ReferenceDistance},
            {"billboard", BillboardModeToJsonString(wui.Billboard)},
            {"type", ECS::WidgetTypeToString(wui.Type)},
            {"widgetId", wui.WidgetId},
            {"text", wui.Text},
            {"color", SerializeColorRGBA(wui.Color)},
            {"backgroundColor", SerializeColorRGBA(wui.BackgroundColor)},
            {"fontSize", wui.FontSize},
            {"fontFamily", wui.FontFamily},
            {"enableDistanceFade", wui.EnableDistanceFade},
            {"fadeStartDistance", wui.FadeStartDistance},
            {"fadeEndDistance", wui.FadeEndDistance},
            {"occludeByGeometry", wui.OccludeByGeometry},
            {"clampToScreen", wui.ClampToScreen},
            {"visible", wui.Visible},
            {"progress", wui.Progress},
            {"zOrder", wui.ZOrder}
        };
    }

    inline ECS::WorldUIComponent DeserializeWorldUIComponent(const Json& j) {
        ECS::WorldUIComponent wui;
        auto localOffsetVec3 = DeserializeVec3(j.value("localOffset", Json::object()));
        wui.LocalOffset = glm::vec3(localOffsetVec3.x, localOffsetVec3.y, localOffsetVec3.z);
        wui.ScreenOffset = DeserializeVec2(j.value("screenOffset", Json::object()));
        wui.Size = DeserializeVec2(j.value("size", Json::object()), glm::vec2(100.0f, 20.0f));
        wui.ScaleWithDistance = j.value("scaleWithDistance", true);
        wui.MinScale = j.value("minScale", 0.5f);
        wui.MaxScale = j.value("maxScale", 2.0f);
        wui.ReferenceDistance = j.value("referenceDistance", 10.0f);
        wui.Billboard = JsonStringToBillboardMode(j.value("billboard", "face_camera"));
        wui.Type = ECS::StringToWidgetType(j.value("type", "label"));
        wui.WidgetId = j.value("widgetId", "");
        wui.Text = j.value("text", "");
        wui.Color = DeserializeVec4(j.value("color", Json::object()));
        wui.BackgroundColor = DeserializeVec4(j.value("backgroundColor", Json::object()),
                                               glm::vec4(0.0f, 0.0f, 0.0f, 0.7f));
        wui.FontSize = j.value("fontSize", 14.0f);
        wui.FontFamily = j.value("fontFamily", "default");
        wui.EnableDistanceFade = j.value("enableDistanceFade", true);
        wui.FadeStartDistance = j.value("fadeStartDistance", 30.0f);
        wui.FadeEndDistance = j.value("fadeEndDistance", 50.0f);
        wui.OccludeByGeometry = j.value("occludeByGeometry", false);
        wui.ClampToScreen = j.value("clampToScreen", true);
        wui.Visible = j.value("visible", true);
        wui.Progress = j.value("progress", 1.0f);
        wui.ZOrder = j.value("zOrder", 0);
        wui.IsDirty = true;
        return wui;
    }

    inline Json SerializePrefabInstance(const ECS::PrefabInstanceComponent& prefabInstance) {
        return {
            {"prefabGuid", prefabInstance.PrefabGuid},
            {"instanceGuid", prefabInstance.InstanceGuid},
            {"hasLocalOverrides", prefabInstance.HasLocalOverrides},
            {"overrides", prefabInstance.Overrides}
        };
    }

    // HierarchyComponent
    inline Json SerializeHierarchy(const ECS::HierarchyComponent& h) {
        Json children = Json::array();
        for (auto child : h.Children) {
            children.push_back(static_cast<uint32_t>(child));
        }

        Json j;
        if (h.Parent != entt::null) {
            j["parent"] = static_cast<uint32_t>(h.Parent);
        }
        j["children"] = children;
        j["depth"] = h.Depth;
        return j;
    }

    // ============================================================================
    // Entity and Scene Serialization
    // ============================================================================

    using NameComponent = ECS::NameComponent;

    // Serialize a single entity with all its components
    inline Json SerializeEntity(entt::entity entity, const entt::registry& registry) {
        Json j;
        j["id"] = static_cast<uint32_t>(entity);

        // Name (if available via a NameComponent - we check if it exists)
        if (auto* name = registry.try_get<NameComponent>(entity)) {
            j["name"] = name->Name;
        }

        // Components
        Json components = Json::object();

        if (auto* transform = registry.try_get<ECS::TransformComponent>(entity)) {
            components["transform"] = SerializeTransform(*transform);
        }

        if (auto* light = registry.try_get<ECS::LightComponent>(entity)) {
            components["light"] = SerializeLight(*light);
        }

        if (auto* mesh = registry.try_get<ECS::MeshComponent>(entity)) {
            components["mesh"] = SerializeMesh(*mesh);
        }

        if (auto* camera = registry.try_get<ECS::CameraComponent>(entity)) {
            components["camera"] = SerializeCamera(*camera);
        }

        if (auto* collider = registry.try_get<ECS::ColliderComponent>(entity)) {
            components["collider"] = SerializeCollider(*collider);
        }

        if (auto* rigidBody = registry.try_get<ECS::RigidBodyComponent>(entity)) {
            components["rigidBody"] = SerializeRigidBody(*rigidBody);
        }

        if (auto* uiComponent = registry.try_get<ECS::UIComponent>(entity)) {
            components["ui"] = SerializeUIComponent(*uiComponent);
        }

        if (auto* worldUI = registry.try_get<ECS::WorldUIComponent>(entity)) {
            components["worldUI"] = SerializeWorldUIComponent(*worldUI);
        }

        if (auto* prefabInstance = registry.try_get<ECS::PrefabInstanceComponent>(entity)) {
            components["prefabInstance"] = SerializePrefabInstance(*prefabInstance);
        }

        if (auto* hierarchy = registry.try_get<ECS::HierarchyComponent>(entity)) {
            j["parent"] = (hierarchy->Parent != entt::null) 
                          ? Json(static_cast<uint32_t>(hierarchy->Parent)) 
                          : Json(nullptr);
            
            Json children = Json::array();
            for (auto child : hierarchy->Children) {
                children.push_back(static_cast<uint32_t>(child));
            }
            j["children"] = children;
        }

        j["components"] = components;
        return j;
    }

    // Serialize entire scene
    inline Json SerializeScene(const ECS::Scene& scene) {
        const auto& registry = scene.GetRegistry();
        
        Json entities = Json::array();
        entt::entity activeCamera = entt::null;

        // Iterate all entities
        registry.each([&](entt::entity entity) {
            entities.push_back(SerializeEntity(entity, registry));

            // Track active camera
            if (auto* camera = registry.try_get<ECS::CameraComponent>(entity)) {
                if (camera->IsActive) {
                    activeCamera = entity;
                }
            }
        });

        Json sceneJson = {
            {"name", scene.GetName()},
            {"entityCount", entities.size()},
            {"entities", entities}
        };

        if (activeCamera != entt::null) {
            sceneJson["activeCamera"] = static_cast<uint32_t>(activeCamera);
        }

        return sceneJson;
    }

    // ============================================================================
    // Human/LLM Readable Text Format
    // ============================================================================

    // Generate human-readable text description of an entity
    inline std::string EntityToText(entt::entity entity, const entt::registry& registry, 
                                     int indent = 0) {
        std::string indentStr(indent * 2, ' ');
        std::ostringstream ss;

        ss << indentStr << "Entity #" << static_cast<uint32_t>(entity);
        
        if (auto* name = registry.try_get<NameComponent>(entity)) {
            ss << " \"" << name->Name << "\"";
        }
        ss << "\n";

        // Transform
        if (auto* t = registry.try_get<ECS::TransformComponent>(entity)) {
            ss << indentStr << "  Transform:\n";
            ss << indentStr << "    Position: (" << t->Position.x << ", " 
               << t->Position.y << ", " << t->Position.z << ")\n";
            ss << indentStr << "    Rotation: (" << glm::degrees(t->Rotation.x) << "°, "
               << glm::degrees(t->Rotation.y) << "°, " << glm::degrees(t->Rotation.z) << "°)\n";
            ss << indentStr << "    Scale: (" << t->Scale.x << ", " 
               << t->Scale.y << ", " << t->Scale.z << ")\n";
        }

        // Light
        if (auto* l = registry.try_get<ECS::LightComponent>(entity)) {
            ss << indentStr << "  Light [" << LightTypeToString(l->Type) << "]:\n";
            ss << indentStr << "    Color: RGB(" << l->Color.r << ", " 
               << l->Color.g << ", " << l->Color.b << ")\n";
            ss << indentStr << "    Intensity: " << l->Intensity << "\n";
            if (l->Type != ECS::LightType::Directional) {
                ss << indentStr << "    Radius: " << l->Radius << "\n";
            }
        }

        // Mesh
        if (auto* m = registry.try_get<ECS::MeshComponent>(entity)) {
            ss << indentStr << "  Mesh:\n";
            ss << indentStr << "    Path: " << (m->MeshPath.empty() ? "<procedural>" : m->MeshPath) << "\n";
            ss << indentStr << "    Visible: " << (m->Visible ? "yes" : "no") << "\n";
        }

        // Camera
        if (auto* c = registry.try_get<ECS::CameraComponent>(entity)) {
            ss << indentStr << "  Camera [" << ProjectionTypeToString(c->Projection) << "]:\n";
            ss << indentStr << "    FOV: " << c->FieldOfView << "°\n";
            ss << indentStr << "    Near/Far: " << c->NearPlane << " / " << c->FarPlane << "\n";
            ss << indentStr << "    Active: " << (c->IsActive ? "yes" : "no") << "\n";
        }

        // Collider
        if (auto* col = registry.try_get<ECS::ColliderComponent>(entity)) {
            ss << indentStr << "  Collider [" << ColliderTypeToString(col->Type) << "]:\n";
            ss << indentStr << "    Sensor: " << (col->IsSensor ? "yes" : "no") << "\n";
        }

        // RigidBody
        if (auto* rb = registry.try_get<ECS::RigidBodyComponent>(entity)) {
            ss << indentStr << "  RigidBody [" << MotionTypeToString(rb->Type) << "]:\n";
            ss << indentStr << "    Mass: " << rb->Mass << "\n";
            ss << indentStr << "    Gravity: " << (rb->GravityEnabled ? "enabled" : "disabled") << "\n";
        }

        // UIComponent
        if (auto* ui = registry.try_get<ECS::UIComponent>(entity)) {
            ss << indentStr << "  UI [" << ECS::WidgetTypeToString(ui->Type) << "]:\n";
            ss << indentStr << "    Anchor: " << AnchorToString(ui->Anchor) << "\n";
            ss << indentStr << "    Offset: (" << ui->Offset.x << ", " << ui->Offset.y << ")\n";
            ss << indentStr << "    Size: (" << ui->Size.x << ", " << ui->Size.y << ")\n";
            if (!ui->Text.empty()) {
                ss << indentStr << "    Text: \"" << ui->Text << "\"\n";
            }
            ss << indentStr << "    Visible: " << (ui->Visible ? "yes" : "no") << "\n";
            ss << indentStr << "    ZOrder: " << ui->ZOrder << "\n";
        }

        // WorldUIComponent
        if (auto* wui = registry.try_get<ECS::WorldUIComponent>(entity)) {
            ss << indentStr << "  WorldUI [" << ECS::WidgetTypeToString(wui->Type) << "]:\n";
            ss << indentStr << "    LocalOffset: (" << wui->LocalOffset.x << ", " 
               << wui->LocalOffset.y << ", " << wui->LocalOffset.z << ")\n";
            ss << indentStr << "    Size: (" << wui->Size.x << ", " << wui->Size.y << ")\n";
            ss << indentStr << "    Billboard: " << BillboardModeToJsonString(wui->Billboard) << "\n";
            if (!wui->Text.empty()) {
                ss << indentStr << "    Text: \"" << wui->Text << "\"\n";
            }
            ss << indentStr << "    Visible: " << (wui->Visible ? "yes" : "no") << "\n";
            if (wui->EnableDistanceFade) {
                ss << indentStr << "    Fade: " << wui->FadeStartDistance << "-" << wui->FadeEndDistance << "m\n";
            }
        }

        if (auto* prefabInstance = registry.try_get<ECS::PrefabInstanceComponent>(entity)) {
            ss << indentStr << "  PrefabInstance:\n";
            ss << indentStr << "    PrefabGuid: " << prefabInstance->PrefabGuid << "\n";
            ss << indentStr << "    InstanceGuid: " << prefabInstance->InstanceGuid << "\n";
            ss << indentStr << "    HasOverrides: " << (prefabInstance->HasLocalOverrides ? "yes" : "no") << "\n";
        }

        return ss.str();
    }

    // Generate human-readable text description of entire scene
    inline std::string SceneToText(const ECS::Scene& scene) {
        const auto& registry = scene.GetRegistry();
        std::ostringstream ss;

        ss << "=== Scene: " << scene.GetName() << " ===\n";
        ss << "Entity Count: " << scene.GetEntityCount() << "\n\n";

        // Find active camera
        entt::entity activeCamera = entt::null;
        registry.view<ECS::CameraComponent>().each([&](entt::entity e, const ECS::CameraComponent& cam) {
            if (cam.IsActive) activeCamera = e;
        });

        if (activeCamera != entt::null) {
            ss << "Active Camera: Entity #" << static_cast<uint32_t>(activeCamera) << "\n\n";
        }

        // List all entities
        ss << "--- Entities ---\n\n";
        registry.each([&](entt::entity entity) {
            ss << EntityToText(entity, registry, 0);
            ss << "\n";
        });

        return ss.str();
    }

} // namespace MCP
} // namespace Core
