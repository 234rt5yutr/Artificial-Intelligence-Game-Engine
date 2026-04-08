#pragma once

/**
 * @file SoftBodyComponent.h
 * @brief ECS component for soft body simulation using Position-Based Dynamics (PBD)
 * 
 * Implements volumetric soft body physics with pressure-based deformation,
 * tetrahedral volume constraints, and surface collision handling. Supports
 * various soft body types including volumetric, pressure-based, and tetrahedral meshes.
 * 
 * Based on Position-Based Dynamics (PBD) with extended support for:
 * - Volume preservation constraints
 * - Pressure-based inflation/deflation
 * - Self-collision detection
 * - Tetrahedral mesh decomposition
 */

#include "Core/Math/Math.h"
#include "Core/ECS/Components/ClothComponent.h"
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace Core {
namespace ECS {

    //=========================================================================
    // Soft Body Types
    //=========================================================================

    /**
     * @brief Classification of soft body simulation types
     * 
     * Different soft body types use different constraint solving strategies
     * and are suited for different use cases.
     */
    enum class SoftBodyType : uint8_t {
        Volumetric,     ///< General volumetric soft body with volume preservation
        Pressure,       ///< Pressure-based soft body (balloons, inflatable objects)
        Tetrahedral     ///< Tetrahedral mesh-based soft body (high-fidelity simulation)
    };

    //=========================================================================
    // Soft Body Particle
    //=========================================================================

    /**
     * @brief Represents a single particle in the soft body simulation
     * 
     * Each particle maintains its own position, velocity, and mass properties.
     * Particles can be pinned to create attachment points or fixed regions.
     */
    struct SoftBodyParticle {
        Math::Vec3 Position{ 0.0f, 0.0f, 0.0f };           ///< Current world position
        Math::Vec3 PreviousPosition{ 0.0f, 0.0f, 0.0f };   ///< Previous frame position (for Verlet integration)
        Math::Vec3 Velocity{ 0.0f, 0.0f, 0.0f };           ///< Current velocity
        float InverseMass = 1.0f;                           ///< Inverse mass (0 = infinite mass / pinned)
        bool IsPinned = false;                              ///< Whether this particle is fixed in place

        SoftBodyParticle() = default;

        /**
         * @brief Construct a particle at the given position
         * @param position Initial world position
         * @param mass Particle mass (converted to inverse mass internally)
         */
        SoftBodyParticle(const Math::Vec3& position, float mass = 1.0f)
            : Position(position)
            , PreviousPosition(position)
            , Velocity(0.0f, 0.0f, 0.0f)
            , InverseMass(mass > 0.0f ? 1.0f / mass : 0.0f)
            , IsPinned(false) {}

        /**
         * @brief Get the effective mass of the particle
         * @return Mass value (returns very large value if pinned)
         */
        float GetMass() const {
            return InverseMass > 0.0f ? 1.0f / InverseMass : 1e10f;
        }

        /**
         * @brief Set the mass of the particle
         * @param mass New mass value (0 or negative pins the particle)
         */
        void SetMass(float mass) {
            InverseMass = mass > 0.0f ? 1.0f / mass : 0.0f;
        }
    };

    //=========================================================================
    // Volume Constraint
    //=========================================================================

    /**
     * @brief Represents a tetrahedral volume constraint
     * 
     * Volume constraints maintain the volume of a tetrahedron formed by four
     * particles. This is essential for preserving the overall shape and volume
     * of volumetric soft bodies.
     */
    struct VolumeConstraint {
        uint32_t ParticleIndices[4] = { 0, 0, 0, 0 };  ///< Indices of the four tetrahedron vertices
        float RestVolume = 0.0f;                        ///< Target volume at rest state
        float Stiffness = 1.0f;                         ///< Constraint stiffness (0-1)

        VolumeConstraint() = default;

        /**
         * @brief Construct a volume constraint for four particles
         * @param i0, i1, i2, i3 Particle indices forming the tetrahedron
         * @param restVol Rest volume of the tetrahedron
         * @param stiffness Constraint stiffness factor
         */
        VolumeConstraint(uint32_t i0, uint32_t i1, uint32_t i2, uint32_t i3,
                         float restVol, float stiffness = 1.0f)
            : RestVolume(restVol)
            , Stiffness(stiffness) {
            ParticleIndices[0] = i0;
            ParticleIndices[1] = i1;
            ParticleIndices[2] = i2;
            ParticleIndices[3] = i3;
        }

        /**
         * @brief Calculate the signed volume of the tetrahedron
         * @param p0, p1, p2, p3 Positions of the four vertices
         * @return Signed volume (positive if vertices are in correct winding order)
         */
        static float CalculateTetrahedronVolume(const Math::Vec3& p0, const Math::Vec3& p1,
                                                 const Math::Vec3& p2, const Math::Vec3& p3) {
            // Volume = (1/6) * |det([p1-p0, p2-p0, p3-p0])|
            Math::Vec3 e1 = p1 - p0;
            Math::Vec3 e2 = p2 - p0;
            Math::Vec3 e3 = p3 - p0;
            return glm::dot(e1, glm::cross(e2, e3)) / 6.0f;
        }
    };

    //=========================================================================
    // Soft Body Component
    //=========================================================================

    /**
     * @brief Component for soft body physics simulation
     * 
     * Implements Position-Based Dynamics (PBD) for deformable volumetric objects.
     * Supports various soft body configurations including pressure-based objects
     * (balloons), volumetric solids (jelly), and tetrahedral meshes.
     * 
     * Usage:
     * @code
     * auto& softBody = entity.AddComponent<SoftBodyComponent>();
     * softBody = SoftBodyComponent::CreateSphere(0.5f, 3);
     * softBody.PressureCoefficient = 1.5f;
     * softBody.IsActive = true;
     * @endcode
     */
    struct SoftBodyComponent {
        //=====================================================================
        // Configuration
        //=====================================================================

        SoftBodyType Type = SoftBodyType::Volumetric;   ///< Type of soft body simulation

        //=====================================================================
        // Particle Data
        //=====================================================================

        std::vector<SoftBodyParticle> Particles;        ///< All particles in the soft body

        //=====================================================================
        // Constraints
        //=====================================================================

        std::vector<ClothConstraint> DistanceConstraints;   ///< Distance constraints between particles
        std::vector<VolumeConstraint> VolumeConstraints;    ///< Tetrahedral volume constraints

        //=====================================================================
        // Pressure Properties (for SoftBodyType::Pressure)
        //=====================================================================

        float PressureCoefficient = 1.0f;   ///< Pressure multiplier (>1 inflates, <1 deflates)

        //=====================================================================
        // Volume Properties
        //=====================================================================

        float VolumeStiffness = 0.9f;       ///< Stiffness of volume preservation (0-1)

        //=====================================================================
        // Surface Properties
        //=====================================================================

        float SurfaceFriction = 0.3f;       ///< Surface friction coefficient for collisions

        //=====================================================================
        // Physics Properties
        //=====================================================================

        float Damping = 0.98f;                              ///< Velocity damping factor (0-1)
        Math::Vec3 Gravity{ 0.0f, -9.81f, 0.0f };           ///< Gravity acceleration vector
        float ParticleMass = 1.0f;                          ///< Default mass per particle

        //=====================================================================
        // Solver Configuration
        //=====================================================================

        uint32_t SolverIterations = 8;      ///< Constraint solver iterations per frame

        //=====================================================================
        // Self-Collision
        //=====================================================================

        bool EnableSelfCollision = false;   ///< Enable particle-to-particle collision
        float SelfCollisionRadius = 0.05f;  ///< Radius for self-collision detection

        //=====================================================================
        // State
        //=====================================================================

        bool IsActive = true;               ///< Whether simulation is active

        //=====================================================================
        // Cached Values
        //=====================================================================

        float CachedRestVolume = 0.0f;      ///< Cached total rest volume (internal use)

        //=====================================================================
        // Constructor
        //=====================================================================

        SoftBodyComponent() = default;

        //=====================================================================
        // Initialization Methods
        //=====================================================================

        /**
         * @brief Initialize the soft body from positions and tetrahedral indices
         * 
         * Creates particles from positions and generates volume constraints from
         * the tetrahedral connectivity. Also generates distance constraints for
         * all edges of the tetrahedra.
         * 
         * @param positions Vertex positions for all particles
         * @param tetrahedra Flat array of indices (4 per tetrahedron)
         */
        void Initialize(const std::vector<Math::Vec3>& positions,
                        const std::vector<uint32_t>& tetrahedra) {
            // Create particles from positions
            Particles.clear();
            Particles.reserve(positions.size());
            for (const auto& pos : positions) {
                Particles.emplace_back(pos, ParticleMass);
            }

            // Generate volume constraints from tetrahedra
            VolumeConstraints.clear();
            uint32_t tetraCount = static_cast<uint32_t>(tetrahedra.size()) / 4;
            VolumeConstraints.reserve(tetraCount);

            // Track unique edges for distance constraints
            std::vector<std::pair<uint32_t, uint32_t>> edges;

            for (uint32_t t = 0; t < tetraCount; ++t) {
                uint32_t i0 = tetrahedra[t * 4 + 0];
                uint32_t i1 = tetrahedra[t * 4 + 1];
                uint32_t i2 = tetrahedra[t * 4 + 2];
                uint32_t i3 = tetrahedra[t * 4 + 3];

                // Calculate rest volume for this tetrahedron
                float restVol = std::abs(VolumeConstraint::CalculateTetrahedronVolume(
                    positions[i0], positions[i1], positions[i2], positions[i3]));

                VolumeConstraints.emplace_back(i0, i1, i2, i3, restVol, VolumeStiffness);

                // Collect all 6 edges of the tetrahedron
                auto addEdge = [&edges](uint32_t a, uint32_t b) {
                    edges.emplace_back(std::min(a, b), std::max(a, b));
                };
                addEdge(i0, i1);
                addEdge(i0, i2);
                addEdge(i0, i3);
                addEdge(i1, i2);
                addEdge(i1, i3);
                addEdge(i2, i3);
            }

            // Remove duplicate edges and create distance constraints
            std::sort(edges.begin(), edges.end());
            edges.erase(std::unique(edges.begin(), edges.end()), edges.end());

            DistanceConstraints.clear();
            DistanceConstraints.reserve(edges.size());
            for (const auto& edge : edges) {
                float restLength = glm::length(positions[edge.second] - positions[edge.first]);
                DistanceConstraints.emplace_back(edge.first, edge.second, restLength, 1.0f);
            }

            // Cache rest volume
            CachedRestVolume = CalculateRestVolume();
        }

        //=====================================================================
        // Volume Calculation Methods
        //=====================================================================

        /**
         * @brief Calculate the total rest volume from all volume constraints
         * @return Total rest volume of the soft body
         */
        float CalculateRestVolume() const {
            float totalVolume = 0.0f;
            for (const auto& constraint : VolumeConstraints) {
                totalVolume += std::abs(constraint.RestVolume);
            }
            return totalVolume;
        }

        /**
         * @brief Calculate the current total volume from particle positions
         * @return Current volume of the soft body
         */
        float CalculateCurrentVolume() const {
            float totalVolume = 0.0f;
            for (const auto& constraint : VolumeConstraints) {
                const auto& p0 = Particles[constraint.ParticleIndices[0]].Position;
                const auto& p1 = Particles[constraint.ParticleIndices[1]].Position;
                const auto& p2 = Particles[constraint.ParticleIndices[2]].Position;
                const auto& p3 = Particles[constraint.ParticleIndices[3]].Position;
                totalVolume += std::abs(VolumeConstraint::CalculateTetrahedronVolume(p0, p1, p2, p3));
            }
            return totalVolume;
        }

        //=====================================================================
        // Pressure Methods
        //=====================================================================

        /**
         * @brief Apply internal pressure forces to all particles
         * 
         * For pressure-based soft bodies, this applies outward forces based on
         * the surface normals and the pressure coefficient.
         * 
         * @param pressure Pressure value to apply (typically PressureCoefficient)
         */
        void ApplyPressure(float pressure) {
            if (Type != SoftBodyType::Pressure || VolumeConstraints.empty()) {
                return;
            }

            // Calculate pressure-based forces for each surface triangle
            // For each tetrahedron, we apply pressure to its faces
            for (const auto& constraint : VolumeConstraints) {
                const uint32_t* indices = constraint.ParticleIndices;

                // Each tetrahedron has 4 triangular faces
                // Face indices: (0,1,2), (0,1,3), (0,2,3), (1,2,3)
                auto applyFacePressure = [this, pressure](uint32_t i0, uint32_t i1, uint32_t i2) {
                    const auto& p0 = Particles[i0].Position;
                    const auto& p1 = Particles[i1].Position;
                    const auto& p2 = Particles[i2].Position;

                    // Calculate face normal and area
                    Math::Vec3 edge1 = p1 - p0;
                    Math::Vec3 edge2 = p2 - p0;
                    Math::Vec3 normal = glm::cross(edge1, edge2);
                    float area = glm::length(normal) * 0.5f;

                    if (area > 1e-6f) {
                        normal = glm::normalize(normal);
                        Math::Vec3 force = normal * (pressure * area / 3.0f);

                        // Apply force to each vertex of the face
                        if (Particles[i0].InverseMass > 0.0f) {
                            Particles[i0].Velocity += force * Particles[i0].InverseMass;
                        }
                        if (Particles[i1].InverseMass > 0.0f) {
                            Particles[i1].Velocity += force * Particles[i1].InverseMass;
                        }
                        if (Particles[i2].InverseMass > 0.0f) {
                            Particles[i2].Velocity += force * Particles[i2].InverseMass;
                        }
                    }
                };

                applyFacePressure(indices[0], indices[1], indices[2]);
                applyFacePressure(indices[0], indices[1], indices[3]);
                applyFacePressure(indices[0], indices[2], indices[3]);
                applyFacePressure(indices[1], indices[2], indices[3]);
            }
        }

        //=====================================================================
        // Particle Control Methods
        //=====================================================================

        /**
         * @brief Pin or unpin a particle at the given index
         * @param index Particle index
         * @param pinned Whether to pin (true) or unpin (false)
         */
        void PinParticle(uint32_t index, bool pinned) {
            if (index >= Particles.size()) return;

            Particles[index].IsPinned = pinned;
            if (pinned) {
                Particles[index].InverseMass = 0.0f;
                Particles[index].Velocity = Math::Vec3(0.0f);
            } else {
                Particles[index].InverseMass = ParticleMass > 0.0f ? 1.0f / ParticleMass : 1.0f;
            }
        }

        //=====================================================================
        // Query Methods
        //=====================================================================

        /**
         * @brief Calculate the center of mass of the soft body
         * @return Center of mass position in world space
         */
        Math::Vec3 GetCenterOfMass() const {
            if (Particles.empty()) {
                return Math::Vec3(0.0f);
            }

            Math::Vec3 centerOfMass(0.0f);
            float totalMass = 0.0f;

            for (const auto& particle : Particles) {
                float mass = particle.GetMass();
                centerOfMass += particle.Position * mass;
                totalMass += mass;
            }

            return totalMass > 0.0f ? centerOfMass / totalMass : Math::Vec3(0.0f);
        }

        //=====================================================================
        // Reset Methods
        //=====================================================================

        /**
         * @brief Reset the soft body to its initial state
         * 
         * Resets all particle velocities to zero and restores positions to
         * their previous positions (approximating initial state).
         */
        void Reset() {
            for (auto& particle : Particles) {
                particle.Velocity = Math::Vec3(0.0f);
                particle.Position = particle.PreviousPosition;
            }
        }

        //=====================================================================
        // Factory Methods
        //=====================================================================

        /**
         * @brief Create a soft body sphere with tetrahedral mesh
         * @param radius Sphere radius
         * @param subdivisions Number of subdivisions (higher = more detail)
         * @return Configured SoftBodyComponent
         */
        static SoftBodyComponent CreateSphere(float radius, uint32_t subdivisions) {
            SoftBodyComponent softBody;
            softBody.Type = SoftBodyType::Pressure;

            // Generate icosphere vertices
            std::vector<Math::Vec3> positions;
            std::vector<uint32_t> tetrahedra;

            // Start with icosahedron vertices
            const float phi = (1.0f + std::sqrt(5.0f)) / 2.0f;  // Golden ratio
            const float scale = radius / std::sqrt(1.0f + phi * phi);

            // 12 vertices of icosahedron
            std::vector<Math::Vec3> icoVerts = {
                {-1.0f,  phi, 0.0f}, { 1.0f,  phi, 0.0f}, {-1.0f, -phi, 0.0f}, { 1.0f, -phi, 0.0f},
                {0.0f, -1.0f,  phi}, {0.0f,  1.0f,  phi}, {0.0f, -1.0f, -phi}, {0.0f,  1.0f, -phi},
                { phi, 0.0f, -1.0f}, { phi, 0.0f,  1.0f}, {-phi, 0.0f, -1.0f}, {-phi, 0.0f,  1.0f}
            };

            // Normalize and scale vertices
            for (auto& v : icoVerts) {
                v = glm::normalize(v) * radius;
            }

            // Add center point for tetrahedralization
            positions.push_back(Math::Vec3(0.0f));  // Center at index 0

            // Add surface vertices
            for (const auto& v : icoVerts) {
                positions.push_back(v);
            }

            // Subdivide if requested
            for (uint32_t s = 0; s < subdivisions; ++s) {
                std::vector<Math::Vec3> newPositions = { positions[0] };  // Keep center
                
                // Subdivide each surface vertex by adding midpoints
                uint32_t surfaceStart = 1;
                uint32_t surfaceCount = static_cast<uint32_t>(positions.size()) - 1;
                
                for (uint32_t i = surfaceStart; i < surfaceStart + surfaceCount; ++i) {
                    newPositions.push_back(positions[i]);
                }

                // Add midpoints between adjacent vertices on surface
                for (uint32_t i = surfaceStart; i < surfaceStart + surfaceCount; ++i) {
                    for (uint32_t j = i + 1; j < surfaceStart + surfaceCount; ++j) {
                        float dist = glm::length(positions[i] - positions[j]);
                        if (dist < radius * 0.8f) {  // Adjacent vertices
                            Math::Vec3 midpoint = (positions[i] + positions[j]) * 0.5f;
                            midpoint = glm::normalize(midpoint) * radius;
                            newPositions.push_back(midpoint);
                        }
                    }
                }

                positions = std::move(newPositions);
            }

            // Generate tetrahedra connecting center to surface triangles
            // Simple tetrahedralization: connect center to each surface triangle
            uint32_t surfaceCount = static_cast<uint32_t>(positions.size()) - 1;

            // Create tetrahedra by connecting nearby surface vertices with center
            for (uint32_t i = 1; i <= surfaceCount; ++i) {
                for (uint32_t j = i + 1; j <= surfaceCount; ++j) {
                    for (uint32_t k = j + 1; k <= surfaceCount; ++k) {
                        // Check if these form a valid surface triangle (nearby vertices)
                        float d1 = glm::length(positions[i] - positions[j]);
                        float d2 = glm::length(positions[j] - positions[k]);
                        float d3 = glm::length(positions[i] - positions[k]);
                        float threshold = radius * 1.2f / static_cast<float>(subdivisions + 1);

                        if (d1 < threshold && d2 < threshold && d3 < threshold) {
                            tetrahedra.push_back(0);  // Center
                            tetrahedra.push_back(i);
                            tetrahedra.push_back(j);
                            tetrahedra.push_back(k);
                        }
                    }
                }
            }

            // If no tetrahedra were generated, create a simple fallback
            if (tetrahedra.empty() && positions.size() >= 4) {
                for (uint32_t i = 1; i + 2 < positions.size(); ++i) {
                    tetrahedra.push_back(0);
                    tetrahedra.push_back(i);
                    tetrahedra.push_back(i + 1);
                    tetrahedra.push_back(i + 2);
                }
            }

            softBody.Initialize(positions, tetrahedra);
            softBody.PressureCoefficient = 1.0f;
            softBody.VolumeStiffness = 0.9f;
            softBody.Damping = 0.98f;
            softBody.SolverIterations = 8;

            return softBody;
        }

        /**
         * @brief Create a soft body cube with tetrahedral mesh
         * @param size Cube size (edge length)
         * @param subdivisions Number of subdivisions per edge
         * @return Configured SoftBodyComponent
         */
        static SoftBodyComponent CreateCube(float size, uint32_t subdivisions) {
            SoftBodyComponent softBody;
            softBody.Type = SoftBodyType::Volumetric;

            subdivisions = std::max(1u, subdivisions);
            uint32_t pointsPerEdge = subdivisions + 1;
            float step = size / static_cast<float>(subdivisions);
            float halfSize = size * 0.5f;

            std::vector<Math::Vec3> positions;
            positions.reserve(pointsPerEdge * pointsPerEdge * pointsPerEdge);

            // Generate grid of vertices
            for (uint32_t z = 0; z < pointsPerEdge; ++z) {
                for (uint32_t y = 0; y < pointsPerEdge; ++y) {
                    for (uint32_t x = 0; x < pointsPerEdge; ++x) {
                        positions.emplace_back(
                            x * step - halfSize,
                            y * step - halfSize,
                            z * step - halfSize
                        );
                    }
                }
            }

            // Generate tetrahedra for each cube cell (6 tetrahedra per cube)
            std::vector<uint32_t> tetrahedra;

            auto getIndex = [pointsPerEdge](uint32_t x, uint32_t y, uint32_t z) -> uint32_t {
                return z * pointsPerEdge * pointsPerEdge + y * pointsPerEdge + x;
            };

            for (uint32_t z = 0; z < subdivisions; ++z) {
                for (uint32_t y = 0; y < subdivisions; ++y) {
                    for (uint32_t x = 0; x < subdivisions; ++x) {
                        // 8 corners of the cube cell
                        uint32_t v0 = getIndex(x,     y,     z    );
                        uint32_t v1 = getIndex(x + 1, y,     z    );
                        uint32_t v2 = getIndex(x + 1, y + 1, z    );
                        uint32_t v3 = getIndex(x,     y + 1, z    );
                        uint32_t v4 = getIndex(x,     y,     z + 1);
                        uint32_t v5 = getIndex(x + 1, y,     z + 1);
                        uint32_t v6 = getIndex(x + 1, y + 1, z + 1);
                        uint32_t v7 = getIndex(x,     y + 1, z + 1);

                        // Decompose cube into 6 tetrahedra (consistent diagonal)
                        // Using the "6-tetrahedra" decomposition
                        tetrahedra.insert(tetrahedra.end(), { v0, v1, v2, v5 });
                        tetrahedra.insert(tetrahedra.end(), { v0, v2, v3, v7 });
                        tetrahedra.insert(tetrahedra.end(), { v0, v5, v4, v7 });
                        tetrahedra.insert(tetrahedra.end(), { v2, v5, v6, v7 });
                        tetrahedra.insert(tetrahedra.end(), { v0, v2, v5, v7 });
                        tetrahedra.insert(tetrahedra.end(), { v0, v3, v4, v7 });
                    }
                }
            }

            softBody.Initialize(positions, tetrahedra);
            softBody.VolumeStiffness = 0.95f;
            softBody.Damping = 0.97f;
            softBody.SolverIterations = 10;

            return softBody;
        }

        /**
         * @brief Create a soft body from mesh vertex and index data
         * 
         * Generates a tetrahedral mesh from surface mesh data using a simple
         * tetrahedralization approach (center point + surface triangles).
         * 
         * @param vertexData Pointer to vertex position data (float[3] per vertex)
         * @param indexData Pointer to index data (uint32_t[3] per triangle)
         * @param vertexCount Number of vertices
         * @param triangleCount Number of triangles
         * @return Configured SoftBodyComponent
         */
        static SoftBodyComponent CreateFromMesh(const void* vertexData, const void* indexData,
                                                 uint32_t vertexCount, uint32_t triangleCount) {
            SoftBodyComponent softBody;
            softBody.Type = SoftBodyType::Tetrahedral;

            if (!vertexData || !indexData || vertexCount < 4 || triangleCount < 1) {
                return softBody;
            }

            const float* vertices = static_cast<const float*>(vertexData);
            const uint32_t* indices = static_cast<const uint32_t*>(indexData);

            std::vector<Math::Vec3> positions;
            positions.reserve(vertexCount + 1);

            // Calculate center of mass for interior point
            Math::Vec3 center(0.0f);
            for (uint32_t i = 0; i < vertexCount; ++i) {
                Math::Vec3 pos(vertices[i * 3], vertices[i * 3 + 1], vertices[i * 3 + 2]);
                positions.push_back(pos);
                center += pos;
            }
            center /= static_cast<float>(vertexCount);

            // Add center point as first interior point
            uint32_t centerIdx = static_cast<uint32_t>(positions.size());
            positions.push_back(center);

            // Generate tetrahedra from surface triangles + center
            std::vector<uint32_t> tetrahedra;
            tetrahedra.reserve(triangleCount * 4);

            for (uint32_t t = 0; t < triangleCount; ++t) {
                uint32_t i0 = indices[t * 3 + 0];
                uint32_t i1 = indices[t * 3 + 1];
                uint32_t i2 = indices[t * 3 + 2];

                if (i0 < vertexCount && i1 < vertexCount && i2 < vertexCount) {
                    tetrahedra.push_back(centerIdx);
                    tetrahedra.push_back(i0);
                    tetrahedra.push_back(i1);
                    tetrahedra.push_back(i2);
                }
            }

            softBody.Initialize(positions, tetrahedra);
            softBody.VolumeStiffness = 0.9f;
            softBody.Damping = 0.98f;
            softBody.SolverIterations = 8;
            softBody.EnableSelfCollision = true;

            return softBody;
        }
    };

} // namespace ECS
} // namespace Core
