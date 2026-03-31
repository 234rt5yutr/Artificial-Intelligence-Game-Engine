#include "Core/ECS/Systems/FirstPersonCameraSystem.h"

namespace Core {
namespace ECS {

    void FirstPersonCameraSystem::Update(Scene& scene, float deltaTime)
    {
        PROFILE_FUNCTION();

        auto& registry = scene.GetRegistry();

        // Process all entities with first-person camera components
        auto view = registry.view<FirstPersonCameraComponent, CameraComponent, TransformComponent>();
        for (auto entity : view) {
            auto& fpCamera = view.get<FirstPersonCameraComponent>(entity);
            auto& camera = view.get<CameraComponent>(entity);
            auto& cameraTransform = view.get<TransformComponent>(entity);

            if (fpCamera.IsActive && fpCamera.ViewMode == CameraViewMode::FirstPerson) {
                UpdateCamera(registry, fpCamera, camera, cameraTransform, deltaTime);
            }
        }
    }

    void FirstPersonCameraSystem::UpdateCamera(
        entt::registry& registry,
        FirstPersonCameraComponent& fpCamera,
        CameraComponent& camera,
        TransformComponent& cameraTransform,
        float deltaTime)
    {
        PROFILE_FUNCTION();

        // Validate target entity
        if (fpCamera.TargetEntity == entt::null || !registry.valid(fpCamera.TargetEntity)) {
            ENGINE_CORE_WARN("FirstPersonCameraSystem: Invalid target entity");
            return;
        }

        // Get target player components
        auto* playerTransform = registry.try_get<TransformComponent>(fpCamera.TargetEntity);
        auto* playerController = registry.try_get<PlayerControllerComponent>(fpCamera.TargetEntity);
        auto* characterController = registry.try_get<CharacterControllerComponent>(fpCamera.TargetEntity);

        if (!playerTransform) {
            ENGINE_CORE_WARN("FirstPersonCameraSystem: Target entity missing TransformComponent");
            return;
        }

        // Get player state
        bool isCrouching = playerController ? playerController->IsCrouching : false;
        bool isSprinting = playerController ? playerController->IsSprinting : false;
        float moveSpeed = characterController ? glm::length(characterController->LinearVelocity) : 0.0f;

        // Calculate eye position
        Math::Vec3 eyePosition = fpCamera.GetEyePosition(playerTransform->Position, isCrouching, deltaTime);

        // Add head bob if moving on ground
        bool isOnGround = characterController ? characterController->IsOnGround() : true;
        if (isOnGround && moveSpeed > 0.1f) {
            Math::Vec3 bobOffset = fpCamera.GetHeadBobOffset(moveSpeed, deltaTime);
            
            // Transform bob offset to world space based on player yaw
            if (playerController) {
                float yawRad = glm::radians(playerController->Yaw);
                float cosYaw = cos(yawRad);
                float sinYaw = sin(yawRad);
                
                Math::Vec3 worldBob;
                worldBob.x = bobOffset.x * cosYaw;
                worldBob.y = bobOffset.y;
                worldBob.z = bobOffset.x * -sinYaw;
                
                eyePosition += worldBob;
            }
        }

        // Update view kick
        fpCamera.UpdateViewKick(deltaTime);

        // Update FOV boost
        fpCamera.UpdateFOVBoost(isSprinting, deltaTime);

        // Set camera transform
        cameraTransform.Position = eyePosition;

        // Get look angles from player controller
        float pitch = 0.0f;
        float yaw = 0.0f;

        if (playerController) {
            pitch = playerController->Pitch;
            yaw = playerController->Yaw;
        }

        // Apply view kick
        pitch += fpCamera.CurrentViewKick.x;
        yaw += fpCamera.CurrentViewKick.y;

        // Set camera rotation (pitch, yaw, roll)
        cameraTransform.Rotation = Math::Vec3(
            glm::radians(pitch),
            glm::radians(yaw),
            0.0f
        );
        cameraTransform.SetDirty();

        // Update camera FOV if changed
        float targetFOV = fpCamera.GetCurrentFOV();
        if (camera.FieldOfView != targetFOV) {
            camera.SetFieldOfView(targetFOV);
        }

        ENGINE_CORE_TRACE("FirstPersonCamera: pos=({:.2f}, {:.2f}, {:.2f}), pitch={:.1f}, yaw={:.1f}, fov={:.1f}",
                          eyePosition.x, eyePosition.y, eyePosition.z,
                          pitch, yaw, targetFOV);
    }

} // namespace ECS
} // namespace Core
