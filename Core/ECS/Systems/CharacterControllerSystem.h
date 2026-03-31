#pragma once

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/CharacterControllerComponent.h"
#include "Core/Physics/PhysicsWorld.h"
#include "Core/Profile.h"
#include "Core/Log.h"
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <vector>

namespace Core {
namespace ECS {

    // Callback for character collision with physics bodies
    class CharacterContactListener : public JPH::CharacterContactListener {
    public:
        void OnAdjustBodyVelocity(
            [[maybe_unused]] const JPH::CharacterVirtual* inCharacter,
            [[maybe_unused]] const JPH::Body& inBody2,
            [[maybe_unused]] JPH::Vec3& ioLinearVelocity,
            [[maybe_unused]] JPH::Vec3& ioAngularVelocity) override
        {
            // Can be used to adjust the velocity of bodies the character is standing on
        }

        bool OnContactValidate(
            [[maybe_unused]] const JPH::CharacterVirtual* inCharacter,
            [[maybe_unused]] const JPH::BodyID& inBodyID2,
            [[maybe_unused]] const JPH::SubShapeID& inSubShapeID2) override
        {
            // Return true to accept the contact
            return true;
        }

        void OnContactAdded(
            [[maybe_unused]] const JPH::CharacterVirtual* inCharacter,
            [[maybe_unused]] const JPH::BodyID& inBodyID2,
            [[maybe_unused]] const JPH::SubShapeID& inSubShapeID2,
            [[maybe_unused]] JPH::RVec3Arg inContactPosition,
            [[maybe_unused]] JPH::Vec3Arg inContactNormal,
            [[maybe_unused]] JPH::CharacterContactSettings& ioSettings) override
        {
            // Called when character makes contact with a body
        }

        void OnContactSolve(
            [[maybe_unused]] const JPH::CharacterVirtual* inCharacter,
            [[maybe_unused]] const JPH::BodyID& inBodyID2,
            [[maybe_unused]] const JPH::SubShapeID& inSubShapeID2,
            [[maybe_unused]] JPH::RVec3Arg inContactPosition,
            [[maybe_unused]] JPH::Vec3Arg inContactNormal,
            [[maybe_unused]] JPH::Vec3Arg inContactVelocity,
            [[maybe_unused]] const JPH::PhysicsMaterial* inContactMaterial,
            [[maybe_unused]] JPH::Vec3Arg inCharacterVelocity,
            [[maybe_unused]] JPH::Vec3& ioNewCharacterVelocity) override
        {
            // Called to solve contact constraints
        }
    };

    class CharacterControllerSystem {
    public:
        CharacterControllerSystem();
        ~CharacterControllerSystem();

        // Initialize with physics world reference
        void Initialize(Physics::PhysicsWorld* physicsWorld);

        // Shutdown and cleanup
        void Shutdown();

        // Pre-update: create character instances, apply desired velocity
        void PreUpdate(Scene& scene);

        // Update character controllers (call after physics step)
        void Update(Scene& scene, float deltaTime);

        // Post-update: sync character position back to transform
        void PostUpdate(Scene& scene);

        // Gravity for characters (can differ from physics world gravity)
        void SetGravity(const Math::Vec3& gravity) { m_Gravity = gravity; }
        const Math::Vec3& GetGravity() const { return m_Gravity; }

    private:
        // Create CharacterVirtual instance for entity
        void CreateCharacter(entt::registry& registry, entt::entity entity,
                             TransformComponent& transform,
                             CharacterControllerComponent& controller);

        // Destroy CharacterVirtual instance
        void DestroyCharacter(CharacterControllerComponent& controller);

        // Update single character
        void UpdateCharacter(CharacterControllerComponent& controller,
                             const TransformComponent& transform,
                             float deltaTime);

    private:
        Physics::PhysicsWorld* m_PhysicsWorld = nullptr;
        std::unique_ptr<CharacterContactListener> m_ContactListener;
        
        Math::Vec3 m_Gravity{ 0.0f, -9.81f, 0.0f };

        // Track all created characters for cleanup
        std::vector<JPH::CharacterVirtual*> m_Characters;
    };

} // namespace ECS
} // namespace Core
