#pragma once

// Scene Serialization for MCP
// Defines JSON schemas and serialization functions for EnTT Registry and Game Objects
// Used by MCP tools to communicate scene state to AI agents

#include "JsonSerialization.h"
#include "Core/Math/Math.h"
#include "Core/ECS/Scene.h"
#include "Core/ECS/Entity.h"
#include "Core/ECS/Components/NameComponent.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/LightComponent.h"
#include "Core/ECS/Components/MeshComponent.h"
#include "Core/ECS/Components/CameraComponent.h"
#include "Core/ECS/Components/ColliderComponent.h"
#include "Core/ECS/Components/RigidBodyComponent.h"
#include "Core/ECS/Components/HierarchyComponent.h"
#include "Core/ECS/Components/SkeletalMeshComponent.h"
#include "Core/ECS/Components/AnimatorComponent.h"
#include "Core/ECS/Components/IKComponent.h"
#include "Core/ECS/Components/UIComponent.h"
#include "Core/ECS/Components/WorldUIComponent.h"
#include "Core/ECS/Components/PrefabInstanceComponent.h"
#include "Core/UI/Anchoring.h"

#include <entt/entt.hpp>
#include <string>
#include <vector>
#include <optional>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <unordered_map>

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
                    {"blueprintId", {{"type", "string"}}},
                    {"layoutId", {{"type", "string"}}},
                    {"bindings", {
                        {"type", "array"},
                        {"items", {
                            {"type", "object"},
                            {"properties", {
                                {"propertyPath", {{"type", "string"}}},
                                {"dataPath", {{"type", "string"}}},
                                {"mode", {{"type", "string"}, {"enum", Json::array({"one_way", "two_way"})}}},
                                {"converterId", {{"type", "string"}}},
                                {"validatorId", {{"type", "string"}}}
                            }}
                        }}
                    }},
                    {"defaultTransitionId", {{"type", "string"}}},
                    {"modalDialogId", {{"type", "string"}}},
                    {"modalRequireFocusLock", {{"type", "boolean"}}},
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
                    {"blueprintId", {{"type", "string"}}},
                    {"layoutId", {{"type", "string"}}},
                    {"defaultModalDialogId", {{"type", "string"}}},
                    {"routeInteractionToModal", {{"type", "boolean"}}},
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
            case UI::Anchor::CenterLeft: return "middle_left";
            case UI::Anchor::Center: return "middle_center";
            case UI::Anchor::CenterRight: return "middle_right";
            case UI::Anchor::BottomLeft: return "bottom_left";
            case UI::Anchor::BottomCenter: return "bottom_center";
            case UI::Anchor::BottomRight: return "bottom_right";
            default: return "top_left";
        }
    }

    inline UI::Anchor StringToAnchor(const std::string& s) {
        if (s == "top_center") return UI::Anchor::TopCenter;
        if (s == "top_right") return UI::Anchor::TopRight;
        if (s == "middle_left") return UI::Anchor::CenterLeft;
        if (s == "middle_center") return UI::Anchor::Center;
        if (s == "middle_right") return UI::Anchor::CenterRight;
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

    inline bool IsValidMetadataIdentifier(const std::string& value) {
        if (value.empty()) {
            return false;
        }
        for (const char c : value) {
            if (!std::isalnum(static_cast<unsigned char>(c)) &&
                c != '_' && c != '-' && c != '.' && c != ':') {
                return false;
            }
        }
        return true;
    }

    inline bool IsValidBindingPath(const std::string& value) {
        if (value.empty() || value.front() == '.' || value.back() == '.') {
            return false;
        }
        bool previousDot = false;
        for (const char c : value) {
            if (c == '.') {
                if (previousDot) {
                    return false;
                }
                previousDot = true;
                continue;
            }
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
                return false;
            }
            previousDot = false;
        }
        return true;
    }

    inline Json SerializeBindingDescriptor(const ECS::UIBindingDescriptor& descriptor) {
        return {
            {"propertyPath", descriptor.PropertyPath},
            {"dataPath", descriptor.DataPath},
            {"mode", descriptor.Mode},
            {"converterId", descriptor.ConverterId},
            {"validatorId", descriptor.ValidatorId}
        };
    }

    inline std::optional<ECS::UIBindingDescriptor> DeserializeBindingDescriptor(const Json& payload) {
        if (!payload.is_object()) {
            return std::nullopt;
        }

        ECS::UIBindingDescriptor descriptor;
        descriptor.PropertyPath = payload.value("propertyPath", "");
        descriptor.DataPath = payload.value("dataPath", "");
        descriptor.Mode = payload.value("mode", "one_way");
        descriptor.ConverterId = payload.value("converterId", "");
        descriptor.ValidatorId = payload.value("validatorId", "");

        if (!IsValidBindingPath(descriptor.PropertyPath) || !IsValidBindingPath(descriptor.DataPath)) {
            return std::nullopt;
        }
        if (descriptor.Mode != "one_way" && descriptor.Mode != "two_way") {
            descriptor.Mode = "one_way";
        }
        return descriptor;
    }

    inline Json SerializeUIComponent(const ECS::UIComponent& ui) {
        Json serialized = {
            {"anchor", AnchorToString(ui.Anchor)},
            {"offset", SerializeVec2(ui.Offset)},
            {"size", SerializeVec2(ui.Size)},
            {"pivot", AnchorToString(ui.Pivot)},
            {"type", ECS::WidgetTypeToString(ui.Type)},
            {"widgetId", ui.WidgetId},
            {"text", ui.Text},
            {"blueprintId", ui.BlueprintId},
            {"layoutId", ui.LayoutId},
            {"defaultTransitionId", ui.DefaultTransitionId},
            {"modalDialogId", ui.ModalDialogId},
            {"modalRequireFocusLock", ui.ModalRequireFocusLock},
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
        Json bindings = Json::array();
        for (const ECS::UIBindingDescriptor& descriptor : ui.BindingDescriptors) {
            bindings.push_back(SerializeBindingDescriptor(descriptor));
        }
        serialized["bindings"] = bindings;
        return serialized;
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
        const std::string blueprintId = j.value("blueprintId", "");
        ui.BlueprintId = IsValidMetadataIdentifier(blueprintId) ? blueprintId : "";
        const std::string layoutId = j.value("layoutId", "");
        ui.LayoutId = IsValidMetadataIdentifier(layoutId) ? layoutId : "";
        const std::string defaultTransitionId = j.value("defaultTransitionId", "");
        ui.DefaultTransitionId = IsValidMetadataIdentifier(defaultTransitionId) ? defaultTransitionId : "";
        const std::string modalDialogId = j.value("modalDialogId", "");
        ui.ModalDialogId = IsValidMetadataIdentifier(modalDialogId) ? modalDialogId : "";
        ui.ModalRequireFocusLock = j.value("modalRequireFocusLock", false);
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
        ui.BindingDescriptors.clear();
        const Json bindings = j.value("bindings", Json::array());
        if (bindings.is_array()) {
            for (const Json& bindingPayload : bindings) {
                std::optional<ECS::UIBindingDescriptor> descriptor =
                    DeserializeBindingDescriptor(bindingPayload);
                if (descriptor.has_value()) {
                    ui.BindingDescriptors.push_back(*descriptor);
                }
            }
        }
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
            {"blueprintId", wui.BlueprintId},
            {"layoutId", wui.LayoutId},
            {"defaultModalDialogId", wui.DefaultModalDialogId},
            {"routeInteractionToModal", wui.RouteInteractionToModal},
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
        const std::string worldBlueprintId = j.value("blueprintId", "");
        wui.BlueprintId = IsValidMetadataIdentifier(worldBlueprintId) ? worldBlueprintId : "";
        const std::string worldLayoutId = j.value("layoutId", "");
        wui.LayoutId = IsValidMetadataIdentifier(worldLayoutId) ? worldLayoutId : "";
        const std::string worldModalId = j.value("defaultModalDialogId", "");
        wui.DefaultModalDialogId = IsValidMetadataIdentifier(worldModalId) ? worldModalId : "";
        wui.RouteInteractionToModal = j.value("routeInteractionToModal", false);
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

    inline Json SerializeSkeletalMesh(const ECS::SkeletalMeshComponent& skeletal) {
        return {
            {"meshPath", skeletal.MeshPath},
            {"materialIndex", skeletal.MaterialIndex},
            {"visible", skeletal.Visible},
            {"castShadows", skeletal.CastShadows},
            {"receiveShadows", skeletal.ReceiveShadows},
            {"autoUpdate", skeletal.AutoUpdate},
            {"rootMotionEnabled", skeletal.RootMotionEnabled},
            {"graphRuntimeAuthoritative", skeletal.GraphRuntimeAuthoritative},
            {"motionFeatureCacheHandle", skeletal.MotionFeatureCacheHandle},
            {"selectedMotionPoseId", skeletal.SelectedMotionPoseId},
            {"trajectoryHistoryHandle", skeletal.TrajectoryHistoryHandle},
            {"activeRetargetProfileId", skeletal.ActiveRetargetProfileId},
            {"lastRetargetedClipName", skeletal.LastRetargetedClipName}
        };
    }

    inline ECS::SkeletalMeshComponent DeserializeSkeletalMesh(const Json& j) {
        ECS::SkeletalMeshComponent skeletal;
        skeletal.MeshPath = j.value("meshPath", std::string{});
        skeletal.MaterialIndex = j.value("materialIndex", 0u);
        skeletal.Visible = j.value("visible", true);
        skeletal.CastShadows = j.value("castShadows", true);
        skeletal.ReceiveShadows = j.value("receiveShadows", true);
        skeletal.AutoUpdate = j.value("autoUpdate", true);
        skeletal.RootMotionEnabled = j.value("rootMotionEnabled", false);
        skeletal.GraphRuntimeAuthoritative = j.value("graphRuntimeAuthoritative", false);
        skeletal.MotionFeatureCacheHandle = j.value("motionFeatureCacheHandle", 0ull);
        skeletal.SelectedMotionPoseId = j.value("selectedMotionPoseId", std::string{});
        skeletal.TrajectoryHistoryHandle = j.value("trajectoryHistoryHandle", 0ull);
        skeletal.ActiveRetargetProfileId = j.value("activeRetargetProfileId", std::string{});
        skeletal.LastRetargetedClipName = j.value("lastRetargetedClipName", std::string{});
        return skeletal;
    }

    inline Json SerializeAnimator(const ECS::AnimatorComponent& animator) {
        Json parameters = Json::object();
        for (const auto& [name, value] : animator.Parameters) {
            Json parameterJson = Json::object();
            parameterJson["type"] = ECS::ParameterTypeToString(value.Type);
            switch (value.Type) {
                case ECS::AnimatorParameterType::Float:
                    parameterJson["value"] = value.GetFloat();
                    break;
                case ECS::AnimatorParameterType::Bool:
                    parameterJson["value"] = value.GetBool();
                    break;
                case ECS::AnimatorParameterType::Int:
                    parameterJson["value"] = value.GetInt();
                    break;
                case ECS::AnimatorParameterType::Trigger:
                    parameterJson["value"] = value.IsTriggerSet();
                    break;
            }
            parameters[name] = std::move(parameterJson);
        }

        Json layers = Json::array();
        for (const ECS::AnimationLayer& layer : animator.Layers) {
            layers.push_back({
                {"name", layer.Name},
                {"index", layer.LayerIndex},
                {"weight", layer.Weight},
                {"isAdditive", layer.IsAdditive},
                {"stateMachine", layer.StateMachineName},
                {"currentState", layer.CurrentStateName},
                {"currentTime", layer.CurrentTime},
                {"affectedBones", layer.AffectedBones}
            });
        }

        return {
            {"stateMachine", animator.StateMachine.Name},
            {"currentState", animator.RuntimeState.CurrentStateName},
            {"currentStateTime", animator.RuntimeState.CurrentStateTime},
            {"normalizedTime", animator.RuntimeState.NormalizedTime},
            {"isPlaying", animator.RuntimeState.IsPlaying},
            {"enabled", animator.Enabled},
            {"updateRate", animator.UpdateRate},
            {"runtimeMode", static_cast<uint32_t>(animator.RuntimeMode)},
            {"activeGraphId", animator.ActiveGraphId},
            {"activeBlendTreeId", animator.ActiveBlendTreeId},
            {"motionDatabaseId", animator.MotionDatabaseId},
            {"motionMatchingEnabled", animator.MotionMatchingEnabled},
            {"graphEvaluationCount", animator.GraphEvaluationCount},
            {"legacyFallbackCount", animator.LegacyFallbackCount},
            {"orchestrationConflictCount", animator.OrchestrationConflictCount},
            {"parameters", std::move(parameters)},
            {"layers", std::move(layers)}
        };
    }

    inline ECS::AnimatorComponent DeserializeAnimator(const Json& j) {
        ECS::AnimatorComponent animator;
        animator.Enabled = j.value("enabled", true);
        animator.UpdateRate = j.value("updateRate", 0.0f);
        animator.RuntimeState.CurrentStateName = j.value("currentState", std::string{});
        animator.RuntimeState.CurrentStateTime = j.value("currentStateTime", 0.0f);
        animator.RuntimeState.NormalizedTime = j.value("normalizedTime", 0.0f);
        animator.RuntimeState.IsPlaying = j.value("isPlaying", true);
        animator.RuntimeMode = static_cast<ECS::AnimatorRuntimeMode>(
            j.value("runtimeMode", static_cast<uint32_t>(ECS::AnimatorRuntimeMode::Legacy)));
        animator.ActiveGraphId = j.value("activeGraphId", std::string{});
        animator.ActiveBlendTreeId = j.value("activeBlendTreeId", std::string{});
        animator.MotionDatabaseId = j.value("motionDatabaseId", std::string{});
        animator.MotionMatchingEnabled = j.value("motionMatchingEnabled", false);
        animator.GraphEvaluationCount = j.value("graphEvaluationCount", 0ull);
        animator.LegacyFallbackCount = j.value("legacyFallbackCount", 0ull);
        animator.OrchestrationConflictCount = j.value("orchestrationConflictCount", 0ull);

        const Json parameters = j.value("parameters", Json::object());
        for (auto it = parameters.begin(); it != parameters.end(); ++it) {
            const std::string paramName = it.key();
            const Json& parameterJson = it.value();
            const std::string type = parameterJson.value("type", "Float");
            if (type == "Float") {
                animator.Parameters[paramName] =
                    ECS::AnimatorParameterValue::CreateFloat(parameterJson.value("value", 0.0f));
            } else if (type == "Bool") {
                animator.Parameters[paramName] =
                    ECS::AnimatorParameterValue::CreateBool(parameterJson.value("value", false));
            } else if (type == "Int") {
                animator.Parameters[paramName] =
                    ECS::AnimatorParameterValue::CreateInt(parameterJson.value("value", 0));
            } else if (type == "Trigger") {
                ECS::AnimatorParameterValue trigger = ECS::AnimatorParameterValue::CreateTrigger();
                if (parameterJson.value("value", false)) {
                    trigger.Value = true;
                    trigger.TriggerConsumed = false;
                }
                animator.Parameters[paramName] = std::move(trigger);
            }
        }

        const Json layers = j.value("layers", Json::array());
        for (const Json& layerJson : layers) {
            ECS::AnimationLayer layer;
            layer.Name = layerJson.value("name", std::string{});
            layer.LayerIndex = layerJson.value("index", 0);
            layer.Weight = layerJson.value("weight", 1.0f);
            layer.IsAdditive = layerJson.value("isAdditive", false);
            layer.StateMachineName = layerJson.value("stateMachine", std::string{});
            layer.CurrentStateName = layerJson.value("currentState", std::string{});
            layer.CurrentTime = layerJson.value("currentTime", 0.0f);
            layer.AffectedBones = layerJson.value("affectedBones", std::vector<std::string>{});
            animator.Layers.push_back(std::move(layer));
        }

        return animator;
    }

    inline Json SerializeIK(const ECS::IKComponent& ik) {
        return {
            {"enabled", ik.Enabled},
            {"globalWeight", ik.GlobalWeight},
            {"footIKEnabled", ik.FootSettings.Enabled},
            {"enableControlRigBake", ik.EnableControlRigBake},
            {"capturePreBakePose", ik.CapturePreBakePose},
            {"capturePostBakePose", ik.CapturePostBakePose},
            {"controlRigAssetId", ik.ControlRigAssetId},
            {"controlRigChannelBindings", ik.ControlRigChannelBindings},
            {"lastBakedClipName", ik.LastBakedClipName}
        };
    }

    inline ECS::IKComponent DeserializeIK(const Json& j) {
        ECS::IKComponent ik;
        ik.Enabled = j.value("enabled", true);
        ik.GlobalWeight = j.value("globalWeight", 1.0f);
        ik.FootSettings.Enabled = j.value("footIKEnabled", true);
        ik.EnableControlRigBake = j.value("enableControlRigBake", false);
        ik.CapturePreBakePose = j.value("capturePreBakePose", false);
        ik.CapturePostBakePose = j.value("capturePostBakePose", false);
        ik.ControlRigAssetId = j.value("controlRigAssetId", std::string{});
        ik.ControlRigChannelBindings = j.value("controlRigChannelBindings", std::vector<std::string>{});
        ik.LastBakedClipName = j.value("lastBakedClipName", std::string{});
        return ik;
    }

    inline Json SerializePrefabInstance(const ECS::PrefabInstanceComponent& prefabInstance) {
        return {
            {"prefabGuid", prefabInstance.PrefabGuid},
            {"instanceGuid", prefabInstance.InstanceGuid},
            {"hasLocalOverrides", prefabInstance.HasLocalOverrides},
            {"overrides", prefabInstance.Overrides}
        };
    }

    inline ECS::PrefabInstanceComponent DeserializePrefabInstance(const Json& j) {
        ECS::PrefabInstanceComponent prefabInstance;
        prefabInstance.PrefabGuid = j.value("prefabGuid", std::string{});
        prefabInstance.InstanceGuid = j.value("instanceGuid", std::string{});
        prefabInstance.HasLocalOverrides = j.value("hasLocalOverrides", false);
        prefabInstance.Overrides = j.value("overrides", Json::array());
        return prefabInstance;
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

        if (auto* skeletal = registry.try_get<ECS::SkeletalMeshComponent>(entity)) {
            components["skeletalMesh"] = SerializeSkeletalMesh(*skeletal);
        }

        if (auto* animator = registry.try_get<ECS::AnimatorComponent>(entity)) {
            components["animator"] = SerializeAnimator(*animator);
        }

        if (auto* ik = registry.try_get<ECS::IKComponent>(entity)) {
            components["ik"] = SerializeIK(*ik);
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

    constexpr uint32_t SCENE_ASSET_SCHEMA_VERSION = 2;

    // Serialize entire scene
    inline Json SerializeScene(const ECS::Scene& scene) {
        const auto& registry = scene.GetRegistry();
        std::vector<entt::entity> orderedEntities;
        orderedEntities.reserve(scene.GetEntityCount());

        if (const auto* entitiesStorage = registry.storage<entt::entity>()) {
            orderedEntities.reserve(entitiesStorage->size());
            for (entt::entity entity : *entitiesStorage) {
                orderedEntities.push_back(entity);
            }
        }

        std::sort(orderedEntities.begin(), orderedEntities.end(),
            [](entt::entity lhs, entt::entity rhs) {
                return static_cast<uint32_t>(lhs) < static_cast<uint32_t>(rhs);
            });

        Json entities = Json::array();
        entt::entity activeCamera = entt::null;

        for (entt::entity entity : orderedEntities) {
            entities.push_back(SerializeEntity(entity, registry));

            if (const auto* camera = registry.try_get<ECS::CameraComponent>(entity); camera && camera->IsActive) {
                activeCamera = entity;
            }
        }

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

    inline bool DeserializeScene(const Json& sceneJson, ECS::Scene& scene, std::string* errorMessage = nullptr) {
        if (!sceneJson.is_object()) {
            if (errorMessage) {
                *errorMessage = "Scene payload must be a JSON object.";
            }
            return false;
        }

        if (!sceneJson.contains("entities") || !sceneJson["entities"].is_array()) {
            if (errorMessage) {
                *errorMessage = "Scene payload is missing an entities array.";
            }
            return false;
        }

        scene.Clear();
        scene.SetName(sceneJson.value("name", std::string{"Untitled Scene"}));

        auto& registry = scene.GetRegistry();
        const Json& entities = sceneJson["entities"];
        std::unordered_map<uint32_t, entt::entity> entityIdMap;
        entityIdMap.reserve(entities.size());

        // Pass 1: create entity handles with stable id mapping.
        for (const Json& entityJson : entities) {
            if (!entityJson.contains("id")) {
                continue;
            }

            const uint32_t sourceId = entityJson.value("id", 0u);
            const std::string name = entityJson.value("name", std::string{"Entity"});
            ECS::Entity created = scene.CreateEntity(name);
            entityIdMap[sourceId] = created.GetHandle();
        }

        // Pass 2: deserialize components.
        for (const Json& entityJson : entities) {
            const uint32_t sourceId = entityJson.value("id", 0u);
            const auto mapped = entityIdMap.find(sourceId);
            if (mapped == entityIdMap.end()) {
                continue;
            }

            const entt::entity entity = mapped->second;
            const Json components = entityJson.value("components", Json::object());

            if (components.contains("transform")) {
                registry.emplace_or_replace<ECS::TransformComponent>(
                    entity, DeserializeTransform(components["transform"]));
            }
            if (components.contains("light")) {
                registry.emplace_or_replace<ECS::LightComponent>(
                    entity, DeserializeLight(components["light"]));
            }
            if (components.contains("mesh")) {
                registry.emplace_or_replace<ECS::MeshComponent>(
                    entity, DeserializeMesh(components["mesh"]));
            }
            if (components.contains("skeletalMesh")) {
                registry.emplace_or_replace<ECS::SkeletalMeshComponent>(
                    entity, DeserializeSkeletalMesh(components["skeletalMesh"]));
            }
            if (components.contains("animator")) {
                registry.emplace_or_replace<ECS::AnimatorComponent>(
                    entity, DeserializeAnimator(components["animator"]));
            }
            if (components.contains("ik")) {
                registry.emplace_or_replace<ECS::IKComponent>(
                    entity, DeserializeIK(components["ik"]));
            }
            if (components.contains("camera")) {
                registry.emplace_or_replace<ECS::CameraComponent>(
                    entity, DeserializeCamera(components["camera"]));
            }
            if (components.contains("collider")) {
                registry.emplace_or_replace<ECS::ColliderComponent>(
                    entity, DeserializeCollider(components["collider"]));
            }
            if (components.contains("rigidBody")) {
                registry.emplace_or_replace<ECS::RigidBodyComponent>(
                    entity, DeserializeRigidBody(components["rigidBody"]));
            }
            if (components.contains("ui")) {
                registry.emplace_or_replace<ECS::UIComponent>(
                    entity, DeserializeUIComponent(components["ui"]));
            }
            if (components.contains("worldUI")) {
                registry.emplace_or_replace<ECS::WorldUIComponent>(
                    entity, DeserializeWorldUIComponent(components["worldUI"]));
            }
            if (components.contains("prefabInstance")) {
                registry.emplace_or_replace<ECS::PrefabInstanceComponent>(
                    entity, DeserializePrefabInstance(components["prefabInstance"]));
            }
        }

        // Pass 3: rebuild hierarchy references.
        for (const Json& entityJson : entities) {
            const uint32_t sourceId = entityJson.value("id", 0u);
            const auto mapped = entityIdMap.find(sourceId);
            if (mapped == entityIdMap.end()) {
                continue;
            }

            const entt::entity entity = mapped->second;
            ECS::HierarchyComponent hierarchy;
            bool hasHierarchyData = false;

            if (entityJson.contains("parent") && entityJson["parent"].is_number_unsigned()) {
                const uint32_t parentId = entityJson["parent"].get<uint32_t>();
                const auto parentIt = entityIdMap.find(parentId);
                if (parentIt != entityIdMap.end()) {
                    hierarchy.Parent = parentIt->second;
                    hasHierarchyData = true;
                }
            }

            if (entityJson.contains("children") && entityJson["children"].is_array()) {
                for (const Json& child : entityJson["children"]) {
                    if (!child.is_number_unsigned()) {
                        continue;
                    }
                    const auto childIt = entityIdMap.find(child.get<uint32_t>());
                    if (childIt != entityIdMap.end()) {
                        hierarchy.Children.push_back(childIt->second);
                        hasHierarchyData = true;
                    }
                }
            }

            if (hasHierarchyData) {
                if (hierarchy.Parent != entt::null && registry.valid(hierarchy.Parent)) {
                    if (const auto* parentHierarchy = registry.try_get<ECS::HierarchyComponent>(hierarchy.Parent)) {
                        hierarchy.Depth = parentHierarchy->Depth + 1;
                    } else {
                        hierarchy.Depth = 1;
                    }
                } else {
                    hierarchy.Depth = 0;
                }

                registry.emplace_or_replace<ECS::HierarchyComponent>(entity, hierarchy);
            }
        }

        // Restore active camera selection.
        if (sceneJson.contains("activeCamera") && sceneJson["activeCamera"].is_number_unsigned()) {
            const uint32_t activeCameraSourceId = sceneJson["activeCamera"].get<uint32_t>();
            const auto activeIt = entityIdMap.find(activeCameraSourceId);

            auto cameras = registry.view<ECS::CameraComponent>();
            for (auto cameraEntity : cameras) {
                auto& camera = cameras.get<ECS::CameraComponent>(cameraEntity);
                camera.IsActive = (activeIt != entityIdMap.end() && cameraEntity == activeIt->second);
                camera.IsDirty = true;
            }
        }

        return true;
    }

    inline bool MigrateScenePayloadToCurrentSchema(
        Json* scenePayload,
        uint32_t fromSchemaVersion,
        std::string* errorMessage = nullptr) {
        if (scenePayload == nullptr || !scenePayload->is_object()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Scene payload migration received invalid JSON object.";
            }
            return false;
        }

        if (fromSchemaVersion >= SCENE_ASSET_SCHEMA_VERSION) {
            return true;
        }

        if (fromSchemaVersion < 2) {
            Json& entities = (*scenePayload)["entities"];
            if (!entities.is_array()) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Scene migration expected entities array.";
                }
                return false;
            }

            for (Json& entityPayload : entities) {
                Json& components = entityPayload["components"];
                if (!components.is_object()) {
                    continue;
                }

                if (components.contains("ui") && components["ui"].is_object()) {
                    Json& ui = components["ui"];
                    ui["blueprintId"] = ui.value("blueprintId", "");
                    ui["layoutId"] = ui.value("layoutId", "");
                    ui["bindings"] = ui.value("bindings", Json::array());
                    ui["defaultTransitionId"] = ui.value("defaultTransitionId", "");
                    ui["modalDialogId"] = ui.value("modalDialogId", "");
                    ui["modalRequireFocusLock"] = ui.value("modalRequireFocusLock", false);
                }

                if (components.contains("worldUI") && components["worldUI"].is_object()) {
                    Json& worldUI = components["worldUI"];
                    worldUI["blueprintId"] = worldUI.value("blueprintId", "");
                    worldUI["layoutId"] = worldUI.value("layoutId", "");
                    worldUI["defaultModalDialogId"] = worldUI.value("defaultModalDialogId", "");
                    worldUI["routeInteractionToModal"] = worldUI.value("routeInteractionToModal", false);
                }
            }
        }

        return true;
    }

    inline bool SerializeSceneToAsset(const ECS::Scene& scene,
                                      const std::filesystem::path& assetPath,
                                      std::string* errorMessage = nullptr) {
        if (assetPath.empty()) {
            if (errorMessage) {
                *errorMessage = "Asset path is empty.";
            }
            return false;
        }

        std::error_code ec;
        const auto parentPath = assetPath.parent_path();
        if (!parentPath.empty()) {
            std::filesystem::create_directories(parentPath, ec);
            if (ec) {
                if (errorMessage) {
                    *errorMessage = "Failed to create scene asset directory: " + parentPath.string();
                }
                return false;
            }
        }

        std::ofstream output(assetPath, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            if (errorMessage) {
                *errorMessage = "Failed to open scene asset for writing: " + assetPath.string();
            }
            return false;
        }

        Json sceneAsset = {
            {"schemaVersion", SCENE_ASSET_SCHEMA_VERSION},
            {"assetType", "scene"},
            {"scene", SerializeScene(scene)}
        };

        output << sceneAsset.dump(2);
        return output.good();
    }

    inline bool DeserializeSceneFromAsset(const std::filesystem::path& assetPath,
                                          ECS::Scene& scene,
                                          std::string* errorMessage = nullptr) {
        if (assetPath.empty()) {
            if (errorMessage) {
                *errorMessage = "Asset path is empty.";
            }
            return false;
        }

        std::ifstream input(assetPath, std::ios::binary);
        if (!input.is_open()) {
            if (errorMessage) {
                *errorMessage = "Failed to open scene asset: " + assetPath.string();
            }
            return false;
        }

        Json payload;
        try {
            input >> payload;
        } catch (const nlohmann::json::parse_error&) {
            if (errorMessage) {
                *errorMessage = "Scene asset JSON parse error.";
            }
            return false;
        }

        uint32_t schemaVersion = SCENE_ASSET_SCHEMA_VERSION;
        const Json* scenePayload = &payload;

        if (payload.contains("scene")) {
            schemaVersion = payload.value("schemaVersion", SCENE_ASSET_SCHEMA_VERSION);
            scenePayload = &payload["scene"];
        }

        if (schemaVersion > SCENE_ASSET_SCHEMA_VERSION) {
            if (errorMessage) {
                *errorMessage = "Unsupported scene asset schema version: " + std::to_string(schemaVersion);
            }
            return false;
        }

        Json migratedPayload = *scenePayload;
        if (!MigrateScenePayloadToCurrentSchema(&migratedPayload, schemaVersion, errorMessage)) {
            return false;
        }

        return DeserializeScene(migratedPayload, scene, errorMessage);
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
        if (const auto* entitiesStorage = registry.storage<entt::entity>()) {
            for (entt::entity entity : *entitiesStorage) {
                ss << EntityToText(entity, registry, 0);
                ss << "\n";
            }
        }

        return ss.str();
    }

} // namespace MCP
} // namespace Core
