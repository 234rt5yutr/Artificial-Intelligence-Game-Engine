#pragma once

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/CameraComponent.h"
#include "Core/ECS/Components/FirstPersonCameraComponent.h"
#include "Core/ECS/Components/ThirdPersonCameraComponent.h"
#include "Core/ECS/Components/CameraViewInterpolatorComponent.h"
#include "Core/Profile.h"
#include "Core/Log.h"

namespace Core {
namespace ECS {

    class CameraViewInterpolatorSystem {
    public:
        CameraViewInterpolatorSystem() = default;
        ~CameraViewInterpolatorSystem() = default;

        // Initialize the system
        void Initialize();

        // Update interpolators (call before FP/TP camera systems)
        void Update(Scene& scene, float deltaTime);

        // Handle input for view mode toggle (call from input handling)
        void HandleViewToggleInput(Scene& scene, bool togglePressed);

    private:
        // Process transition requests
        void ProcessTransitionRequests(
            entt::registry& registry,
            CameraViewInterpolatorComponent& interp,
            float deltaTime);

        // Update ongoing transitions
        void UpdateTransition(
            entt::registry& registry,
            CameraViewInterpolatorComponent& interp,
            TransformComponent& cameraTransform,
            CameraComponent& camera,
            float deltaTime);

        // Calculate first-person camera state
        void CalculateFPState(
            entt::registry& registry,
            CameraViewInterpolatorComponent& interp,
            Math::Vec3& outPosition,
            Math::Vec3& outRotation,
            float& outFOV);

        // Calculate third-person camera state
        void CalculateTPState(
            entt::registry& registry,
            CameraViewInterpolatorComponent& interp,
            Math::Vec3& outPosition,
            Math::Vec3& outRotation,
            float& outFOV);

        // Sync FP/TP component states based on current mode
        void SyncComponentStates(
            entt::registry& registry,
            CameraViewInterpolatorComponent& interp);

    private:
        bool m_Initialized = false;
    };

} // namespace ECS
} // namespace Core
