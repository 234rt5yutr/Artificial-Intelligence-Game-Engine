#include "Core/Physics/PhysicsWorld.h"
#include <thread>

// Jolt trace callback
static void JoltTrace(const char* inFMT, ...)
{
    va_list list;
    va_start(list, inFMT);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), inFMT, list);
    va_end(list);
    ENGINE_CORE_TRACE("[Jolt] {}", buffer);
}

#ifdef JPH_ENABLE_ASSERTS
// Jolt assert callback
static bool JoltAssertFailed(const char* inExpression, const char* inMessage, const char* inFile, uint32_t inLine)
{
    ENGINE_CORE_ERROR("[Jolt Assert] {} : {} ({}:{})", inExpression, inMessage ? inMessage : "", inFile, inLine);
    return true; // Break into debugger
}
#endif

namespace Core {
namespace Physics {

    // ContactListenerImpl
    JPH::ValidateResult ContactListenerImpl::OnContactValidate(
        const JPH::Body& /*inBody1*/,
        const JPH::Body& /*inBody2*/,
        JPH::RVec3Arg /*inBaseOffset*/,
        const JPH::CollideShapeResult& /*inCollisionResult*/)
    {
        // Accept all contacts by default
        return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    void ContactListenerImpl::OnContactAdded(
        const JPH::Body& /*inBody1*/,
        const JPH::Body& /*inBody2*/,
        const JPH::ContactManifold& /*inManifold*/,
        JPH::ContactSettings& /*ioSettings*/)
    {
        // Contact added - can be used for sound effects, damage, etc.
    }

    void ContactListenerImpl::OnContactPersisted(
        const JPH::Body& /*inBody1*/,
        const JPH::Body& /*inBody2*/,
        const JPH::ContactManifold& /*inManifold*/,
        JPH::ContactSettings& /*ioSettings*/)
    {
        // Contact persisted
    }

    void ContactListenerImpl::OnContactRemoved(const JPH::SubShapeIDPair& /*inSubShapePair*/)
    {
        // Contact removed
    }

    // BodyActivationListenerImpl
    void BodyActivationListenerImpl::OnBodyActivated(const JPH::BodyID& /*inBodyID*/, uint64_t /*inBodyUserData*/)
    {
        ENGINE_CORE_TRACE("Body activated");
    }

    void BodyActivationListenerImpl::OnBodyDeactivated(const JPH::BodyID& /*inBodyID*/, uint64_t /*inBodyUserData*/)
    {
        ENGINE_CORE_TRACE("Body deactivated");
    }

    // PhysicsWorld
    PhysicsWorld::PhysicsWorld() = default;

    PhysicsWorld::~PhysicsWorld()
    {
        if (m_Initialized) {
            Shutdown();
        }
    }

    bool PhysicsWorld::Initialize(const PhysicsConfig& config)
    {
        PROFILE_FUNCTION();

        if (m_Initialized) {
            ENGINE_CORE_WARN("PhysicsWorld already initialized");
            return true;
        }

        m_Config = config;

        ENGINE_CORE_INFO("Initializing Jolt Physics...");

        // Register allocation hooks
        JPH::RegisterDefaultAllocator();

        // Install callbacks
        JPH::Trace = JoltTrace;
        JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = JoltAssertFailed;)

        // Create factory once for the process lifetime
        if (JPH::Factory::sInstance == nullptr) {
            JPH::Factory::sInstance = new JPH::Factory();
        } else {
            ENGINE_CORE_WARN("Jolt factory already exists, reusing existing instance");
        }

        // Register all Jolt physics types
        JPH::RegisterTypes();

        // Create temp allocator for physics operations
        m_TempAllocator = std::make_unique<JPH::TempAllocatorImpl>(config.TempAllocatorSize);

        // Create job system - use engine's job system or Jolt's thread pool
        if (config.UseEngineJobSystem) {
            auto engineJobSystem = std::make_unique<EngineJobSystemAdapter>();
            engineJobSystem->Initialize();
            m_JobSystem = std::move(engineJobSystem);
            ENGINE_CORE_INFO("Jolt using Engine JobSystem adapter");
        } else {
            uint32_t numThreads = std::max(1u, std::thread::hardware_concurrency() - 1);
            m_JobSystem = std::make_unique<JPH::JobSystemThreadPool>(
                JPH::cMaxPhysicsJobs,
                JPH::cMaxPhysicsBarriers,
                static_cast<int>(numThreads)
            );
            ENGINE_CORE_INFO("Jolt using built-in ThreadPool with {} threads", numThreads);
        }

        // Create layer interfaces
        m_BroadPhaseLayerInterface = std::make_unique<BroadPhaseLayerInterfaceImpl>();
        m_ObjectVsBroadPhaseLayerFilter = std::make_unique<ObjectVsBroadPhaseLayerFilterImpl>();
        m_ObjectLayerPairFilter = std::make_unique<ObjectLayerPairFilterImpl>();

        // Create physics system
        m_PhysicsSystem = std::make_unique<JPH::PhysicsSystem>();
        m_PhysicsSystem->Init(
            config.MaxBodies,
            config.NumBodyMutexes,
            config.MaxBodyPairs,
            config.MaxContactConstraints,
            *m_BroadPhaseLayerInterface,
            *m_ObjectVsBroadPhaseLayerFilter,
            *m_ObjectLayerPairFilter
        );

        // Set gravity
        m_PhysicsSystem->SetGravity(JPH::Vec3(0.0f, config.GravityY, 0.0f));

        // Create and set listeners
        m_ContactListener = std::make_unique<ContactListenerImpl>();
        m_BodyActivationListener = std::make_unique<BodyActivationListenerImpl>();

        m_PhysicsSystem->SetContactListener(m_ContactListener.get());
        m_PhysicsSystem->SetBodyActivationListener(m_BodyActivationListener.get());

        m_Initialized = true;
        ENGINE_CORE_INFO("Jolt Physics initialized (MaxBodies: {}, Gravity: {})",
                         config.MaxBodies, config.GravityY);

        return true;
    }

    void PhysicsWorld::Shutdown()
    {
        PROFILE_FUNCTION();

        if (!m_Initialized) {
            return;
        }

        ENGINE_CORE_INFO("Shutting down Jolt Physics...");

        // Remove listeners
        if (m_PhysicsSystem) {
            m_PhysicsSystem->SetContactListener(nullptr);
            m_PhysicsSystem->SetBodyActivationListener(nullptr);
        }

        // Cleanup in reverse order
        m_BodyActivationListener.reset();
        m_ContactListener.reset();
        m_PhysicsSystem.reset();
        m_ObjectLayerPairFilter.reset();
        m_ObjectVsBroadPhaseLayerFilter.reset();
        m_BroadPhaseLayerInterface.reset();
        m_JobSystem.reset();
        m_TempAllocator.reset();

        // Unregister types and destroy factory
        JPH::UnregisterTypes();
        if (JPH::Factory::sInstance != nullptr) {
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }

        m_Initialized = false;
        ENGINE_CORE_INFO("Jolt Physics shutdown complete");
    }

    JPH::BodyInterface& PhysicsWorld::GetBodyInterface()
    {
        return m_PhysicsSystem->GetBodyInterface();
    }

    const JPH::BodyInterface& PhysicsWorld::GetBodyInterface() const
    {
        return m_PhysicsSystem->GetBodyInterface();
    }

    JPH::Vec3 PhysicsWorld::GetGravity() const
    {
        return m_PhysicsSystem->GetGravity();
    }

    void PhysicsWorld::SetGravity(const JPH::Vec3& gravity)
    {
        m_PhysicsSystem->SetGravity(gravity);
    }

    bool PhysicsWorld::ShiftBody(const JPH::BodyID& bodyId, const Math::Vec3& worldOffset)
    {
        if (!m_Initialized || bodyId.IsInvalid()) {
            return false;
        }

        JPH::BodyInterface& bodyInterface = GetBodyInterface();
        if (!bodyInterface.IsAdded(bodyId)) {
            return false;
        }

        const JPH::RVec3 position = bodyInterface.GetPosition(bodyId);
        const JPH::RVec3 shiftedPosition = position + JPH::RVec3(
            worldOffset.x,
            worldOffset.y,
            worldOffset.z);
        bodyInterface.SetPosition(bodyId, shiftedPosition, JPH::EActivation::DontActivate);
        return true;
    }

} // namespace Physics
} // namespace Core
