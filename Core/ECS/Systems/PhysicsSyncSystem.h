#pragma once

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/RigidBodyComponent.h"
#include "Core/Physics/PhysicsWorld.h"
#include "Core/Profile.h"
#include "Core/Log.h"

namespace Core {
namespace ECS {

    // Synchronization mode for physics-to-transform updates
    enum class PhysicsSyncMode : uint8_t {
        Full,           // Sync both position and rotation
        PositionOnly,   // Sync only position (useful for constrained objects)
        RotationOnly    // Sync only rotation
    };

    // Optional component to customize sync behavior per-entity
    struct PhysicsSyncComponent {
        PhysicsSyncMode Mode = PhysicsSyncMode::Full;
        bool Enabled = true;
        
        // Interpolation factor for smooth rendering (0.0 = previous, 1.0 = current)
        float InterpolationAlpha = 1.0f;
        
        // Previous frame data for interpolation
        Math::Vec3 PreviousPosition{ 0.0f, 0.0f, 0.0f };
        Math::Vec3 PreviousRotation{ 0.0f, 0.0f, 0.0f };
        
        PhysicsSyncComponent() = default;
        explicit PhysicsSyncComponent(PhysicsSyncMode mode) : Mode(mode) {}
    };

    // System that syncs Jolt physics body transforms back to ECS TransformComponents
    class PhysicsSyncSystem {
    public:
        PhysicsSyncSystem();
        ~PhysicsSyncSystem();

        // Initialize with physics world reference
        void Initialize(Physics::PhysicsWorld* physicsWorld);

        // Shutdown and cleanup
        void Shutdown();

        // Update transforms from physics bodies (called after physics step)
        void Update(Scene& scene);

        // Update with interpolation (for rendering between physics steps)
        void UpdateInterpolated(Scene& scene, float alpha);

        // Store previous frame state (call before physics step)
        void StorePreviousState(Scene& scene);

        // Statistics
        uint32_t GetSyncedBodyCount() const { return m_SyncedBodyCount; }
        float GetLastSyncTime() const { return m_LastSyncTime; }

        // Configuration
        void SetInterpolationEnabled(bool enabled) { m_InterpolationEnabled = enabled; }
        bool IsInterpolationEnabled() const { return m_InterpolationEnabled; }

    private:
        // Sync a single entity's transform from its physics body
        void SyncEntityTransform(entt::registry& registry, entt::entity entity,
                                 TransformComponent& transform, RigidBodyComponent& rigidBody);

        // Sync with interpolation
        void SyncEntityTransformInterpolated(entt::registry& registry, entt::entity entity,
                                             TransformComponent& transform, RigidBodyComponent& rigidBody,
                                             PhysicsSyncComponent& sync, float alpha);

        // Convert Jolt quaternion to Euler angles
        Math::Vec3 QuaternionToEuler(const JPH::Quat& quat);

    private:
        Physics::PhysicsWorld* m_PhysicsWorld = nullptr;

        // Statistics
        uint32_t m_SyncedBodyCount = 0;
        float m_LastSyncTime = 0.0f;

        // Configuration
        bool m_InterpolationEnabled = false;
    };

} // namespace ECS
} // namespace Core
