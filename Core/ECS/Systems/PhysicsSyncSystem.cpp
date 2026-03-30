#include "Core/ECS/Systems/PhysicsSyncSystem.h"
#include <chrono>
#include <cmath>

namespace Core {
namespace ECS {

    PhysicsSyncSystem::PhysicsSyncSystem() = default;

    PhysicsSyncSystem::~PhysicsSyncSystem()
    {
        Shutdown();
    }

    void PhysicsSyncSystem::Initialize(Physics::PhysicsWorld* physicsWorld)
    {
        PROFILE_FUNCTION();

        m_PhysicsWorld = physicsWorld;
        m_SyncedBodyCount = 0;
        m_LastSyncTime = 0.0f;

        ENGINE_CORE_INFO("PhysicsSyncSystem initialized");
    }

    void PhysicsSyncSystem::Shutdown()
    {
        PROFILE_FUNCTION();

        m_PhysicsWorld = nullptr;
        ENGINE_CORE_INFO("PhysicsSyncSystem shutdown");
    }

    void PhysicsSyncSystem::Update(Scene& scene)
    {
        PROFILE_FUNCTION();

        if (!m_PhysicsWorld || !m_PhysicsWorld->IsInitialized()) {
            return;
        }

        auto startTime = std::chrono::high_resolution_clock::now();

        auto& registry = scene.GetRegistry();
        m_SyncedBodyCount = 0;

        // Iterate all entities with both Transform and RigidBody components
        auto view = registry.view<TransformComponent, RigidBodyComponent>();
        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& rigidBody = view.get<RigidBodyComponent>(entity);

            // Only sync if body exists and is dynamic or kinematic
            if (rigidBody.IsBodyCreated && rigidBody.Type != MotionType::Static) {
                // Check if entity has custom sync settings
                if (registry.any_of<PhysicsSyncComponent>(entity)) {
                    auto& sync = registry.get<PhysicsSyncComponent>(entity);
                    if (!sync.Enabled) {
                        continue;
                    }
                }

                SyncEntityTransform(registry, entity, transform, rigidBody);
                m_SyncedBodyCount++;
            }
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        m_LastSyncTime = std::chrono::duration<float>(endTime - startTime).count();

        ENGINE_CORE_TRACE("PhysicsSyncSystem: synced {} bodies in {:.3f}ms",
                          m_SyncedBodyCount, m_LastSyncTime * 1000.0f);
    }

    void PhysicsSyncSystem::UpdateInterpolated(Scene& scene, float alpha)
    {
        PROFILE_FUNCTION();

        if (!m_PhysicsWorld || !m_PhysicsWorld->IsInitialized()) {
            return;
        }

        if (!m_InterpolationEnabled) {
            Update(scene);
            return;
        }

        auto startTime = std::chrono::high_resolution_clock::now();

        auto& registry = scene.GetRegistry();
        m_SyncedBodyCount = 0;

        // Only interpolate entities that have the PhysicsSyncComponent
        auto view = registry.view<TransformComponent, RigidBodyComponent, PhysicsSyncComponent>();
        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& rigidBody = view.get<RigidBodyComponent>(entity);
            auto& sync = view.get<PhysicsSyncComponent>(entity);

            if (rigidBody.IsBodyCreated && sync.Enabled && rigidBody.Type != MotionType::Static) {
                SyncEntityTransformInterpolated(registry, entity, transform, rigidBody, sync, alpha);
                m_SyncedBodyCount++;
            }
        }

        // Also sync entities without PhysicsSyncComponent (no interpolation)
        auto viewNoSync = registry.view<TransformComponent, RigidBodyComponent>(entt::exclude<PhysicsSyncComponent>);
        for (auto entity : viewNoSync) {
            auto& transform = viewNoSync.get<TransformComponent>(entity);
            auto& rigidBody = viewNoSync.get<RigidBodyComponent>(entity);

            if (rigidBody.IsBodyCreated && rigidBody.Type != MotionType::Static) {
                SyncEntityTransform(registry, entity, transform, rigidBody);
                m_SyncedBodyCount++;
            }
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        m_LastSyncTime = std::chrono::duration<float>(endTime - startTime).count();
    }

    void PhysicsSyncSystem::StorePreviousState(Scene& scene)
    {
        PROFILE_FUNCTION();

        if (!m_InterpolationEnabled) {
            return;
        }

        auto& registry = scene.GetRegistry();

        // Store current state as previous for entities with PhysicsSyncComponent
        auto view = registry.view<TransformComponent, RigidBodyComponent, PhysicsSyncComponent>();
        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& sync = view.get<PhysicsSyncComponent>(entity);

            sync.PreviousPosition = transform.Position;
            sync.PreviousRotation = transform.Rotation;
        }
    }

    void PhysicsSyncSystem::SyncEntityTransform([[maybe_unused]] entt::registry& registry, 
                                                 [[maybe_unused]] entt::entity entity,
                                                 TransformComponent& transform, 
                                                 RigidBodyComponent& rigidBody)
    {
        PROFILE_FUNCTION();

        const JPH::BodyInterface& bodyInterface = m_PhysicsWorld->GetBodyInterface();

        // Get body position and rotation from Jolt
        JPH::RVec3 position = bodyInterface.GetPosition(rigidBody.BodyID);
        JPH::Quat rotation = bodyInterface.GetRotation(rigidBody.BodyID);

        // Check for PhysicsSyncComponent to determine sync mode
        PhysicsSyncMode mode = PhysicsSyncMode::Full;
        if (registry.any_of<PhysicsSyncComponent>(entity)) {
            mode = registry.get<PhysicsSyncComponent>(entity).Mode;
        }

        // Update transform based on sync mode
        switch (mode) {
            case PhysicsSyncMode::Full:
                transform.Position = Math::Vec3(
                    static_cast<float>(position.GetX()),
                    static_cast<float>(position.GetY()),
                    static_cast<float>(position.GetZ())
                );
                transform.Rotation = QuaternionToEuler(rotation);
                break;

            case PhysicsSyncMode::PositionOnly:
                transform.Position = Math::Vec3(
                    static_cast<float>(position.GetX()),
                    static_cast<float>(position.GetY()),
                    static_cast<float>(position.GetZ())
                );
                break;

            case PhysicsSyncMode::RotationOnly:
                transform.Rotation = QuaternionToEuler(rotation);
                break;
        }

        // Update velocity from physics
        JPH::Vec3 linearVel = bodyInterface.GetLinearVelocity(rigidBody.BodyID);
        JPH::Vec3 angularVel = bodyInterface.GetAngularVelocity(rigidBody.BodyID);

        rigidBody.LinearVelocity = Math::Vec3(linearVel.GetX(), linearVel.GetY(), linearVel.GetZ());
        rigidBody.AngularVelocity = Math::Vec3(angularVel.GetX(), angularVel.GetY(), angularVel.GetZ());

        // Mark transform as dirty so TransformSystem will recalculate world matrix
        transform.SetDirty();
    }

    void PhysicsSyncSystem::SyncEntityTransformInterpolated([[maybe_unused]] entt::registry& registry,
                                                            [[maybe_unused]] entt::entity entity,
                                                            TransformComponent& transform,
                                                            RigidBodyComponent& rigidBody,
                                                            PhysicsSyncComponent& sync,
                                                            float alpha)
    {
        PROFILE_FUNCTION();

        const JPH::BodyInterface& bodyInterface = m_PhysicsWorld->GetBodyInterface();

        // Get current body position and rotation
        JPH::RVec3 currentPos = bodyInterface.GetPosition(rigidBody.BodyID);
        JPH::Quat currentRot = bodyInterface.GetRotation(rigidBody.BodyID);

        Math::Vec3 physicsPosition(
            static_cast<float>(currentPos.GetX()),
            static_cast<float>(currentPos.GetY()),
            static_cast<float>(currentPos.GetZ())
        );
        Math::Vec3 physicsRotation = QuaternionToEuler(currentRot);

        // Store interpolation alpha
        sync.InterpolationAlpha = alpha;

        // Interpolate between previous and current state
        switch (sync.Mode) {
            case PhysicsSyncMode::Full:
                transform.Position = glm::mix(sync.PreviousPosition, physicsPosition, alpha);
                // Use shortest path interpolation for rotation
                transform.Rotation = glm::mix(sync.PreviousRotation, physicsRotation, alpha);
                break;

            case PhysicsSyncMode::PositionOnly:
                transform.Position = glm::mix(sync.PreviousPosition, physicsPosition, alpha);
                break;

            case PhysicsSyncMode::RotationOnly:
                transform.Rotation = glm::mix(sync.PreviousRotation, physicsRotation, alpha);
                break;
        }

        // Update velocity (no interpolation needed for velocity)
        JPH::Vec3 linearVel = bodyInterface.GetLinearVelocity(rigidBody.BodyID);
        JPH::Vec3 angularVel = bodyInterface.GetAngularVelocity(rigidBody.BodyID);

        rigidBody.LinearVelocity = Math::Vec3(linearVel.GetX(), linearVel.GetY(), linearVel.GetZ());
        rigidBody.AngularVelocity = Math::Vec3(angularVel.GetX(), angularVel.GetY(), angularVel.GetZ());

        transform.SetDirty();
    }

    Math::Vec3 PhysicsSyncSystem::QuaternionToEuler(const JPH::Quat& quat)
    {
        // Convert Jolt quaternion to GLM quaternion
        glm::quat glmQuat(quat.GetW(), quat.GetX(), quat.GetY(), quat.GetZ());

        // Extract Euler angles (returns in radians)
        Math::Vec3 euler = glm::eulerAngles(glmQuat);

        return euler;
    }

} // namespace ECS
} // namespace Core
