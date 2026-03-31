#pragma once

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/CameraComponent.h"
#include "Core/ECS/Components/PlayerControllerComponent.h"
#include "Core/ECS/Components/CharacterControllerComponent.h"
#include "Core/ECS/Components/FirstPersonCameraComponent.h"
#include "Core/Profile.h"
#include "Core/Log.h"

namespace Core {
namespace ECS {

    class FirstPersonCameraSystem {
    public:
        FirstPersonCameraSystem() = default;
        ~FirstPersonCameraSystem() = default;

        // Update first-person cameras (call after player controller update, before camera system)
        void Update(Scene& scene, float deltaTime);

    private:
        // Update a single first-person camera
        void UpdateCamera(
            entt::registry& registry,
            FirstPersonCameraComponent& fpCamera,
            CameraComponent& camera,
            TransformComponent& cameraTransform,
            float deltaTime);
    };

} // namespace ECS
} // namespace Core
