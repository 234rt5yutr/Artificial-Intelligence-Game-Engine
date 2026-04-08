#pragma once

#include "Core/Math/Vec3.h"
#include "Core/Math/Vec4.h"
#include "Core/Math/Transform.h"
#include "Core/Physics/SoftBody/ClothComponent.h"

#include <vector>
#include <cstdint>

namespace Core::Physics {

// ============================================================================
// Collider Type Enumeration
// ============================================================================

enum class ColliderType : uint8_t {
    Sphere,
    Capsule,
    Plane,
    Mesh
};

// ============================================================================
// Collider Data Structures
// ============================================================================

struct SphereData {
    float radius;
};

struct CapsuleData {
    float radius;
    float height;
};

struct PlaneData {
    Vec4 equation;  // ax + by + cz + d = 0 (normal = xyz, distance = w)
};

struct MeshData {
    const void* vertexData;
    const void* indexData;
    uint32_t vertexCount;
    uint32_t triangleCount;
};

// ============================================================================
// Cloth Collider
// ============================================================================

struct ClothCollider {
    ColliderType type;
    Transform worldTransform;
    
    union ColliderData {
        SphereData sphere;
        CapsuleData capsule;
        PlaneData plane;
        MeshData mesh;
        
        ColliderData() : sphere{} {}
        ~ColliderData() {}
    } data;
    
    ClothCollider() : type(ColliderType::Sphere), worldTransform(), data() {}
    ~ClothCollider() = default;
    
    // Factory methods for convenience
    static ClothCollider CreateSphere(const Transform& transform, float radius) {
        ClothCollider collider;
        collider.type = ColliderType::Sphere;
        collider.worldTransform = transform;
        collider.data.sphere.radius = radius;
        return collider;
    }
    
    static ClothCollider CreateCapsule(const Transform& transform, float radius, float height) {
        ClothCollider collider;
        collider.type = ColliderType::Capsule;
        collider.worldTransform = transform;
        collider.data.capsule.radius = radius;
        collider.data.capsule.height = height;
        return collider;
    }
    
    static ClothCollider CreatePlane(const Transform& transform, const Vec4& equation) {
        ClothCollider collider;
        collider.type = ColliderType::Plane;
        collider.worldTransform = transform;
        collider.data.plane.equation = equation;
        return collider;
    }
    
    static ClothCollider CreateMesh(const Transform& transform, 
                                     const void* vertices, 
                                     const void* indices,
                                     uint32_t vertexCount, 
                                     uint32_t triangleCount) {
        ClothCollider collider;
        collider.type = ColliderType::Mesh;
        collider.worldTransform = transform;
        collider.data.mesh.vertexData = vertices;
        collider.data.mesh.indexData = indices;
        collider.data.mesh.vertexCount = vertexCount;
        collider.data.mesh.triangleCount = triangleCount;
        return collider;
    }
};

// ============================================================================
// Collision Contact
// ============================================================================

struct CollisionContact {
    Vec3 point;           // Contact point in world space
    Vec3 normal;          // Contact normal (pointing away from collider)
    float penetration;    // Penetration depth (positive when overlapping)
    uint32_t particleIndex;
    
    CollisionContact()
        : point(0.0f, 0.0f, 0.0f)
        , normal(0.0f, 1.0f, 0.0f)
        , penetration(0.0f)
        , particleIndex(0) {}
    
    CollisionContact(const Vec3& p, const Vec3& n, float pen, uint32_t idx)
        : point(p)
        , normal(n)
        , penetration(pen)
        , particleIndex(idx) {}
};

// ============================================================================
// Cloth Collision Detector (Static Utility Class)
// ============================================================================

class ClothCollisionDetector {
public:
    ClothCollisionDetector() = delete;
    ~ClothCollisionDetector() = delete;
    
    // ========================================================================
    // Main Detection & Resolution Interface
    // ========================================================================
    
    /**
     * @brief Detects all collisions between cloth particles and colliders
     * @param cloth The cloth component to test
     * @param colliders List of colliders to test against
     * @return Vector of collision contacts found
     */
    static std::vector<CollisionContact> DetectCollisions(
        const ClothComponent& cloth,
        const std::vector<ClothCollider>& colliders);
    
    /**
     * @brief Resolves collision contacts by adjusting particle positions
     * @param cloth The cloth component to modify
     * @param contacts The collision contacts to resolve
     */
    static void ResolveCollisions(
        ClothComponent& cloth,
        const std::vector<CollisionContact>& contacts);
    
    // ========================================================================
    // Primitive-Particle Collision Tests
    // ========================================================================
    
    /**
     * @brief Tests a particle against a sphere collider
     * @param particlePos Particle position in world space
     * @param collider Sphere collider to test against
     * @param out Output collision contact if collision detected
     * @return True if collision detected, false otherwise
     */
    static bool TestSphereParticle(
        Vec3 particlePos,
        const ClothCollider& collider,
        CollisionContact& out);
    
    /**
     * @brief Tests a particle against a capsule collider
     * @param particlePos Particle position in world space
     * @param collider Capsule collider to test against
     * @param out Output collision contact if collision detected
     * @return True if collision detected, false otherwise
     */
    static bool TestCapsuleParticle(
        Vec3 particlePos,
        const ClothCollider& collider,
        CollisionContact& out);
    
    /**
     * @brief Tests a particle against a plane collider
     * @param particlePos Particle position in world space
     * @param collider Plane collider to test against
     * @param out Output collision contact if collision detected
     * @return True if collision detected, false otherwise
     */
    static bool TestPlaneParticle(
        Vec3 particlePos,
        const ClothCollider& collider,
        CollisionContact& out);
    
    /**
     * @brief Tests a particle against a mesh collider
     * @param particlePos Particle position in world space
     * @param collider Mesh collider to test against
     * @param out Output collision contact if collision detected
     * @return True if collision detected, false otherwise
     */
    static bool TestMeshParticle(
        Vec3 particlePos,
        const ClothCollider& collider,
        CollisionContact& out);
    
    // ========================================================================
    // Utility Functions
    // ========================================================================
    
    /**
     * @brief Finds the closest point on a line segment to a given point
     * @param point The query point
     * @param a Start of the line segment
     * @param b End of the line segment
     * @return The closest point on segment [a, b] to the query point
     */
    static Vec3 ClosestPointOnSegment(Vec3 point, Vec3 a, Vec3 b);
    
    /**
     * @brief Computes friction response for a velocity against a surface
     * @param velocity The incoming velocity
     * @param normal The surface normal
     * @param friction Friction coefficient (0 = no friction, 1 = full friction)
     * @return Adjusted velocity after friction is applied
     */
    static Vec3 ComputeFriction(Vec3 velocity, Vec3 normal, float friction);

private:
    // ========================================================================
    // Internal Helper Functions
    // ========================================================================
    
    /**
     * @brief Dispatches collision test based on collider type
     */
    static bool TestParticleCollider(
        Vec3 particlePos,
        const ClothCollider& collider,
        CollisionContact& out);
};

} // namespace Core::Physics
