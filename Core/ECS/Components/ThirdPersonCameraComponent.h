#pragma once

#include "Core/Math/Math.h"
#include "Core/ECS/Components/FirstPersonCameraComponent.h"
#include <entt/entt.hpp>

namespace Core {
namespace ECS {

    struct ThirdPersonCameraComponent {
        // The player entity this camera follows
        entt::entity TargetEntity = entt::null;

        // Spring arm settings
        float ArmLength = 4.0f;            // Desired distance from target
        float MinArmLength = 0.5f;         // Minimum distance (collision compressed)
        float MaxArmLength = 10.0f;        // Maximum distance (zoom out limit)
        float CurrentArmLength = 4.0f;     // Current interpolated length

        // Target offset (where the camera looks at relative to player position)
        Math::Vec3 TargetOffset{ 0.0f, 1.5f, 0.0f };  // Look at player's head/shoulder area

        // Arm attachment offset (where arm pivots from, relative to target)
        Math::Vec3 PivotOffset{ 0.0f, 1.7f, 0.0f };

        // Arm rotation (controlled by player input)
        float Yaw = 0.0f;                  // Horizontal rotation (degrees)
        float Pitch = -15.0f;              // Vertical rotation (degrees), slightly looking down
        float MinPitch = -80.0f;           // Looking up limit
        float MaxPitch = 80.0f;            // Looking down limit

        // Camera collision settings
        float CollisionRadius = 0.2f;      // Radius of camera collision sphere
        float CollisionPushSpeed = 15.0f;  // How fast camera moves when colliding
        float CollisionRecoverSpeed = 5.0f; // How fast camera returns to desired position
        bool EnableCollision = true;

        // Lag/smoothing settings
        float PositionLagSpeed = 10.0f;    // How fast camera follows target position
        float RotationLagSpeed = 8.0f;     // How fast camera rotation follows input
        bool EnablePositionLag = true;
        bool EnableRotationLag = true;

        // Current smoothed values
        Math::Vec3 CurrentPivotPosition{ 0.0f };
        float CurrentYaw = 0.0f;
        float CurrentPitch = -15.0f;

        // Camera shake (for impacts, explosions)
        float ShakeIntensity = 0.0f;
        float ShakeDecay = 5.0f;
        Math::Vec3 CurrentShakeOffset{ 0.0f };

        // FOV settings
        float BaseFOV = 60.0f;
        float ZoomFOV = 30.0f;             // FOV when aiming/zooming
        float CurrentFOV = 60.0f;
        float FOVTransitionSpeed = 8.0f;
        bool IsZooming = false;

        // View mode
        CameraViewMode ViewMode = CameraViewMode::ThirdPerson;

        // State
        bool IsActive = true;

        ThirdPersonCameraComponent() = default;

        // Factory method
        static ThirdPersonCameraComponent Create(
            entt::entity target, 
            float armLength = 4.0f,
            const Math::Vec3& targetOffset = Math::Vec3(0.0f, 1.5f, 0.0f))
        {
            ThirdPersonCameraComponent tp;
            tp.TargetEntity = target;
            tp.ArmLength = armLength;
            tp.CurrentArmLength = armLength;
            tp.TargetOffset = targetOffset;
            return tp;
        }

        // Apply look input (from mouse/gamepad)
        void ApplyLookDelta(float deltaYaw, float deltaPitch, float sensitivity = 1.0f)
        {
            Yaw += deltaYaw * sensitivity;
            Pitch += deltaPitch * sensitivity;

            // Wrap yaw
            if (Yaw > 180.0f) Yaw -= 360.0f;
            if (Yaw < -180.0f) Yaw += 360.0f;

            // Clamp pitch
            Pitch = glm::clamp(Pitch, MinPitch, MaxPitch);
        }

        // Set zoom state
        void SetZooming(bool zooming)
        {
            IsZooming = zooming;
        }

        // Apply camera shake
        void ApplyShake(float intensity)
        {
            ShakeIntensity = glm::max(ShakeIntensity, intensity);
        }

        // Get the arm direction vector from current yaw/pitch
        Math::Vec3 GetArmDirection() const
        {
            float yawRad = glm::radians(CurrentYaw);
            float pitchRad = glm::radians(CurrentPitch);

            // Calculate direction (camera is behind and above target)
            return Math::Vec3(
                -sin(yawRad) * cos(pitchRad),
                -sin(pitchRad),
                -cos(yawRad) * cos(pitchRad)
            );
        }

        // Get the desired camera position (without collision)
        Math::Vec3 GetDesiredCameraPosition(const Math::Vec3& targetPosition) const
        {
            Math::Vec3 pivotPos = targetPosition + PivotOffset;
            Math::Vec3 armDir = GetArmDirection();
            return pivotPos - armDir * ArmLength;
        }

        // Update lag smoothing for rotation
        void UpdateRotationLag(float deltaTime)
        {
            if (EnableRotationLag) {
                float t = 1.0f - glm::exp(-RotationLagSpeed * deltaTime);
                CurrentYaw = glm::mix(CurrentYaw, Yaw, t);
                CurrentPitch = glm::mix(CurrentPitch, Pitch, t);
            }
            else {
                CurrentYaw = Yaw;
                CurrentPitch = Pitch;
            }
        }

        // Update position lag
        void UpdatePositionLag(const Math::Vec3& targetPivot, float deltaTime)
        {
            if (EnablePositionLag) {
                float t = 1.0f - glm::exp(-PositionLagSpeed * deltaTime);
                CurrentPivotPosition = glm::mix(CurrentPivotPosition, targetPivot, t);
            }
            else {
                CurrentPivotPosition = targetPivot;
            }
        }

        // Update camera shake
        void UpdateShake(float deltaTime)
        {
            if (ShakeIntensity > 0.001f) {
                // Generate random shake offset
                CurrentShakeOffset = Math::Vec3(
                    (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f * ShakeIntensity,
                    (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f * ShakeIntensity,
                    (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f * ShakeIntensity
                );

                // Decay shake
                ShakeIntensity *= glm::exp(-ShakeDecay * deltaTime);
            }
            else {
                ShakeIntensity = 0.0f;
                CurrentShakeOffset = Math::Vec3(0.0f);
            }
        }

        // Update FOV based on zoom state
        void UpdateFOV(float deltaTime)
        {
            float targetFOV = IsZooming ? ZoomFOV : BaseFOV;
            float t = 1.0f - glm::exp(-FOVTransitionSpeed * deltaTime);
            CurrentFOV = glm::mix(CurrentFOV, targetFOV, t);
        }

        // Update arm length (for collision or zoom)
        void UpdateArmLength(float targetLength, float speed, float deltaTime)
        {
            float clampedTarget = glm::clamp(targetLength, MinArmLength, MaxArmLength);
            float t = 1.0f - glm::exp(-speed * deltaTime);
            CurrentArmLength = glm::mix(CurrentArmLength, clampedTarget, t);
        }

        // Get look direction (from camera position to target)
        Math::Vec3 GetLookDirection(const Math::Vec3& cameraPosition, const Math::Vec3& targetPosition) const
        {
            Math::Vec3 lookTarget = targetPosition + TargetOffset;
            return glm::normalize(lookTarget - cameraPosition);
        }

        // Get rotation to look at target (returns pitch, yaw, roll in radians)
        Math::Vec3 GetLookAtRotation(const Math::Vec3& cameraPosition, const Math::Vec3& targetPosition) const
        {
            Math::Vec3 direction = GetLookDirection(cameraPosition, targetPosition);
            
            float pitch = asin(direction.y);
            float yaw = atan2(-direction.x, -direction.z);

            return Math::Vec3(pitch, yaw, 0.0f);
        }
    };

} // namespace ECS
} // namespace Core
