#pragma once

#include "Core/Math/Math.h"

namespace Core {
namespace ECS {

    struct PlayerControllerComponent {
        // Movement speeds
        float WalkSpeed = 4.0f;          // m/s
        float SprintSpeed = 7.0f;        // m/s
        float CrouchSpeed = 2.0f;        // m/s
        float JumpVelocity = 5.0f;       // m/s (vertical)

        // Look settings
        float LookSensitivity = 1.0f;
        float MinPitch = -89.0f;         // Degrees
        float MaxPitch = 89.0f;          // Degrees

        // Current look angles (in degrees)
        float Yaw = 0.0f;
        float Pitch = 0.0f;

        // State flags
        bool IsSprinting = false;
        bool IsCrouching = false;
        bool IsJumping = false;
        bool CanJump = true;

        // Which player index this controller uses (for local multiplayer)
        int PlayerIndex = 0;

        // Enable/disable input processing
        bool InputEnabled = true;

        PlayerControllerComponent() = default;

        // Get the current movement speed based on state
        float GetCurrentSpeed() const
        {
            if (IsCrouching) return CrouchSpeed;
            if (IsSprinting) return SprintSpeed;
            return WalkSpeed;
        }

        // Get forward direction from yaw (horizontal only)
        Math::Vec3 GetForwardDirection() const
        {
            float yawRad = glm::radians(Yaw);
            return Math::Vec3(
                -sin(yawRad),
                0.0f,
                -cos(yawRad)
            );
        }

        // Get right direction from yaw (horizontal only)
        Math::Vec3 GetRightDirection() const
        {
            float yawRad = glm::radians(Yaw);
            return Math::Vec3(
                cos(yawRad),
                0.0f,
                -sin(yawRad)
            );
        }

        // Get full look direction including pitch
        Math::Vec3 GetLookDirection() const
        {
            float yawRad = glm::radians(Yaw);
            float pitchRad = glm::radians(Pitch);
            return Math::Vec3(
                -sin(yawRad) * cos(pitchRad),
                sin(pitchRad),
                -cos(yawRad) * cos(pitchRad)
            );
        }

        // Apply look delta (mouse/stick input)
        void ApplyLookDelta(float deltaYaw, float deltaPitch)
        {
            Yaw += deltaYaw * LookSensitivity;
            Pitch += deltaPitch * LookSensitivity;

            // Wrap yaw
            if (Yaw > 180.0f) Yaw -= 360.0f;
            if (Yaw < -180.0f) Yaw += 360.0f;

            // Clamp pitch
            Pitch = glm::clamp(Pitch, MinPitch, MaxPitch);
        }

        // Get rotation as euler angles (radians) for TransformComponent
        Math::Vec3 GetRotationRadians() const
        {
            return Math::Vec3(
                glm::radians(Pitch),
                glm::radians(Yaw),
                0.0f
            );
        }
    };

} // namespace ECS
} // namespace Core
