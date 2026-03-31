#pragma once

#include "Core/Math/Math.h"
#include "Core/ECS/Components/FirstPersonCameraComponent.h"
#include <entt/entt.hpp>

namespace Core {
namespace ECS {

    // State machine states for camera view transitions
    enum class CameraTransitionState : uint8_t {
        Idle = 0,           // No transition in progress
        TransitioningToFP,  // Transitioning from TP to FP
        TransitioningToTP,  // Transitioning from FP to TP
        Completed           // Transition just completed (reset to Idle next frame)
    };

    // Easing functions for smooth transitions
    enum class TransitionEasing : uint8_t {
        Linear = 0,
        EaseInOut,      // Smooth start and end
        EaseIn,         // Slow start, fast end
        EaseOut,        // Fast start, slow end
        EaseInOutCubic, // More pronounced easing
        Spring          // Slight overshoot for dynamic feel
    };

    struct CameraViewInterpolatorComponent {
        // Current view mode (the target when no transition, or destination during transition)
        CameraViewMode CurrentMode = CameraViewMode::FirstPerson;
        CameraViewMode PreviousMode = CameraViewMode::FirstPerson;

        // State machine
        CameraTransitionState TransitionState = CameraTransitionState::Idle;

        // Transition timing
        float TransitionDuration = 0.4f;    // Total transition time in seconds
        float TransitionProgress = 0.0f;    // 0.0 = start, 1.0 = complete
        float TransitionTimer = 0.0f;       // Accumulated time

        // Easing
        TransitionEasing Easing = TransitionEasing::EaseInOutCubic;

        // Interpolation values (0.0 = FP, 1.0 = TP)
        float BlendAlpha = 0.0f;            // Current blend factor

        // Cached positions for interpolation
        Math::Vec3 StartPosition{ 0.0f };   // Camera position at transition start
        Math::Vec3 EndPosition{ 0.0f };     // Target camera position
        Math::Vec3 CurrentPosition{ 0.0f }; // Current interpolated position

        // Cached rotations for interpolation (Euler angles in radians)
        Math::Vec3 StartRotation{ 0.0f };
        Math::Vec3 EndRotation{ 0.0f };
        Math::Vec3 CurrentRotation{ 0.0f };

        // FOV interpolation
        float StartFOV = 70.0f;
        float EndFOV = 60.0f;
        float CurrentFOV = 70.0f;

        // Input blocking during transition
        bool BlockInputDuringTransition = false;
        bool IsInputBlocked = false;

        // Transition triggers
        bool RequestTransitionToFP = false;
        bool RequestTransitionToTP = false;

        // Entity references
        entt::entity CameraEntity = entt::null;
        entt::entity PlayerEntity = entt::null;

        CameraViewInterpolatorComponent() = default;

        // Factory method
        static CameraViewInterpolatorComponent Create(
            entt::entity cameraEntity,
            entt::entity playerEntity,
            CameraViewMode initialMode = CameraViewMode::FirstPerson,
            float transitionDuration = 0.4f)
        {
            CameraViewInterpolatorComponent interp;
            interp.CameraEntity = cameraEntity;
            interp.PlayerEntity = playerEntity;
            interp.CurrentMode = initialMode;
            interp.PreviousMode = initialMode;
            interp.TransitionDuration = transitionDuration;
            interp.BlendAlpha = (initialMode == CameraViewMode::ThirdPerson) ? 1.0f : 0.0f;
            return interp;
        }

        // Request a view mode change
        void RequestViewModeChange(CameraViewMode targetMode)
        {
            if (targetMode == CurrentMode && TransitionState == CameraTransitionState::Idle) {
                return; // Already in target mode
            }

            if (targetMode == CameraViewMode::FirstPerson) {
                RequestTransitionToFP = true;
                RequestTransitionToTP = false;
            }
            else {
                RequestTransitionToTP = true;
                RequestTransitionToFP = false;
            }
        }

        // Toggle between FP and TP
        void ToggleViewMode()
        {
            if (CurrentMode == CameraViewMode::FirstPerson) {
                RequestViewModeChange(CameraViewMode::ThirdPerson);
            }
            else {
                RequestViewModeChange(CameraViewMode::FirstPerson);
            }
        }

        // Check if currently transitioning
        bool IsTransitioning() const
        {
            return TransitionState == CameraTransitionState::TransitioningToFP ||
                   TransitionState == CameraTransitionState::TransitioningToTP;
        }

        // Apply easing function to progress value
        float ApplyEasing(float t) const
        {
            t = glm::clamp(t, 0.0f, 1.0f);

            switch (Easing) {
                case TransitionEasing::Linear:
                    return t;

                case TransitionEasing::EaseIn:
                    return t * t;

                case TransitionEasing::EaseOut:
                    return 1.0f - (1.0f - t) * (1.0f - t);

                case TransitionEasing::EaseInOut:
                    return t < 0.5f 
                        ? 2.0f * t * t 
                        : 1.0f - glm::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;

                case TransitionEasing::EaseInOutCubic:
                    return t < 0.5f 
                        ? 4.0f * t * t * t 
                        : 1.0f - glm::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;

                case TransitionEasing::Spring: {
                    // Spring-like easing with slight overshoot
                    const float c4 = (2.0f * glm::pi<float>()) / 3.0f;
                    if (t <= 0.0f) return 0.0f;
                    if (t >= 1.0f) return 1.0f;
                    return glm::pow(2.0f, -10.0f * t) * glm::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
                }

                default:
                    return t;
            }
        }

        // Begin transition
        void BeginTransition(
            CameraViewMode targetMode,
            const Math::Vec3& fromPosition,
            const Math::Vec3& toPosition,
            const Math::Vec3& fromRotation,
            const Math::Vec3& toRotation,
            float fromFOV,
            float toFOV)
        {
            PreviousMode = CurrentMode;
            CurrentMode = targetMode;

            StartPosition = fromPosition;
            EndPosition = toPosition;
            StartRotation = fromRotation;
            EndRotation = toRotation;
            StartFOV = fromFOV;
            EndFOV = toFOV;

            TransitionTimer = 0.0f;
            TransitionProgress = 0.0f;

            if (targetMode == CameraViewMode::FirstPerson) {
                TransitionState = CameraTransitionState::TransitioningToFP;
            }
            else {
                TransitionState = CameraTransitionState::TransitioningToTP;
            }

            if (BlockInputDuringTransition) {
                IsInputBlocked = true;
            }

            // Clear request flags
            RequestTransitionToFP = false;
            RequestTransitionToTP = false;
        }

        // Update transition progress
        void UpdateTransition(float deltaTime)
        {
            if (!IsTransitioning()) {
                return;
            }

            TransitionTimer += deltaTime;
            TransitionProgress = glm::clamp(TransitionTimer / TransitionDuration, 0.0f, 1.0f);

            // Apply easing
            float easedProgress = ApplyEasing(TransitionProgress);

            // Interpolate position
            CurrentPosition = glm::mix(StartPosition, EndPosition, easedProgress);

            // Interpolate rotation (handle angle wrapping)
            CurrentRotation = InterpolateRotation(StartRotation, EndRotation, easedProgress);

            // Interpolate FOV
            CurrentFOV = glm::mix(StartFOV, EndFOV, easedProgress);

            // Update blend alpha
            if (CurrentMode == CameraViewMode::ThirdPerson) {
                BlendAlpha = easedProgress;
            }
            else {
                BlendAlpha = 1.0f - easedProgress;
            }

            // Check for completion
            if (TransitionProgress >= 1.0f) {
                CompleteTransition();
            }
        }

        // Complete the transition
        void CompleteTransition()
        {
            TransitionState = CameraTransitionState::Completed;
            TransitionProgress = 1.0f;
            IsInputBlocked = false;

            CurrentPosition = EndPosition;
            CurrentRotation = EndRotation;
            CurrentFOV = EndFOV;

            BlendAlpha = (CurrentMode == CameraViewMode::ThirdPerson) ? 1.0f : 0.0f;
        }

        // Reset state after completion is acknowledged
        void AcknowledgeCompletion()
        {
            if (TransitionState == CameraTransitionState::Completed) {
                TransitionState = CameraTransitionState::Idle;
            }
        }

        // Cancel transition (snap to target)
        void CancelTransition()
        {
            if (IsTransitioning()) {
                CompleteTransition();
                TransitionState = CameraTransitionState::Idle;
            }
        }

    private:
        // Helper to interpolate rotation with proper angle wrapping
        Math::Vec3 InterpolateRotation(const Math::Vec3& from, const Math::Vec3& to, float t) const
        {
            Math::Vec3 result;

            // Wrap angles to find shortest path
            for (int i = 0; i < 3; ++i) {
                float diff = to[i] - from[i];

                // Normalize to [-PI, PI]
                while (diff > glm::pi<float>()) diff -= 2.0f * glm::pi<float>();
                while (diff < -glm::pi<float>()) diff += 2.0f * glm::pi<float>();

                result[i] = from[i] + diff * t;
            }

            return result;
        }
    };

} // namespace ECS
} // namespace Core
