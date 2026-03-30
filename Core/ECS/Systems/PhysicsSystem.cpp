#include "Core/ECS/Systems/PhysicsSystem.h"
#include <chrono>

namespace Core {
namespace ECS {

    PhysicsSystem::PhysicsSystem() = default;

    PhysicsSystem::~PhysicsSystem()
    {
        Shutdown();
    }

    void PhysicsSystem::Initialize(Physics::PhysicsWorld* physicsWorld)
    {
        PROFILE_FUNCTION();

        m_PhysicsWorld = physicsWorld;
        ENGINE_CORE_INFO("PhysicsSystem initialized");
    }

    void PhysicsSystem::Shutdown()
    {
        PROFILE_FUNCTION();

        m_PhysicsWorld = nullptr;
        ENGINE_CORE_INFO("PhysicsSystem shutdown");
    }

    void PhysicsSystem::PreUpdate(Scene& scene)
    {
        PROFILE_FUNCTION();

        if (!m_PhysicsWorld || !m_PhysicsWorld->IsInitialized()) {
            return;
        }

        auto& registry = scene.GetRegistry();

        // Create bodies for entities that don't have them yet
        auto view = registry.view<TransformComponent, RigidBodyComponent, ColliderComponent>();
        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& rigidBody = view.get<RigidBodyComponent>(entity);
            auto& collider = view.get<ColliderComponent>(entity);

            if (!rigidBody.IsBodyCreated) {
                CreateBody(registry, entity, transform, rigidBody, collider);
            }
        }

        // Apply pending forces and impulses
        ApplyPendingForces(registry);

        // Update body count statistics
        m_TotalBodyCount = static_cast<uint32_t>(m_PhysicsWorld->GetPhysicsSystem().GetNumBodies());
        m_ActiveBodyCount = static_cast<uint32_t>(m_PhysicsWorld->GetPhysicsSystem().GetNumActiveBodies(JPH::EBodyType::RigidBody));
    }

    void PhysicsSystem::Update(float deltaTime)
    {
        PROFILE_FUNCTION();

        if (!m_PhysicsWorld || !m_PhysicsWorld->IsInitialized()) {
            return;
        }

        auto startTime = std::chrono::high_resolution_clock::now();

        // Accumulate time for fixed timestep
        m_AccumulatedTime += deltaTime;

        // Step physics with fixed timestep
        int stepsPerformed = 0;
        while (m_AccumulatedTime >= m_FixedTimestep) {
            // Optimize broadphase (optional, can be done periodically)
            m_PhysicsWorld->GetPhysicsSystem().OptimizeBroadPhase();

            // Step the simulation
            m_PhysicsWorld->GetPhysicsSystem().Update(
                m_FixedTimestep,
                m_CollisionSteps,
                &m_PhysicsWorld->GetTempAllocator(),
                &m_PhysicsWorld->GetJobSystem()
            );

            m_AccumulatedTime -= m_FixedTimestep;
            stepsPerformed++;

            // Prevent spiral of death - cap accumulated time
            if (stepsPerformed >= 4) {
                m_AccumulatedTime = 0.0f;
                ENGINE_CORE_WARN("Physics: Too many steps, capping accumulated time");
                break;
            }
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        m_LastStepTime = std::chrono::duration<float>(endTime - startTime).count();

        ENGINE_CORE_TRACE("PhysicsSystem: {} steps, {:.2f}ms, {} active / {} total bodies",
                          stepsPerformed, m_LastStepTime * 1000.0f, m_ActiveBodyCount, m_TotalBodyCount);
    }

    void PhysicsSystem::CreateBody([[maybe_unused]] entt::registry& registry, entt::entity entity,
                                    TransformComponent& transform, RigidBodyComponent& rigidBody,
                                    ColliderComponent& collider)
    {
        PROFILE_FUNCTION();

        // Create shape
        JPH::RefConst<JPH::Shape> shape = CreateShape(collider);
        if (!shape) {
            ENGINE_CORE_ERROR("Failed to create shape for entity");
            return;
        }

        // Convert transform to Jolt types
        JPH::RVec3 position(transform.Position.x, transform.Position.y, transform.Position.z);
        JPH::Quat rotation = JPH::Quat::sEulerAngles(
            JPH::Vec3(transform.Rotation.x, transform.Rotation.y, transform.Rotation.z)
        );

        // Create body settings
        JPH::BodyCreationSettings bodySettings(
            shape,
            position,
            rotation,
            rigidBody.GetJoltMotionType(),
            collider.Layer
        );

        // Configure body properties
        bodySettings.mFriction = collider.Friction;
        bodySettings.mRestitution = collider.Restitution;
        bodySettings.mLinearDamping = rigidBody.LinearDamping;
        bodySettings.mAngularDamping = rigidBody.AngularDamping;
        bodySettings.mAllowSleeping = rigidBody.AllowSleep;
        bodySettings.mGravityFactor = rigidBody.GravityEnabled ? 1.0f : 0.0f;
        bodySettings.mIsSensor = collider.IsSensor;
        bodySettings.mMotionQuality = rigidBody.UseCCD ? 
            JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;

        // Set mass for dynamic bodies
        if (rigidBody.Type == MotionType::Dynamic && rigidBody.Mass > 0.0f) {
            bodySettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            bodySettings.mMassPropertiesOverride.mMass = rigidBody.Mass;
        }

        // Store entity ID in user data for reverse lookup
        bodySettings.mUserData = static_cast<uint64_t>(entity);

        // Create body
        JPH::BodyInterface& bodyInterface = m_PhysicsWorld->GetBodyInterface();
        JPH::Body* body = bodyInterface.CreateBody(bodySettings);

        if (!body) {
            ENGINE_CORE_ERROR("Failed to create physics body for entity");
            return;
        }

        rigidBody.BodyID = body->GetID();
        rigidBody.IsBodyCreated = true;
        rigidBody.NeedsSync = false;

        // Add to world and activate
        bodyInterface.AddBody(rigidBody.BodyID, JPH::EActivation::Activate);

        // Set initial velocity if specified
        if (glm::length(rigidBody.LinearVelocity) > 0.0f || glm::length(rigidBody.AngularVelocity) > 0.0f) {
            bodyInterface.SetLinearVelocity(rigidBody.BodyID, 
                JPH::Vec3(rigidBody.LinearVelocity.x, rigidBody.LinearVelocity.y, rigidBody.LinearVelocity.z));
            bodyInterface.SetAngularVelocity(rigidBody.BodyID,
                JPH::Vec3(rigidBody.AngularVelocity.x, rigidBody.AngularVelocity.y, rigidBody.AngularVelocity.z));
        }

        ENGINE_CORE_TRACE("Created physics body for entity");
    }

    JPH::RefConst<JPH::Shape> PhysicsSystem::CreateShape(const ColliderComponent& collider)
    {
        PROFILE_FUNCTION();

        JPH::RefConst<JPH::Shape> baseShape;

        switch (collider.Type) {
            case ColliderType::Box: {
                const auto& box = std::get<BoxColliderData>(collider.ShapeData);
                baseShape = new JPH::BoxShape(
                    JPH::Vec3(box.HalfExtents.x, box.HalfExtents.y, box.HalfExtents.z)
                );
                break;
            }

            case ColliderType::Sphere: {
                const auto& sphere = std::get<SphereColliderData>(collider.ShapeData);
                baseShape = new JPH::SphereShape(sphere.Radius);
                break;
            }

            case ColliderType::Capsule: {
                const auto& capsule = std::get<CapsuleColliderData>(collider.ShapeData);
                baseShape = new JPH::CapsuleShape(capsule.HalfHeight, capsule.Radius);
                break;
            }

            case ColliderType::Mesh: {
                const auto& mesh = std::get<MeshColliderData>(collider.ShapeData);
                if (mesh.Vertices.empty()) {
                    ENGINE_CORE_ERROR("Mesh collider has no vertices");
                    return nullptr;
                }

                if (mesh.Convex) {
                    // Create convex hull
                    JPH::Array<JPH::Vec3> points;
                    points.reserve(mesh.Vertices.size());
                    for (const auto& v : mesh.Vertices) {
                        points.push_back(JPH::Vec3(v.x, v.y, v.z));
                    }
                    
                    JPH::ConvexHullShapeSettings settings(points.data(), static_cast<int>(points.size()));
                    auto result = settings.Create();
                    if (result.HasError()) {
                        ENGINE_CORE_ERROR("Failed to create convex hull: {}", result.GetError().c_str());
                        return nullptr;
                    }
                    baseShape = result.Get();
                } else {
                    // Create triangle mesh (static only)
                    JPH::VertexList vertices;
                    vertices.reserve(mesh.Vertices.size());
                    for (const auto& v : mesh.Vertices) {
                        vertices.push_back(JPH::Float3(v.x, v.y, v.z));
                    }

                    JPH::IndexedTriangleList triangles;
                    for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3) {
                        triangles.push_back(JPH::IndexedTriangle(
                            mesh.Indices[i], mesh.Indices[i + 1], mesh.Indices[i + 2]
                        ));
                    }

                    JPH::MeshShapeSettings settings(vertices, triangles);
                    auto result = settings.Create();
                    if (result.HasError()) {
                        ENGINE_CORE_ERROR("Failed to create mesh shape: {}", result.GetError().c_str());
                        return nullptr;
                    }
                    baseShape = result.Get();
                }
                break;
            }

            default:
                ENGINE_CORE_ERROR("Unknown collider type");
                return nullptr;
        }

        // Apply local offset if needed
        if (glm::length(collider.Offset) > 0.001f || 
            glm::abs(collider.RotationOffset.w - 1.0f) > 0.001f) {
            JPH::Vec3 offset(collider.Offset.x, collider.Offset.y, collider.Offset.z);
            JPH::Quat rotation(collider.RotationOffset.x, collider.RotationOffset.y, 
                               collider.RotationOffset.z, collider.RotationOffset.w);
            
            JPH::RotatedTranslatedShapeSettings settings(offset, rotation, baseShape);
            auto result = settings.Create();
            if (result.HasError()) {
                ENGINE_CORE_ERROR("Failed to create offset shape: {}", result.GetError().c_str());
                return baseShape;
            }
            return result.Get();
        }

        return baseShape;
    }

    void PhysicsSystem::ApplyPendingForces(entt::registry& registry)
    {
        PROFILE_FUNCTION();

        if (!m_PhysicsWorld) return;

        JPH::BodyInterface& bodyInterface = m_PhysicsWorld->GetBodyInterface();

        auto view = registry.view<RigidBodyComponent>();
        for (auto entity : view) {
            auto& rigidBody = view.get<RigidBodyComponent>(entity);

            if (!rigidBody.IsBodyCreated || !rigidBody.NeedsSync) {
                continue;
            }

            // Apply pending impulses
            if (glm::length(rigidBody.PendingLinearImpulse) > 0.0f) {
                bodyInterface.AddImpulse(rigidBody.BodyID,
                    JPH::Vec3(rigidBody.PendingLinearImpulse.x, 
                              rigidBody.PendingLinearImpulse.y, 
                              rigidBody.PendingLinearImpulse.z));
                rigidBody.PendingLinearImpulse = Math::Vec3(0.0f);
            }

            if (glm::length(rigidBody.PendingAngularImpulse) > 0.0f) {
                bodyInterface.AddAngularImpulse(rigidBody.BodyID,
                    JPH::Vec3(rigidBody.PendingAngularImpulse.x,
                              rigidBody.PendingAngularImpulse.y,
                              rigidBody.PendingAngularImpulse.z));
                rigidBody.PendingAngularImpulse = Math::Vec3(0.0f);
            }

            rigidBody.NeedsSync = false;
        }
    }

    void PhysicsSystem::RemoveDestroyedBodies([[maybe_unused]] Scene& scene)
    {
        PROFILE_FUNCTION();

        // This would be called when entities are destroyed
        // Implementation depends on how entity destruction is handled
        // For now, bodies are cleaned up when the physics world is destroyed
    }

} // namespace ECS
} // namespace Core
