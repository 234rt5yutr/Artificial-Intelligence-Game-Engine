#pragma once

// Scene Serialization for MCP
// Defines JSON schemas and serialization functions for EnTT Registry and Game Objects
// Used by MCP tools to communicate scene state to AI agents

#include "JsonSerialization.h"
#include "Core/Math/Math.h"
#include "Core/ECS/Scene.h"
#include "Core/ECS/Entity.h"
#include "Core/ECS/Components/Components.h"

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
                            {"rigidBody", RigidBodySchema()}
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

    // NameComponent for entity names (simple struct)
    struct NameComponent {
        std::string Name;
    };

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
