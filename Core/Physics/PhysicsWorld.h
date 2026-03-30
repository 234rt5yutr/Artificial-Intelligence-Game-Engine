#pragma once

#include "Core/Physics/PhysicsLayers.h"
#include "Core/Physics/EngineJobSystemAdapter.h"
#include "Core/Log.h"
#include "Core/Profile.h"
#include <memory>

namespace Core {
namespace Physics {

    // Contact listener for collision callbacks
    class ContactListenerImpl : public JPH::ContactListener {
    public:
        JPH::ValidateResult OnContactValidate(
            const JPH::Body& inBody1,
            const JPH::Body& inBody2,
            JPH::RVec3Arg inBaseOffset,
            const JPH::CollideShapeResult& inCollisionResult) override;

        void OnContactAdded(
            const JPH::Body& inBody1,
            const JPH::Body& inBody2,
            const JPH::ContactManifold& inManifold,
            JPH::ContactSettings& ioSettings) override;

        void OnContactPersisted(
            const JPH::Body& inBody1,
            const JPH::Body& inBody2,
            const JPH::ContactManifold& inManifold,
            JPH::ContactSettings& ioSettings) override;

        void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override;
    };

    // Body activation listener for sleep/wake callbacks
    class BodyActivationListenerImpl : public JPH::BodyActivationListener {
    public:
        void OnBodyActivated(const JPH::BodyID& inBodyID, uint64_t inBodyUserData) override;
        void OnBodyDeactivated(const JPH::BodyID& inBodyID, uint64_t inBodyUserData) override;
    };

    // Physics world configuration
    struct PhysicsConfig {
        uint32_t MaxBodies = 65536;
        uint32_t NumBodyMutexes = 0;  // 0 = default
        uint32_t MaxBodyPairs = 65536;
        uint32_t MaxContactConstraints = 10240;
        float GravityY = -9.81f;
        uint32_t TempAllocatorSize = 10 * 1024 * 1024;  // 10 MB
        bool UseEngineJobSystem = true;  // Use engine's job system instead of Jolt's
    };

    class PhysicsWorld {
    public:
        PhysicsWorld();
        ~PhysicsWorld();

        // Delete copy/move
        PhysicsWorld(const PhysicsWorld&) = delete;
        PhysicsWorld& operator=(const PhysicsWorld&) = delete;
        PhysicsWorld(PhysicsWorld&&) = delete;
        PhysicsWorld& operator=(PhysicsWorld&&) = delete;

        // Initialize the physics world
        bool Initialize(const PhysicsConfig& config = PhysicsConfig{});

        // Shutdown and cleanup
        void Shutdown();

        // Check if initialized
        bool IsInitialized() const { return m_Initialized; }

        // Access the Jolt physics system
        JPH::PhysicsSystem& GetPhysicsSystem() { return *m_PhysicsSystem; }
        const JPH::PhysicsSystem& GetPhysicsSystem() const { return *m_PhysicsSystem; }

        // Access the body interface for creating/modifying bodies
        JPH::BodyInterface& GetBodyInterface();
        const JPH::BodyInterface& GetBodyInterface() const;

        // Access temp allocator for physics operations
        JPH::TempAllocator& GetTempAllocator() { return *m_TempAllocator; }

        // Access job system
        JPH::JobSystem& GetJobSystem() { return *m_JobSystem; }

        // Get gravity
        JPH::Vec3 GetGravity() const;
        void SetGravity(const JPH::Vec3& gravity);

    private:
        bool m_Initialized = false;
        PhysicsConfig m_Config;

        // Jolt systems
        std::unique_ptr<JPH::TempAllocatorImpl> m_TempAllocator;
        std::unique_ptr<JPH::JobSystem> m_JobSystem;  // Can be either EngineJobSystemAdapter or JobSystemThreadPool
        std::unique_ptr<JPH::PhysicsSystem> m_PhysicsSystem;

        // Layer interfaces
        std::unique_ptr<BroadPhaseLayerInterfaceImpl> m_BroadPhaseLayerInterface;
        std::unique_ptr<ObjectVsBroadPhaseLayerFilterImpl> m_ObjectVsBroadPhaseLayerFilter;
        std::unique_ptr<ObjectLayerPairFilterImpl> m_ObjectLayerPairFilter;

        // Listeners
        std::unique_ptr<ContactListenerImpl> m_ContactListener;
        std::unique_ptr<BodyActivationListenerImpl> m_BodyActivationListener;
    };

} // namespace Physics
} // namespace Core
