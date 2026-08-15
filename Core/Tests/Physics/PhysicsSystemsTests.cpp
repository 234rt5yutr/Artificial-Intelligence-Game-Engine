// PhysicsSystemsTests: Comprehensive unit tests for Jolt Physics integration,
// PhysicsWorld initialization, layer collision matrix, body simulation,
// ECS sync, forces, raycasting, and origin rebasing.

#include "Core/Physics/PhysicsWorld.h"
#include "Core/Physics/PhysicsLayers.h"
#include "Core/Physics/Constraints/ConstraintHelpers.h"
#include "Core/Physics/Constraints/ConstraintTypes.h"
#include "Core/ECS/Scene.h"
#include "Core/ECS/Entity.h"
#include "Core/ECS/Components/Components.h"
#include "Core/ECS/Systems/PhysicsSystem.h"
#include "Core/ECS/Systems/PhysicsSyncSystem.h"
#include "Core/Log.h"

#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n",                 \
                         #expr, __FILE__, __LINE__);                           \
            std::abort();                                                      \
        }                                                                      \
    } while (false)

int main() {
    Engine::Log::Init();

    using namespace Core::Physics;
    using namespace Core::ECS;

    // 1. PhysicsWorld Lifecycle & Configuration
    {
        PhysicsWorld world;
        CHECK(!world.IsInitialized());

        PhysicsConfig config;
        config.MaxBodies = 2048;
        config.GravityY = -9.81f;
        config.UseEngineJobSystem = false; // Use built-in thread pool for test isolation

        bool initOk = world.Initialize(config);
        CHECK(initOk);
        CHECK(world.IsInitialized());

        JPH::Vec3 g = world.GetGravity();
        CHECK(std::fabs(g.GetY() - (-9.81f)) < 0.001f);

        world.SetGravity(JPH::Vec3(0.0f, -19.62f, 0.0f));
        CHECK(std::fabs(world.GetGravity().GetY() - (-19.62f)) < 0.001f);

        world.Shutdown();
        CHECK(!world.IsInitialized());
    }

    // 2. Collision Matrix and Layer Filtering
    {
        ObjectLayerPairFilterImpl layerFilter;

        // Static vs Static -> No collision
        CHECK(!layerFilter.ShouldCollide(Layers::NON_MOVING, Layers::NON_MOVING));

        // Static vs Moving -> Collision
        CHECK(layerFilter.ShouldCollide(Layers::NON_MOVING, Layers::MOVING));
        CHECK(layerFilter.ShouldCollide(Layers::MOVING, Layers::NON_MOVING));

        // Static vs Debris -> Collision
        CHECK(layerFilter.ShouldCollide(Layers::NON_MOVING, Layers::DEBRIS));

        // Moving vs Moving -> Collision
        CHECK(layerFilter.ShouldCollide(Layers::MOVING, Layers::MOVING));

        // Moving vs Debris -> Collision
        CHECK(layerFilter.ShouldCollide(Layers::MOVING, Layers::DEBRIS));

        // Debris vs Debris -> No collision (perf optimization)
        CHECK(!layerFilter.ShouldCollide(Layers::DEBRIS, Layers::DEBRIS));

        // Sensor vs Moving -> Collision (trigger)
        CHECK(layerFilter.ShouldCollide(Layers::SENSOR, Layers::MOVING));

        // Sensor vs Static -> No collision
        CHECK(!layerFilter.ShouldCollide(Layers::SENSOR, Layers::NON_MOVING));

        // BroadPhase Layer Interface
        BroadPhaseLayerInterfaceImpl bpInterface;
        CHECK(bpInterface.GetNumBroadPhaseLayers() == BroadPhaseLayers::NUM_LAYERS);
        CHECK(bpInterface.GetBroadPhaseLayer(Layers::NON_MOVING) == BroadPhaseLayers::NON_MOVING);
        CHECK(bpInterface.GetBroadPhaseLayer(Layers::MOVING) == BroadPhaseLayers::MOVING);
        CHECK(bpInterface.GetBroadPhaseLayer(Layers::DEBRIS) == BroadPhaseLayers::DEBRIS);
        CHECK(bpInterface.GetBroadPhaseLayer(Layers::SENSOR) == BroadPhaseLayers::SENSOR);
    }

    // 3. ECS Body Creation, Simulation & PhysicsSyncSystem
    {
        PhysicsWorld world;
        PhysicsConfig config;
        config.GravityY = -9.81f;
        config.UseEngineJobSystem = false;
        CHECK(world.Initialize(config));

        PhysicsSystem physicsSystem;
        physicsSystem.Initialize(&world);

        PhysicsSyncSystem syncSystem;
        syncSystem.Initialize(&world);

        Scene scene("PhysicsSimScene");

        // Create static ground box at y = 0
        auto ground = scene.CreateEntity("Ground");
        auto& groundTf = ground.AddComponent<TransformComponent>();
        groundTf.Position = Core::Math::Vec3(0.0f, 0.0f, 0.0f);

        auto& groundRb = ground.AddComponent<RigidBodyComponent>();
        groundRb.Type = MotionType::Static;

        auto& groundCol = ground.AddComponent<ColliderComponent>();
        groundCol.Type = Core::ECS::ColliderType::Box;
        groundCol.Layer = Layers::NON_MOVING;
        groundCol.ShapeData = BoxColliderData{ Core::Math::Vec3(25.0f, 0.5f, 25.0f) }; // half extents

        // Create dynamic falling box at y = 10
        auto box = scene.CreateEntity("FallingBox");
        auto& boxTf = box.AddComponent<TransformComponent>();
        boxTf.Position = Core::Math::Vec3(0.0f, 10.0f, 0.0f);

        auto& boxRb = box.AddComponent<RigidBodyComponent>();
        boxRb.Type = MotionType::Dynamic;
        boxRb.Mass = 5.0f;

        auto& boxCol = box.AddComponent<ColliderComponent>();
        boxCol.Type = Core::ECS::ColliderType::Box;
        boxCol.Layer = Layers::MOVING;
        boxCol.ShapeData = BoxColliderData{ Core::Math::Vec3(0.5f, 0.5f, 0.5f) };

        // PreUpdate creates the Jolt bodies
        physicsSystem.PreUpdate(scene);
        CHECK(groundRb.IsBodyCreated);
        CHECK(boxRb.IsBodyCreated);
        CHECK(world.GetBodyInterface().IsAdded(groundRb.BodyID));
        CHECK(world.GetBodyInterface().IsAdded(boxRb.BodyID));

        // Step simulation for 60 frames (1 second total at 60 Hz)
        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 60; ++i) {
            physicsSystem.Update(dt);
        }

        // Sync transforms back to ECS
        syncSystem.Update(scene);

        // Ground top face is at y = 0.5, box half-height is 0.5 -> resting position around y = 1.0
        CHECK(boxTf.Position.y < 9.0f); // definitely fell
        CHECK(boxTf.Position.y >= 0.8f); // rested on top of ground without falling through
        CHECK(boxTf.IsDirty);

        physicsSystem.Shutdown();
        syncSystem.Shutdown();
        world.Shutdown();
    }

    // 4. Force, Impulse, and Velocity
    {
        PhysicsWorld world;
        PhysicsConfig config;
        config.GravityY = 0.0f; // zero gravity to isolate impulse
        config.UseEngineJobSystem = false;
        CHECK(world.Initialize(config));

        PhysicsSystem physicsSystem;
        physicsSystem.Initialize(&world);

        Scene scene("ForceTestScene");
        auto projectile = scene.CreateEntity("Projectile");
        auto& tf = projectile.AddComponent<TransformComponent>();
        tf.Position = Core::Math::Vec3(0.0f, 0.0f, 0.0f);

        auto& rb = projectile.AddComponent<RigidBodyComponent>();
        rb.Type = MotionType::Dynamic;
        rb.Mass = 2.0f;

        auto& col = projectile.AddComponent<ColliderComponent>();
        col.Type = Core::ECS::ColliderType::Sphere;
        col.Layer = Layers::MOVING;
        col.ShapeData = SphereColliderData{ 0.5f };

        // Create body
        physicsSystem.PreUpdate(scene);
        CHECK(rb.IsBodyCreated);

        // Apply linear impulse of 10 along +X (mass 2 -> velocity 5 m/s)
        rb.ApplyLinearImpulse(Core::Math::Vec3(10.0f, 0.0f, 0.0f));
        physicsSystem.PreUpdate(scene);

        JPH::Vec3 vel = world.GetBodyInterface().GetLinearVelocity(rb.BodyID);
        CHECK(std::fabs(vel.GetX() - 5.0f) < 0.01f);
        CHECK(std::fabs(vel.GetY()) < 0.01f);

        physicsSystem.Shutdown();
        world.Shutdown();
    }

    // 5. NarrowPhase Raycast Query
    {
        PhysicsWorld world;
        PhysicsConfig config;
        config.UseEngineJobSystem = false;
        CHECK(world.Initialize(config));

        PhysicsSystem physicsSystem;
        physicsSystem.Initialize(&world);

        Scene scene("RaycastScene");
        auto target = scene.CreateEntity("TargetBox");
        auto& tf = target.AddComponent<TransformComponent>();
        tf.Position = Core::Math::Vec3(0.0f, 5.0f, 0.0f);

        auto& rb = target.AddComponent<RigidBodyComponent>();
        rb.Type = MotionType::Static;

        auto& col = target.AddComponent<ColliderComponent>();
        col.Type = Core::ECS::ColliderType::Box;
        col.Layer = Layers::NON_MOVING;
        col.ShapeData = BoxColliderData{ Core::Math::Vec3(1.0f, 1.0f, 1.0f) };

        physicsSystem.PreUpdate(scene);

        // Optimize broadphase for fast query
        world.GetPhysicsSystem().OptimizeBroadPhase();

        // Cast ray from (0, 10, 0) downwards towards (0, 0, 0)
        JPH::RRayCast ray(JPH::RVec3(0.0f, 10.0f, 0.0f), JPH::Vec3(0.0f, -10.0f, 0.0f));
        JPH::RayCastResult hit;

        bool hasHit = world.GetPhysicsSystem().GetNarrowPhaseQuery().CastRay(ray, hit);
        CHECK(hasHit);
        CHECK(hit.mBodyID == rb.BodyID);
        // Ray origin at 10, box top face at 6 -> hit at fraction (10 - 6) / 10 = 0.4
        CHECK(std::fabs(hit.mFraction - 0.4f) < 0.01f);

        physicsSystem.Shutdown();
        world.Shutdown();
    }

    // 6. Origin Rebasing / Body Shifting
    {
        PhysicsWorld world;
        PhysicsConfig config;
        config.UseEngineJobSystem = false;
        CHECK(world.Initialize(config));

        PhysicsSystem physicsSystem;
        physicsSystem.Initialize(&world);

        Scene scene("ShiftScene");
        auto entity = scene.CreateEntity("Object");
        auto& tf = entity.AddComponent<TransformComponent>();
        tf.Position = Core::Math::Vec3(10.0f, 20.0f, 30.0f);

        auto& rb = entity.AddComponent<RigidBodyComponent>();
        rb.Type = MotionType::Dynamic;
        rb.Mass = 1.0f;

        auto& col = entity.AddComponent<ColliderComponent>();
        col.Type = Core::ECS::ColliderType::Sphere;
        col.Layer = Layers::MOVING;
        col.ShapeData = SphereColliderData{ 1.0f };

        physicsSystem.PreUpdate(scene);

        JPH::RVec3 beforePos = world.GetBodyInterface().GetPosition(rb.BodyID);
        CHECK(std::fabs(beforePos.GetX() - 10.0f) < 0.001f);

        // Shift by (100, -50, 200)
        bool shiftOk = world.ShiftBody(rb.BodyID, Core::Math::Vec3(100.0f, -50.0f, 200.0f));
        CHECK(shiftOk);

        JPH::RVec3 afterPos = world.GetBodyInterface().GetPosition(rb.BodyID);
        CHECK(std::fabs(afterPos.GetX() - 110.0f) < 0.001f);
        CHECK(std::fabs(afterPos.GetY() - (-30.0f)) < 0.001f);
        CHECK(std::fabs(afterPos.GetZ() - 230.0f) < 0.001f);

        physicsSystem.Shutdown();
        world.Shutdown();
    }

    // 7. Constraint Helper Math Conversions
    {
        Core::Math::Vec3 v(1.5f, -2.5f, 3.5f);
        JPH::Vec3 jv = JoltConstraintHelpers::ToJPHVec3(v);
        CHECK(std::fabs(jv.GetX() - 1.5f) < 0.0001f);
        CHECK(std::fabs(jv.GetY() - (-2.5f)) < 0.0001f);
        CHECK(std::fabs(jv.GetZ() - 3.5f) < 0.0001f);

        Core::Math::Vec3 backV = JoltConstraintHelpers::FromJPHVec3(jv);
        CHECK(std::fabs(backV.x - 1.5f) < 0.0001f);
        CHECK(std::fabs(backV.y - (-2.5f)) < 0.0001f);
        CHECK(std::fabs(backV.z - 3.5f) < 0.0001f);

        Core::Math::Quat q = glm::angleAxis(glm::radians(90.0f), Core::Math::Vec3(0.0f, 1.0f, 0.0f));
        JPH::Quat jq = JoltConstraintHelpers::ToJPHQuat(q);
        Core::Math::Quat backQ = JoltConstraintHelpers::FromJPHQuat(jq);
        CHECK(std::fabs(backQ.w - q.w) < 0.001f);
        CHECK(std::fabs(backQ.y - q.y) < 0.001f);
    }

    std::printf("PhysicsSystemsTests: ALL TESTS PASSED!\n");
    return 0;
}
