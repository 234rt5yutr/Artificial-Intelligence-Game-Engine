#pragma once

#include "Core/Physics/PhysicsLayers.h"
#include "Core/Math/Math.h"
#include <memory>
#include <variant>

namespace Core {
namespace ECS {

    // Collider shape types
    enum class ColliderType : uint8_t {
        Box,
        Sphere,
        Capsule,
        Mesh
    };

    // Box collider parameters
    struct BoxColliderData {
        Math::Vec3 HalfExtents{ 0.5f, 0.5f, 0.5f };
    };

    // Sphere collider parameters
    struct SphereColliderData {
        float Radius = 0.5f;
    };

    // Capsule collider parameters
    struct CapsuleColliderData {
        float Radius = 0.5f;
        float HalfHeight = 0.5f;
    };

    // Mesh collider parameters (convex hull or triangle mesh)
    struct MeshColliderData {
        std::vector<Math::Vec3> Vertices;
        std::vector<uint32_t> Indices;
        bool Convex = true;  // If false, uses triangle mesh (static only)
    };

    // Collider shape variant
    using ColliderShapeData = std::variant<BoxColliderData, SphereColliderData, CapsuleColliderData, MeshColliderData>;

    struct ColliderComponent {
        ColliderType Type = ColliderType::Box;
        ColliderShapeData ShapeData;

        // Local offset from entity transform
        Math::Vec3 Offset{ 0.0f, 0.0f, 0.0f };
        Math::Quat RotationOffset{ 1.0f, 0.0f, 0.0f, 0.0f };

        // Physics material properties
        float Friction = 0.5f;
        float Restitution = 0.3f;

        // Collision layer
        JPH::ObjectLayer Layer = Physics::Layers::MOVING;

        // Is this a sensor/trigger (no physical response)
        bool IsSensor = false;

        ColliderComponent() : ShapeData(BoxColliderData{}) {}

        // Factory methods
        static ColliderComponent CreateBox(const Math::Vec3& halfExtents, 
                                            JPH::ObjectLayer layer = Physics::Layers::MOVING)
        {
            ColliderComponent c;
            c.Type = ColliderType::Box;
            c.ShapeData = BoxColliderData{ halfExtents };
            c.Layer = layer;
            return c;
        }

        static ColliderComponent CreateSphere(float radius,
                                               JPH::ObjectLayer layer = Physics::Layers::MOVING)
        {
            ColliderComponent c;
            c.Type = ColliderType::Sphere;
            c.ShapeData = SphereColliderData{ radius };
            c.Layer = layer;
            return c;
        }

        static ColliderComponent CreateCapsule(float radius, float halfHeight,
                                                JPH::ObjectLayer layer = Physics::Layers::MOVING)
        {
            ColliderComponent c;
            c.Type = ColliderType::Capsule;
            c.ShapeData = CapsuleColliderData{ radius, halfHeight };
            c.Layer = layer;
            return c;
        }

        static ColliderComponent CreateSensor(const Math::Vec3& halfExtents)
        {
            ColliderComponent c;
            c.Type = ColliderType::Box;
            c.ShapeData = BoxColliderData{ halfExtents };
            c.Layer = Physics::Layers::SENSOR;
            c.IsSensor = true;
            return c;
        }
    };

} // namespace ECS
} // namespace Core
