#pragma once

#include "Core/Physics/PhysicsLayers.h"
#include "Core/Math/Math.h"
#include <Jolt/Physics/Body/BodyID.h>

namespace Core {
namespace ECS {

    // Motion type determines how the body moves in the physics simulation
    enum class MotionType : uint8_t {
        Static,     // Never moves (terrain, buildings)
        Kinematic,  // Moved by code, not physics (platforms, elevators)
        Dynamic     // Fully simulated by physics
    };

    struct RigidBodyComponent {
        // Motion type
        MotionType Type = MotionType::Dynamic;

        // Mass properties (only for dynamic bodies)
        float Mass = 1.0f;

        // Damping
        float LinearDamping = 0.05f;
        float AngularDamping = 0.05f;

        // Velocity
        Math::Vec3 LinearVelocity{ 0.0f, 0.0f, 0.0f };
        Math::Vec3 AngularVelocity{ 0.0f, 0.0f, 0.0f };

        // Constraints
        bool AllowSleep = true;
        bool GravityEnabled = true;

        // Continuous collision detection (for fast-moving objects)
        bool UseCCD = false;

        // Jolt body ID (set by physics system after body creation)
        JPH::BodyID BodyID;
        bool IsBodyCreated = false;

        // Dirty flag for syncing changes to physics world
        bool NeedsSync = true;

        RigidBodyComponent() = default;

        // Factory methods
        static RigidBodyComponent CreateStatic()
        {
            RigidBodyComponent rb;
            rb.Type = MotionType::Static;
            rb.Mass = 0.0f;
            rb.AllowSleep = true;
            rb.GravityEnabled = false;
            return rb;
        }

        static RigidBodyComponent CreateKinematic()
        {
            RigidBodyComponent rb;
            rb.Type = MotionType::Kinematic;
            rb.Mass = 0.0f;
            rb.AllowSleep = false;
            rb.GravityEnabled = false;
            return rb;
        }

        static RigidBodyComponent CreateDynamic(float mass = 1.0f)
        {
            RigidBodyComponent rb;
            rb.Type = MotionType::Dynamic;
            rb.Mass = mass;
            return rb;
        }

        // Get Jolt motion type
        JPH::EMotionType GetJoltMotionType() const
        {
            switch (Type) {
                case MotionType::Static:    return JPH::EMotionType::Static;
                case MotionType::Kinematic: return JPH::EMotionType::Kinematic;
                case MotionType::Dynamic:   return JPH::EMotionType::Dynamic;
                default:                    return JPH::EMotionType::Dynamic;
            }
        }

        // Apply impulse (accumulated until physics step)
        void ApplyLinearImpulse(const Math::Vec3& impulse)
        {
            PendingLinearImpulse += impulse;
            NeedsSync = true;
        }

        void ApplyAngularImpulse(const Math::Vec3& impulse)
        {
            PendingAngularImpulse += impulse;
            NeedsSync = true;
        }

        void SetLinearVelocity(const Math::Vec3& velocity)
        {
            LinearVelocity = velocity;
            NeedsSync = true;
        }

        void SetAngularVelocity(const Math::Vec3& velocity)
        {
            AngularVelocity = velocity;
            NeedsSync = true;
        }

        // Pending impulses (applied during physics step)
        Math::Vec3 PendingLinearImpulse{ 0.0f, 0.0f, 0.0f };
        Math::Vec3 PendingAngularImpulse{ 0.0f, 0.0f, 0.0f };
    };

} // namespace ECS
} // namespace Core
