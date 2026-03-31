#pragma once

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/CameraComponent.h"
#include "Core/ECS/Components/ThirdPersonCameraComponent.h"
#include "Core/Physics/PhysicsWorld.h"
#include "Core/Profile.h"
#include "Core/Log.h"
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>

namespace Core {
namespace ECS {

    class ThirdPersonCameraSystem {
    public:
        ThirdPersonCameraSystem() = default;
        ~ThirdPersonCameraSystem() = default;

        // Initialize with physics world for raycasting
        void Initialize(Physics::PhysicsWorld* physicsWorld);

        // Update third-person cameras (call after player controller, before camera system)
        void Update(Scene& scene, float deltaTime);

        // Set physics world reference
        void SetPhysicsWorld(Physics::PhysicsWorld* physicsWorld) { m_PhysicsWorld = physicsWorld; }

    private:
        // Update a single third-person camera
        void UpdateCamera(
            entt::registry& registry,
            ThirdPersonCameraComponent& tpCamera,
            CameraComponent& camera,
            TransformComponent& cameraTransform,
            float deltaTime);

        // Perform spring-arm raycast to detect collisions
        float PerformSpringArmRaycast(
            const Math::Vec3& start,
            const Math::Vec3& end,
            float armLength,
            float collisionRadius,
            entt::entity ignoreEntity);

    private:
        Physics::PhysicsWorld* m_PhysicsWorld = nullptr;
    };

} // namespace ECS
} // namespace Core
