#pragma once

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/RigidBodyComponent.h"
#include "Core/ECS/Components/ColliderComponent.h"
#include "Core/Physics/PhysicsWorld.h"
#include "Core/Profile.h"
#include "Core/Log.h"
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

namespace Core {
namespace ECS {

    class PhysicsSystem {
    public:
        PhysicsSystem();
        ~PhysicsSystem();

        // Initialize with physics world reference
        void Initialize(Physics::PhysicsWorld* physicsWorld);

        // Shutdown and cleanup
        void Shutdown();

        // Pre-physics update: sync ECS -> Physics (create bodies, apply forces)
        void PreUpdate(Scene& scene);

        // Step the physics simulation
        void Update(float deltaTime);

        // Post-physics update: sync Physics -> ECS (handled by separate system in Step 6.6)
        
        // Fixed timestep settings
        void SetFixedTimestep(float timestep) { m_FixedTimestep = timestep; }
        float GetFixedTimestep() const { return m_FixedTimestep; }

        // Collision steps per update
        void SetCollisionSteps(int steps) { m_CollisionSteps = steps; }
        int GetCollisionSteps() const { return m_CollisionSteps; }

        // Statistics
        uint32_t GetActiveBodyCount() const { return m_ActiveBodyCount; }
        uint32_t GetTotalBodyCount() const { return m_TotalBodyCount; }
        float GetLastStepTime() const { return m_LastStepTime; }

    private:
        // Create a Jolt body from ECS components
        void CreateBody(entt::registry& registry, entt::entity entity,
                        TransformComponent& transform, RigidBodyComponent& rigidBody,
                        ColliderComponent& collider);

        // Create Jolt shape from collider data
        JPH::RefConst<JPH::Shape> CreateShape(const ColliderComponent& collider);

        // Apply pending forces/impulses to bodies
        void ApplyPendingForces(entt::registry& registry);

        // Remove destroyed bodies
        void RemoveDestroyedBodies(Scene& scene);

    private:
        Physics::PhysicsWorld* m_PhysicsWorld = nullptr;

        // Simulation settings
        float m_FixedTimestep = 1.0f / 60.0f;  // 60 Hz
        int m_CollisionSteps = 1;
        float m_AccumulatedTime = 0.0f;

        // Statistics
        uint32_t m_ActiveBodyCount = 0;
        uint32_t m_TotalBodyCount = 0;
        float m_LastStepTime = 0.0f;
    };

} // namespace ECS
} // namespace Core
