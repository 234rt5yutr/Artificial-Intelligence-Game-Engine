#pragma once

#include "Core/Math/Math.h"
#include <entt/entt.hpp>

namespace Core {
namespace ECS {

    // Camera view modes
    enum class CameraViewMode : uint8_t {
        FirstPerson = 0,
        ThirdPerson = 1
    };

    struct FirstPersonCameraComponent {
        // The player entity this camera is attached to
        entt::entity TargetEntity = entt::null;

        // Eye offset from player position (local space)
        // Typical: slightly forward and at eye height
        Math::Vec3 EyeOffset{ 0.0f, 1.6f, 0.0f };  // ~1.6m eye height for standing

        // Crouching eye offset
        Math::Vec3 CrouchEyeOffset{ 0.0f, 1.0f, 0.0f };  // ~1.0m for crouching

        // Current interpolated eye offset (for smooth crouch transitions)
        Math::Vec3 CurrentEyeOffset{ 0.0f, 1.6f, 0.0f };

        // Head bob settings
        bool EnableHeadBob = true;
        float HeadBobFrequency = 8.0f;     // Oscillations per second while walking
        float HeadBobAmplitudeY = 0.02f;   // Vertical bob amount
        float HeadBobAmplitudeX = 0.01f;   // Horizontal bob amount
        float HeadBobPhase = 0.0f;         // Current phase in bob cycle

        // Camera effects
        float ViewKickDecay = 10.0f;       // How fast view kick recovers
        Math::Vec2 CurrentViewKick{ 0.0f, 0.0f };  // Current kick offset (pitch, yaw)

        // FOV effects
        float BaseFOV = 70.0f;
        float SprintFOVBoost = 10.0f;      // Additional FOV when sprinting
        float CurrentFOVBoost = 0.0f;       // Interpolated FOV boost
        float FOVTransitionSpeed = 5.0f;

        // View mode
        CameraViewMode ViewMode = CameraViewMode::FirstPerson;

        // State
        bool IsActive = true;

        FirstPersonCameraComponent() = default;

        // Factory method
        static FirstPersonCameraComponent Create(entt::entity target, const Math::Vec3& eyeOffset = Math::Vec3(0.0f, 1.6f, 0.0f))
        {
            FirstPersonCameraComponent fp;
            fp.TargetEntity = target;
            fp.EyeOffset = eyeOffset;
            fp.CurrentEyeOffset = eyeOffset;
            return fp;
        }

        // Apply recoil/view kick (e.g., from firing weapon)
        void ApplyViewKick(float pitchKick, float yawKick)
        {
            CurrentViewKick.x += pitchKick;
            CurrentViewKick.y += yawKick;
        }

        // Get the final eye position given player position and crouch state
        Math::Vec3 GetEyePosition(const Math::Vec3& playerPosition, bool isCrouching, float deltaTime)
        {
            // Interpolate eye offset based on crouch state
            Math::Vec3 targetOffset = isCrouching ? CrouchEyeOffset : EyeOffset;
            float t = 1.0f - glm::exp(-8.0f * deltaTime);  // Smooth interpolation
            CurrentEyeOffset = glm::mix(CurrentEyeOffset, targetOffset, t);

            return playerPosition + CurrentEyeOffset;
        }

        // Get head bob offset based on movement
        Math::Vec3 GetHeadBobOffset(float speed, float deltaTime)
        {
            if (!EnableHeadBob || speed < 0.1f) {
                // Gradually return to center
                HeadBobPhase *= 0.9f;
                return Math::Vec3(0.0f);
            }

            // Advance phase based on movement speed
            HeadBobPhase += HeadBobFrequency * deltaTime * (speed / 4.0f);

            // Calculate bob offset
            float bobY = sin(HeadBobPhase * 2.0f) * HeadBobAmplitudeY;
            float bobX = sin(HeadBobPhase) * HeadBobAmplitudeX;

            return Math::Vec3(bobX, bobY, 0.0f);
        }

        // Update view kick decay
        void UpdateViewKick(float deltaTime)
        {
            float decay = glm::exp(-ViewKickDecay * deltaTime);
            CurrentViewKick *= decay;

            // Snap to zero when very small
            if (glm::length(CurrentViewKick) < 0.001f) {
                CurrentViewKick = Math::Vec2(0.0f);
            }
        }

        // Update FOV boost based on sprint state
        void UpdateFOVBoost(bool isSprinting, float deltaTime)
        {
            float targetBoost = isSprinting ? SprintFOVBoost : 0.0f;
            float t = 1.0f - glm::exp(-FOVTransitionSpeed * deltaTime);
            CurrentFOVBoost = glm::mix(CurrentFOVBoost, targetBoost, t);
        }

        // Get current FOV including effects
        float GetCurrentFOV() const
        {
            return BaseFOV + CurrentFOVBoost;
        }
    };

} // namespace ECS
} // namespace Core
