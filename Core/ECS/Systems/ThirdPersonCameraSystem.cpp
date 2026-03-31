#include "Core/ECS/Systems/ThirdPersonCameraSystem.h"
#include "Core/ECS/Components/PlayerControllerComponent.h"
#include "Core/ECS/Components/CharacterControllerComponent.h"
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>

namespace Core {
namespace ECS {

    void ThirdPersonCameraSystem::Initialize(Physics::PhysicsWorld* physicsWorld)
    {
        m_PhysicsWorld = physicsWorld;
        ENGINE_CORE_INFO("ThirdPersonCameraSystem initialized");
    }

    void ThirdPersonCameraSystem::Update(Scene& scene, float deltaTime)
    {
        PROFILE_FUNCTION();

        auto& registry = scene.GetRegistry();

        // Process all entities with third-person camera components
        auto view = registry.view<ThirdPersonCameraComponent, CameraComponent, TransformComponent>();
        for (auto entity : view) {
            auto& tpCamera = view.get<ThirdPersonCameraComponent>(entity);
            auto& camera = view.get<CameraComponent>(entity);
            auto& cameraTransform = view.get<TransformComponent>(entity);

            if (tpCamera.IsActive && tpCamera.ViewMode == CameraViewMode::ThirdPerson) {
                UpdateCamera(registry, tpCamera, camera, cameraTransform, deltaTime);
            }
        }
    }

    void ThirdPersonCameraSystem::UpdateCamera(
        entt::registry& registry,
        ThirdPersonCameraComponent& tpCamera,
        CameraComponent& camera,
        TransformComponent& cameraTransform,
        float deltaTime)
    {
        PROFILE_FUNCTION();

        // Validate target entity
        if (tpCamera.TargetEntity == entt::null || !registry.valid(tpCamera.TargetEntity)) {
            ENGINE_CORE_WARN("ThirdPersonCameraSystem: Invalid target entity");
            return;
        }

        // Get target transform
        auto* targetTransform = registry.try_get<TransformComponent>(tpCamera.TargetEntity);
        if (!targetTransform) {
            ENGINE_CORE_WARN("ThirdPersonCameraSystem: Target missing TransformComponent");
            return;
        }

        // Update rotation lag
        tpCamera.UpdateRotationLag(deltaTime);

        // Calculate pivot position (where the arm attaches to the player)
        Math::Vec3 targetPivot = targetTransform->Position + tpCamera.PivotOffset;

        // Update position lag
        tpCamera.UpdatePositionLag(targetPivot, deltaTime);

        // Get arm direction from current rotation
        Math::Vec3 armDirection = tpCamera.GetArmDirection();

        // Calculate desired camera position
        Math::Vec3 desiredPosition = tpCamera.CurrentPivotPosition - armDirection * tpCamera.ArmLength;

        // Perform collision detection
        float actualArmLength = tpCamera.ArmLength;
        if (tpCamera.EnableCollision && m_PhysicsWorld && m_PhysicsWorld->IsInitialized()) {
            actualArmLength = PerformSpringArmRaycast(
                tpCamera.CurrentPivotPosition,
                desiredPosition,
                tpCamera.ArmLength,
                tpCamera.CollisionRadius,
                tpCamera.TargetEntity
            );

            // Update arm length with appropriate speed
            if (actualArmLength < tpCamera.CurrentArmLength) {
                // Collision detected - move quickly
                tpCamera.UpdateArmLength(actualArmLength, tpCamera.CollisionPushSpeed, deltaTime);
            }
            else {
                // No collision - recover slowly
                tpCamera.UpdateArmLength(tpCamera.ArmLength, tpCamera.CollisionRecoverSpeed, deltaTime);
            }
        }
        else {
            tpCamera.CurrentArmLength = tpCamera.ArmLength;
        }

        // Calculate final camera position using current (possibly collision-adjusted) arm length
        Math::Vec3 cameraPosition = tpCamera.CurrentPivotPosition - armDirection * tpCamera.CurrentArmLength;

        // Update camera shake
        tpCamera.UpdateShake(deltaTime);
        cameraPosition += tpCamera.CurrentShakeOffset;

        // Update FOV
        tpCamera.UpdateFOV(deltaTime);

        // Set camera transform
        cameraTransform.Position = cameraPosition;

        // Calculate rotation to look at target
        Math::Vec3 lookAtRotation = tpCamera.GetLookAtRotation(cameraPosition, targetTransform->Position);
        cameraTransform.Rotation = lookAtRotation;
        cameraTransform.SetDirty();

        // Update camera FOV if changed
        if (camera.FieldOfView != tpCamera.CurrentFOV) {
            camera.SetFieldOfView(tpCamera.CurrentFOV);
        }

        ENGINE_CORE_TRACE("ThirdPersonCamera: pos=({:.2f}, {:.2f}, {:.2f}), armLen={:.2f}, yaw={:.1f}, pitch={:.1f}",
                          cameraPosition.x, cameraPosition.y, cameraPosition.z,
                          tpCamera.CurrentArmLength, tpCamera.CurrentYaw, tpCamera.CurrentPitch);
    }

    float ThirdPersonCameraSystem::PerformSpringArmRaycast(
        const Math::Vec3& start,
        const Math::Vec3& end,
        float armLength,
        float collisionRadius,
        entt::entity ignoreEntity)
    {
        PROFILE_FUNCTION();

        if (!m_PhysicsWorld || !m_PhysicsWorld->IsInitialized()) {
            return armLength;
        }

        JPH::PhysicsSystem& physicsSystem = m_PhysicsWorld->GetPhysicsSystem();
        const JPH::NarrowPhaseQuery& narrowPhase = physicsSystem.GetNarrowPhaseQuery();

        // Convert to Jolt types
        JPH::RVec3 rayStart(start.x, start.y, start.z);
        JPH::Vec3 direction = JPH::Vec3(end.x - start.x, end.y - start.y, end.z - start.z);
        float dirLength = direction.Length();
        
        if (dirLength < 0.001f) {
            return armLength;
        }

        direction /= dirLength;  // Normalize

        // Use sphere cast for smoother collision detection
        JPH::SphereShape sphereShape(collisionRadius);

        // Create shape cast
        JPH::RShapeCast shapeCast = JPH::RShapeCast::sFromWorldTransform(
            &sphereShape,
            JPH::Vec3::sReplicate(1.0f),  // Scale
            JPH::RMat44::sTranslation(rayStart),
            direction * armLength
        );

        // Set up collision filtering
        JPH::DefaultBroadPhaseLayerFilter broadPhaseFilter(
            physicsSystem.GetObjectVsBroadPhaseLayerFilter(),
            Physics::Layers::MOVING
        );

        JPH::DefaultObjectLayerFilter objectFilter(
            physicsSystem.GetObjectLayerPairFilter(),
            Physics::Layers::MOVING
        );

        // Body filter to ignore the player
        class IgnoreEntityBodyFilter : public JPH::BodyFilter {
        public:
            IgnoreEntityBodyFilter(uint64_t ignoreUserData) : m_IgnoreUserData(ignoreUserData) {}

            bool ShouldCollide([[maybe_unused]] const JPH::BodyID& inBodyID) const override
            {
                return true;  // Allow all bodies initially
            }

            bool ShouldCollideLocked(const JPH::Body& inBody) const override
            {
                // Ignore the player entity
                return inBody.GetUserData() != m_IgnoreUserData;
            }

        private:
            uint64_t m_IgnoreUserData;
        };

        IgnoreEntityBodyFilter bodyFilter(static_cast<uint64_t>(ignoreEntity));

        // Perform shape cast
        JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
        
        narrowPhase.CastShape(
            shapeCast,
            JPH::ShapeCastSettings(),
            JPH::RVec3::sZero(),  // Base offset
            collector,
            broadPhaseFilter,
            objectFilter,
            bodyFilter
        );

        if (collector.HadHit()) {
            // Hit something - return the collision distance
            float hitFraction = collector.mHit.mFraction;
            float hitDistance = armLength * hitFraction;

            // Add small offset to prevent camera from being exactly at collision point
            hitDistance = glm::max(hitDistance - 0.1f, 0.1f);

            ENGINE_CORE_TRACE("SpringArm: Collision at fraction {:.3f}, distance {:.2f}", hitFraction, hitDistance);
            return hitDistance;
        }

        // No collision - return full arm length
        return armLength;
    }

} // namespace ECS
} // namespace Core
