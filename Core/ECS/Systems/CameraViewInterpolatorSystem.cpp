#include "Core/ECS/Systems/CameraViewInterpolatorSystem.h"
#include "Core/ECS/Components/PlayerControllerComponent.h"

namespace Core {
namespace ECS {

    void CameraViewInterpolatorSystem::Initialize()
    {
        m_Initialized = true;
        ENGINE_CORE_INFO("CameraViewInterpolatorSystem initialized");
    }

    void CameraViewInterpolatorSystem::Update(Scene& scene, float deltaTime)
    {
        PROFILE_FUNCTION();

        auto& registry = scene.GetRegistry();

        // Process all entities with camera view interpolators
        auto view = registry.view<CameraViewInterpolatorComponent>();
        for (auto entity : view) {
            auto& interp = view.get<CameraViewInterpolatorComponent>(entity);

            // Get camera entity's components
            if (interp.CameraEntity == entt::null || !registry.valid(interp.CameraEntity)) {
                continue;
            }

            auto* cameraTransform = registry.try_get<TransformComponent>(interp.CameraEntity);
            auto* camera = registry.try_get<CameraComponent>(interp.CameraEntity);

            if (!cameraTransform || !camera) {
                continue;
            }

            // Handle completion acknowledgment from previous frame
            interp.AcknowledgeCompletion();

            // Process any transition requests
            ProcessTransitionRequests(registry, interp, deltaTime);

            // Update ongoing transitions
            if (interp.IsTransitioning()) {
                UpdateTransition(registry, interp, *cameraTransform, *camera, deltaTime);
            }

            // Sync component states
            SyncComponentStates(registry, interp);
        }
    }

    void CameraViewInterpolatorSystem::HandleViewToggleInput(Scene& scene, bool togglePressed)
    {
        if (!togglePressed) {
            return;
        }

        auto& registry = scene.GetRegistry();

        auto view = registry.view<CameraViewInterpolatorComponent>();
        for (auto entity : view) {
            auto& interp = view.get<CameraViewInterpolatorComponent>(entity);

            // Don't allow toggle while already transitioning
            if (!interp.IsTransitioning()) {
                interp.ToggleViewMode();
                ENGINE_CORE_INFO("View mode toggle requested: {} -> {}",
                    interp.CurrentMode == CameraViewMode::FirstPerson ? "TP" : "FP",
                    interp.CurrentMode == CameraViewMode::FirstPerson ? "FP" : "TP");
            }
        }
    }

    void CameraViewInterpolatorSystem::ProcessTransitionRequests(
        entt::registry& registry,
        CameraViewInterpolatorComponent& interp,
        [[maybe_unused]] float deltaTime)
    {
        PROFILE_FUNCTION();

        // Check for pending requests
        if (!interp.RequestTransitionToFP && !interp.RequestTransitionToTP) {
            return;
        }

        // Don't start new transition if already transitioning
        if (interp.IsTransitioning()) {
            interp.RequestTransitionToFP = false;
            interp.RequestTransitionToTP = false;
            return;
        }

        CameraViewMode targetMode = interp.RequestTransitionToFP 
            ? CameraViewMode::FirstPerson 
            : CameraViewMode::ThirdPerson;

        // Calculate start and end states
        Math::Vec3 startPos, endPos;
        Math::Vec3 startRot, endRot;
        float startFOV, endFOV;

        // Get current camera state as start
        if (auto* cameraTransform = registry.try_get<TransformComponent>(interp.CameraEntity)) {
            startPos = cameraTransform->Position;
            startRot = cameraTransform->Rotation;
        }

        if (auto* camera = registry.try_get<CameraComponent>(interp.CameraEntity)) {
            startFOV = camera->FieldOfView;
        }
        else {
            startFOV = 70.0f;
        }

        // Calculate target state
        if (targetMode == CameraViewMode::FirstPerson) {
            CalculateFPState(registry, interp, endPos, endRot, endFOV);
        }
        else {
            CalculateTPState(registry, interp, endPos, endRot, endFOV);
        }

        // Begin the transition
        interp.BeginTransition(targetMode, startPos, endPos, startRot, endRot, startFOV, endFOV);

        ENGINE_CORE_INFO("Camera transition started: {} -> {} (duration: {:.2f}s)",
            targetMode == CameraViewMode::FirstPerson ? "TP" : "FP",
            targetMode == CameraViewMode::FirstPerson ? "FP" : "TP",
            interp.TransitionDuration);
    }

    void CameraViewInterpolatorSystem::UpdateTransition(
        entt::registry& registry,
        CameraViewInterpolatorComponent& interp,
        TransformComponent& cameraTransform,
        CameraComponent& camera,
        float deltaTime)
    {
        PROFILE_FUNCTION();

        // Update end positions in case player moved during transition
        Math::Vec3 currentEndPos, currentEndRot;
        float currentEndFOV;

        if (interp.CurrentMode == CameraViewMode::FirstPerson) {
            CalculateFPState(registry, interp, currentEndPos, currentEndRot, currentEndFOV);
        }
        else {
            CalculateTPState(registry, interp, currentEndPos, currentEndRot, currentEndFOV);
        }

        // Update end targets (allows camera to follow moving player during transition)
        interp.EndPosition = currentEndPos;
        interp.EndRotation = currentEndRot;
        interp.EndFOV = currentEndFOV;

        // Update the transition
        interp.UpdateTransition(deltaTime);

        // Apply interpolated values to camera
        cameraTransform.Position = interp.CurrentPosition;
        cameraTransform.Rotation = interp.CurrentRotation;
        cameraTransform.SetDirty();

        camera.SetFieldOfView(interp.CurrentFOV);

        ENGINE_CORE_TRACE("Camera transition: {:.1f}% complete, pos=({:.2f}, {:.2f}, {:.2f})",
            interp.TransitionProgress * 100.0f,
            interp.CurrentPosition.x, interp.CurrentPosition.y, interp.CurrentPosition.z);
    }

    void CameraViewInterpolatorSystem::CalculateFPState(
        entt::registry& registry,
        CameraViewInterpolatorComponent& interp,
        Math::Vec3& outPosition,
        Math::Vec3& outRotation,
        float& outFOV)
    {
        PROFILE_FUNCTION();

        // Default values
        outPosition = Math::Vec3(0.0f, 1.6f, 0.0f);
        outRotation = Math::Vec3(0.0f);
        outFOV = 70.0f;

        // Get player transform
        if (interp.PlayerEntity == entt::null || !registry.valid(interp.PlayerEntity)) {
            return;
        }

        auto* playerTransform = registry.try_get<TransformComponent>(interp.PlayerEntity);
        if (!playerTransform) {
            return;
        }

        // Try to get FirstPersonCameraComponent for settings
        auto* fpCamera = registry.try_get<FirstPersonCameraComponent>(interp.CameraEntity);
        if (fpCamera) {
            // Get player controller for rotation
            auto* playerController = registry.try_get<PlayerControllerComponent>(interp.PlayerEntity);
            
            // Calculate FP camera position
            outPosition = playerTransform->Position + fpCamera->CurrentEyeOffset;
            
            if (playerController) {
                outRotation = Math::Vec3(
                    glm::radians(playerController->Pitch),
                    glm::radians(playerController->Yaw),
                    0.0f
                );
            }
            else {
                outRotation = playerTransform->Rotation;
            }
            
            outFOV = fpCamera->GetCurrentFOV();
        }
        else {
            // Fallback: use player position with default eye height
            outPosition = playerTransform->Position + Math::Vec3(0.0f, 1.6f, 0.0f);
            outRotation = playerTransform->Rotation;
            outFOV = 70.0f;
        }
    }

    void CameraViewInterpolatorSystem::CalculateTPState(
        entt::registry& registry,
        CameraViewInterpolatorComponent& interp,
        Math::Vec3& outPosition,
        Math::Vec3& outRotation,
        float& outFOV)
    {
        PROFILE_FUNCTION();

        // Default values
        outPosition = Math::Vec3(0.0f, 3.0f, -5.0f);
        outRotation = Math::Vec3(0.0f);
        outFOV = 60.0f;

        // Get player transform
        if (interp.PlayerEntity == entt::null || !registry.valid(interp.PlayerEntity)) {
            return;
        }

        auto* playerTransform = registry.try_get<TransformComponent>(interp.PlayerEntity);
        if (!playerTransform) {
            return;
        }

        // Try to get ThirdPersonCameraComponent for settings
        auto* tpCamera = registry.try_get<ThirdPersonCameraComponent>(interp.CameraEntity);
        if (tpCamera) {
            // Calculate TP camera position using arm settings
            Math::Vec3 pivotPos = playerTransform->Position + tpCamera->PivotOffset;
            Math::Vec3 armDir = tpCamera->GetArmDirection();
            
            outPosition = pivotPos - armDir * tpCamera->CurrentArmLength;
            
            // Calculate look-at rotation
            outRotation = tpCamera->GetLookAtRotation(outPosition, playerTransform->Position);
            
            outFOV = tpCamera->CurrentFOV;
        }
        else {
            // Fallback: position behind and above player
            Math::Vec3 pivotPos = playerTransform->Position + Math::Vec3(0.0f, 1.7f, 0.0f);
            Math::Vec3 offset = Math::Vec3(0.0f, 1.0f, -4.0f);
            outPosition = pivotPos + offset;
            
            // Look at player
            Math::Vec3 toPlayer = glm::normalize(playerTransform->Position - outPosition);
            outRotation = Math::Vec3(
                asin(toPlayer.y),
                atan2(-toPlayer.x, -toPlayer.z),
                0.0f
            );
            
            outFOV = 60.0f;
        }
    }

    void CameraViewInterpolatorSystem::SyncComponentStates(
        entt::registry& registry,
        CameraViewInterpolatorComponent& interp)
    {
        PROFILE_FUNCTION();

        // Update FP and TP component active states based on current mode
        auto* fpCamera = registry.try_get<FirstPersonCameraComponent>(interp.CameraEntity);
        auto* tpCamera = registry.try_get<ThirdPersonCameraComponent>(interp.CameraEntity);

        bool isTransitioning = interp.IsTransitioning();
        bool isFPMode = interp.CurrentMode == CameraViewMode::FirstPerson;

        if (fpCamera) {
            // FP camera is active when in FP mode and NOT transitioning
            // During transition, the interpolator controls the camera
            fpCamera->IsActive = isFPMode && !isTransitioning;
            fpCamera->ViewMode = interp.CurrentMode;
        }

        if (tpCamera) {
            // TP camera is active when in TP mode and NOT transitioning
            tpCamera->IsActive = !isFPMode && !isTransitioning;
            tpCamera->ViewMode = interp.CurrentMode;
        }

        // Sync input blocking state
        if (auto* playerController = registry.try_get<PlayerControllerComponent>(interp.PlayerEntity)) {
            // Could add an IsInputBlocked field to PlayerControllerComponent if needed
            // For now, we just track the state in the interpolator
        }
    }

} // namespace ECS
} // namespace Core
