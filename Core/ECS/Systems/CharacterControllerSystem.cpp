#include "Core/ECS/Systems/CharacterControllerSystem.h"

namespace Core {
namespace ECS {

    CharacterControllerSystem::CharacterControllerSystem() = default;

    CharacterControllerSystem::~CharacterControllerSystem()
    {
        Shutdown();
    }

    void CharacterControllerSystem::Initialize(Physics::PhysicsWorld* physicsWorld)
    {
        PROFILE_FUNCTION();

        m_PhysicsWorld = physicsWorld;
        m_ContactListener = std::make_unique<CharacterContactListener>();

        // Default gravity matches physics world if available
        if (m_PhysicsWorld && m_PhysicsWorld->IsInitialized()) {
            JPH::Vec3 gravity = m_PhysicsWorld->GetGravity();
            m_Gravity = Math::Vec3(gravity.GetX(), gravity.GetY(), gravity.GetZ());
        }

        ENGINE_CORE_INFO("CharacterControllerSystem initialized");
    }

    void CharacterControllerSystem::Shutdown()
    {
        PROFILE_FUNCTION();

        // Cleanup all character instances
        for (auto* character : m_Characters) {
            delete character;
        }
        m_Characters.clear();

        m_ContactListener.reset();
        m_PhysicsWorld = nullptr;

        ENGINE_CORE_INFO("CharacterControllerSystem shutdown");
    }

    void CharacterControllerSystem::PreUpdate(Scene& scene)
    {
        PROFILE_FUNCTION();

        if (!m_PhysicsWorld || !m_PhysicsWorld->IsInitialized()) {
            return;
        }

        auto& registry = scene.GetRegistry();

        // Create character instances for entities that don't have them
        auto view = registry.view<TransformComponent, CharacterControllerComponent>();
        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& controller = view.get<CharacterControllerComponent>(entity);

            if (!controller.IsCreated) {
                CreateCharacter(registry, entity, transform, controller);
            }
        }
    }

    void CharacterControllerSystem::Update(Scene& scene, float deltaTime)
    {
        PROFILE_FUNCTION();

        if (!m_PhysicsWorld || !m_PhysicsWorld->IsInitialized()) {
            return;
        }

        auto& registry = scene.GetRegistry();

        auto view = registry.view<TransformComponent, CharacterControllerComponent>();
        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& controller = view.get<CharacterControllerComponent>(entity);

            if (controller.IsCreated && controller.CharacterInstance) {
                UpdateCharacter(controller, transform, deltaTime);
            }
        }
    }

    void CharacterControllerSystem::PostUpdate(Scene& scene)
    {
        PROFILE_FUNCTION();

        if (!m_PhysicsWorld || !m_PhysicsWorld->IsInitialized()) {
            return;
        }

        auto& registry = scene.GetRegistry();

        // Sync character positions back to transforms
        auto view = registry.view<TransformComponent, CharacterControllerComponent>();
        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& controller = view.get<CharacterControllerComponent>(entity);

            if (controller.IsCreated && controller.CharacterInstance) {
                // Get character position
                JPH::RVec3 position = controller.CharacterInstance->GetPosition();
                transform.Position = Math::Vec3(
                    static_cast<float>(position.GetX()),
                    static_cast<float>(position.GetY()),
                    static_cast<float>(position.GetZ())
                );

                // Get current velocity
                JPH::Vec3 velocity = controller.CharacterInstance->GetLinearVelocity();
                controller.LinearVelocity = Math::Vec3(velocity.GetX(), velocity.GetY(), velocity.GetZ());

                // Update ground state
                controller.CurrentGroundState = CharacterControllerComponent::FromJoltGroundState(
                    controller.CharacterInstance->GetGroundState()
                );

                // Get ground normal if on ground
                if (controller.IsOnGround() || controller.CurrentGroundState == GroundState::OnSteepGround) {
                    JPH::Vec3 normal = controller.CharacterInstance->GetGroundNormal();
                    controller.GroundNormal = Math::Vec3(normal.GetX(), normal.GetY(), normal.GetZ());
                }

                // Mark transform dirty for transform system
                transform.SetDirty();
            }
        }
    }

    void CharacterControllerSystem::CreateCharacter(
        [[maybe_unused]] entt::registry& registry,
        entt::entity entity,
        TransformComponent& transform,
        CharacterControllerComponent& controller)
    {
        PROFILE_FUNCTION();

        // Create capsule shape for character
        // Capsule is centered at origin, so we need to offset it up
        float halfHeight = controller.GetCapsuleHalfHeight();
        
        JPH::RefConst<JPH::Shape> standingShape = JPH::RotatedTranslatedShapeSettings(
            JPH::Vec3(0, halfHeight + controller.Radius, 0),
            JPH::Quat::sIdentity(),
            new JPH::CapsuleShape(halfHeight, controller.Radius)
        ).Create().Get();

        // Character settings
        JPH::CharacterVirtualSettings settings;
        settings.mMaxSlopeAngle = glm::radians(controller.MaxSlopeAngle);
        settings.mMaxStrength = controller.MaxStrength;
        settings.mShape = standingShape;
        settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
        settings.mCharacterPadding = 0.02f;
        settings.mPenetrationRecoverySpeed = 1.0f;
        settings.mPredictiveContactDistance = controller.PredictiveContactDistance;
        settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -controller.Radius);
        settings.mMass = controller.Mass;

        // Initial position
        JPH::RVec3 position(transform.Position.x, transform.Position.y, transform.Position.z);
        JPH::Quat rotation = JPH::Quat::sEulerAngles(
            JPH::Vec3(transform.Rotation.x, transform.Rotation.y, transform.Rotation.z)
        );

        // Create character
        controller.CharacterInstance = new JPH::CharacterVirtual(
            &settings,
            position,
            rotation,
            0,  // User data
            &m_PhysicsWorld->GetPhysicsSystem()
        );

        controller.CharacterInstance->SetListener(m_ContactListener.get());
        controller.IsCreated = true;
        controller.NeedsSync = false;

        m_Characters.push_back(controller.CharacterInstance);

        ENGINE_CORE_INFO("Created CharacterController for entity {}", static_cast<uint32_t>(entity));
    }

    void CharacterControllerSystem::DestroyCharacter(CharacterControllerComponent& controller)
    {
        PROFILE_FUNCTION();

        if (controller.CharacterInstance) {
            // Remove from tracked list
            auto it = std::find(m_Characters.begin(), m_Characters.end(), controller.CharacterInstance);
            if (it != m_Characters.end()) {
                m_Characters.erase(it);
            }

            delete controller.CharacterInstance;
            controller.CharacterInstance = nullptr;
            controller.IsCreated = false;
        }
    }

    void CharacterControllerSystem::UpdateCharacter(
        CharacterControllerComponent& controller,
        [[maybe_unused]] const TransformComponent& transform,
        float deltaTime)
    {
        PROFILE_FUNCTION();

        if (!controller.CharacterInstance) {
            return;
        }

        JPH::CharacterVirtual* character = controller.CharacterInstance;

        // Get current velocity
        JPH::Vec3 currentVelocity = character->GetLinearVelocity();

        // Build desired velocity from input + gravity
        JPH::Vec3 desiredVelocity(
            controller.DesiredVelocity.x,
            controller.DesiredVelocity.y,
            controller.DesiredVelocity.z
        );

        // Apply gravity based on ground state
        JPH::Vec3 gravity(m_Gravity.x, m_Gravity.y, m_Gravity.z);

        JPH::Vec3 newVelocity;

        if (character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround) {
            // On ground: use desired horizontal velocity, maintain ground contact
            newVelocity = desiredVelocity;
            
            // Cancel gravity when on ground, but keep vertical input (jumping)
            if (controller.DesiredVelocity.y <= 0.0f) {
                newVelocity.SetY(0.0f);
            }
        }
        else {
            // In air: apply gravity, preserve horizontal velocity with some air control
            float airControl = 0.3f;  // Reduced control in air
            
            newVelocity.SetX(currentVelocity.GetX() + (desiredVelocity.GetX() - currentVelocity.GetX()) * airControl);
            newVelocity.SetZ(currentVelocity.GetZ() + (desiredVelocity.GetZ() - currentVelocity.GetZ()) * airControl);
            newVelocity.SetY(currentVelocity.GetY() + gravity.GetY() * deltaTime);

            // Apply vertical input (for jetpacks, double jumps, etc.)
            if (controller.DesiredVelocity.y > 0.0f) {
                newVelocity.SetY(newVelocity.GetY() + desiredVelocity.GetY());
            }
        }

        character->SetLinearVelocity(newVelocity);

        // Settings for character update
        JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
        updateSettings.mStickToFloorStepDown = JPH::Vec3(0, -controller.StepHeight, 0);
        updateSettings.mWalkStairsStepUp = JPH::Vec3(0, controller.StepHeight, 0);
        updateSettings.mWalkStairsMinStepForward = 0.02f;
        updateSettings.mWalkStairsStepForwardTest = 0.15f;
        updateSettings.mWalkStairsCosAngleForwardContact = glm::cos(glm::radians(75.0f));
        updateSettings.mWalkStairsStepDownExtra = JPH::Vec3::sZero();

        // Broadphase layer filter
        JPH::DefaultBroadPhaseLayerFilter broadPhaseFilter(
            m_PhysicsWorld->GetPhysicsSystem().GetObjectVsBroadPhaseLayerFilter(),
            controller.Layer
        );

        // Object layer filter
        JPH::DefaultObjectLayerFilter objectFilter(
            m_PhysicsWorld->GetPhysicsSystem().GetObjectLayerPairFilter(),
            controller.Layer
        );

        // Body filter (ignore specific bodies if needed)
        JPH::IgnoreMultipleBodiesFilter bodyFilter;

        // Shape filter
        JPH::ShapeFilter shapeFilter;

        // Extended update: handles stepping, penetration recovery, etc.
        character->ExtendedUpdate(
            deltaTime,
            gravity,
            updateSettings,
            broadPhaseFilter,
            objectFilter,
            bodyFilter,
            shapeFilter,
            m_PhysicsWorld->GetTempAllocator()
        );

        // Clear desired velocity after applying
        controller.DesiredVelocity = Math::Vec3(0.0f, 0.0f, 0.0f);
        controller.NeedsSync = false;
    }

} // namespace ECS
} // namespace Core
