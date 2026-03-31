#pragma once

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/CharacterControllerComponent.h"
#include "Core/ECS/Components/PlayerControllerComponent.h"
#include "Core/InputMapper.h"
#include "Core/Profile.h"
#include "Core/Log.h"

namespace Core {
namespace ECS {

    class PlayerControllerSystem {
    public:
        PlayerControllerSystem();
        ~PlayerControllerSystem() = default;

        // Initialize with input mapper
        void Initialize(InputMapper* inputMapper);

        // Update player controllers (call before character controller update)
        void Update(Scene& scene, float deltaTime);

        // Get/set input mapper
        void SetInputMapper(InputMapper* mapper) { m_InputMapper = mapper; }
        InputMapper* GetInputMapper() const { return m_InputMapper; }

    private:
        void ProcessPlayerInput(
            PlayerControllerComponent& player,
            CharacterControllerComponent& character,
            TransformComponent& transform,
            float deltaTime);

    private:
        InputMapper* m_InputMapper = nullptr;
    };

} // namespace ECS
} // namespace Core
