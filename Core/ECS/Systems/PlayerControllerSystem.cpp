#include "Core/ECS/Systems/PlayerControllerSystem.h"

namespace Core {
namespace ECS {

    PlayerControllerSystem::PlayerControllerSystem() = default;

    void PlayerControllerSystem::Initialize(InputMapper* inputMapper)
    {
        m_InputMapper = inputMapper;
        ENGINE_CORE_INFO("PlayerControllerSystem initialized");
    }

    void PlayerControllerSystem::Update(Scene& scene, float deltaTime)
    {
        PROFILE_FUNCTION();

        if (!m_InputMapper) {
            return;
        }

        auto& registry = scene.GetRegistry();

        // Process all entities with player controller, character controller, and transform
        auto view = registry.view<PlayerControllerComponent, CharacterControllerComponent, TransformComponent>();
        for (auto entity : view) {
            auto& player = view.get<PlayerControllerComponent>(entity);
            auto& character = view.get<CharacterControllerComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);

            if (player.InputEnabled) {
                ProcessPlayerInput(player, character, transform, deltaTime);
            }
        }
    }

    void PlayerControllerSystem::ProcessPlayerInput(
        PlayerControllerComponent& player,
        CharacterControllerComponent& character,
        TransformComponent& transform,
        [[maybe_unused]] float deltaTime)
    {
        PROFILE_FUNCTION();

        // Process look input
        Math::Vec2 lookDelta = m_InputMapper->GetLookDelta();
        player.ApplyLookDelta(lookDelta.x, -lookDelta.y);  // Invert Y for natural mouse feel

        // Update transform rotation (yaw only for character, pitch is for camera)
        transform.Rotation.y = glm::radians(player.Yaw);
        transform.SetDirty();

        // Process movement input
        Math::Vec3 inputMove = m_InputMapper->GetMovementVector();

        // Check sprint/crouch state
        player.IsSprinting = m_InputMapper->IsActionPressed(InputActions::Sprint);
        player.IsCrouching = m_InputMapper->IsActionPressed(InputActions::Crouch);

        // Can't sprint while crouching
        if (player.IsCrouching) {
            player.IsSprinting = false;
        }

        // Get movement speed
        float speed = player.GetCurrentSpeed();

        // Calculate world-space velocity based on player facing direction
        Math::Vec3 forward = player.GetForwardDirection();
        Math::Vec3 right = player.GetRightDirection();

        Math::Vec3 velocity = 
            forward * (-inputMove.z) +  // Forward/backward (inputMove.z is -forward)
            right * inputMove.x;         // Left/right

        // Normalize and apply speed
        if (glm::length(velocity) > 0.001f) {
            velocity = glm::normalize(velocity) * speed;
        }

        // Handle jumping
        if (m_InputMapper->IsActionJustPressed(InputActions::Jump) && character.CanJump()) {
            velocity.y = player.JumpVelocity;
            player.IsJumping = true;
            player.CanJump = false;
        }
        else {
            player.IsJumping = false;
        }

        // Reset jump ability when landing
        if (character.IsOnGround() && !m_InputMapper->IsActionPressed(InputActions::Jump)) {
            player.CanJump = true;
        }

        // Apply velocity to character controller
        character.SetDesiredVelocity(velocity);

        ENGINE_CORE_TRACE("PlayerController: vel=({:.2f}, {:.2f}, {:.2f}), yaw={:.1f}, pitch={:.1f}, sprint={}, crouch={}",
                          velocity.x, velocity.y, velocity.z,
                          player.Yaw, player.Pitch,
                          player.IsSprinting, player.IsCrouching);
    }

} // namespace ECS
} // namespace Core
