#pragma once

#include "Core/Physics/PhysicsLayers.h"
#include "Core/Math/Math.h"
#include <Jolt/Physics/Character/CharacterVirtual.h>

namespace Core {
namespace ECS {

    // Character controller ground state
    enum class GroundState : uint8_t {
        OnGround,
        OnSteepGround,
        NotSupported,
        InAir
    };

    struct CharacterControllerComponent {
        // Physical dimensions
        float Height = 1.8f;           // Total height of the character
        float Radius = 0.3f;           // Capsule radius
        float Mass = 70.0f;            // Character mass in kg

        // Movement settings
        float MaxSlopeAngle = 50.0f;   // Maximum walkable slope in degrees
        float MaxStrength = 100.0f;    // Max force character can push objects with
        float PredictiveContactDistance = 0.1f;

        // Movement state
        Math::Vec3 LinearVelocity{ 0.0f, 0.0f, 0.0f };
        Math::Vec3 DesiredVelocity{ 0.0f, 0.0f, 0.0f };

        // Ground state
        GroundState CurrentGroundState = GroundState::InAir;
        Math::Vec3 GroundNormal{ 0.0f, 1.0f, 0.0f };
        float GroundDistance = 0.0f;

        // Stepping
        float StepHeight = 0.35f;      // Max step height character can climb

        // Collision layer
        JPH::ObjectLayer Layer = Physics::Layers::MOVING;

        // Internal state
        JPH::CharacterVirtual* CharacterInstance = nullptr;
        bool IsCreated = false;
        bool NeedsSync = true;

        CharacterControllerComponent() = default;

        // Factory method
        static CharacterControllerComponent Create(float height = 1.8f, float radius = 0.3f, float mass = 70.0f)
        {
            CharacterControllerComponent cc;
            cc.Height = height;
            cc.Radius = radius;
            cc.Mass = mass;
            return cc;
        }

        // Get the capsule half-height (from center to top of hemisphere)
        float GetCapsuleHalfHeight() const
        {
            return (Height - 2.0f * Radius) * 0.5f;
        }

        // Set desired movement velocity (in world space)
        void SetDesiredVelocity(const Math::Vec3& velocity)
        {
            DesiredVelocity = velocity;
            NeedsSync = true;
        }

        // Check if character is on ground
        bool IsOnGround() const
        {
            return CurrentGroundState == GroundState::OnGround;
        }

        // Check if character can jump (on walkable ground)
        bool CanJump() const
        {
            return CurrentGroundState == GroundState::OnGround ||
                   CurrentGroundState == GroundState::OnSteepGround;
        }

        // Check if character is in air
        bool IsInAir() const
        {
            return CurrentGroundState == GroundState::InAir ||
                   CurrentGroundState == GroundState::NotSupported;
        }

        // Convert Jolt ground state to our enum
        static GroundState FromJoltGroundState(JPH::CharacterBase::EGroundState state)
        {
            switch (state) {
                case JPH::CharacterBase::EGroundState::OnGround:       return GroundState::OnGround;
                case JPH::CharacterBase::EGroundState::OnSteepGround:  return GroundState::OnSteepGround;
                case JPH::CharacterBase::EGroundState::NotSupported:   return GroundState::NotSupported;
                case JPH::CharacterBase::EGroundState::InAir:          return GroundState::InAir;
                default:                                               return GroundState::InAir;
            }
        }
    };

} // namespace ECS
} // namespace Core
