#pragma once

/**
 * @file VoronoiFracturer.h
 * @brief Voronoi-based mesh fracturing utility for physics destruction systems.
 * 
 * This module provides functionality to fracture 3D meshes into smaller pieces
 * using Voronoi tessellation. The algorithm distributes seed points within a
 * mesh's bounding volume and partitions the mesh into convex cells based on
 * proximity to each seed point.
 * 
 * Common use cases:
 * - Destructible environments (walls, floors, objects)
 * - Impact-based fragmentation with radial seed patterns
 * - Pre-computed fracture pieces for real-time destruction
 * 
 * @note All fracturing operations are performed on the CPU. For real-time
 *       destruction, consider pre-computing fracture patterns at load time.
 * 
 * @see Core::Physics::RigidBody for physics simulation of fractured pieces
 */

#include "Core/Math/Math.h"

#include <cfloat>
#include <cstdint>
#include <string>
#include <vector>

namespace Core {
namespace Physics {

/**
 * @brief Represents a single cell (fragment) produced by Voronoi fracturing.
 * 
 * Each cell is a convex polyhedron defined by the intersection of half-spaces
 * formed by the perpendicular bisector planes between adjacent seed points.
 * The mesh data is triangulated and ready for rendering or physics simulation.
 */
struct VoronoiCell
{
    /** @brief Vertex positions defining the cell geometry. */
    std::vector<Math::Vec3> Vertices;
    
    /** @brief Triangle indices (triplets) for rendering the cell. */
    std::vector<uint32_t> Indices;
    
    /** @brief Geometric center of mass (assuming uniform density). */
    Math::Vec3 Centroid{ 0.0f, 0.0f, 0.0f };
    
    /** @brief Volume of the cell in cubic units. */
    float Volume{ 0.0f };
    
    /** @brief Minimum corner of the axis-aligned bounding box. */
    Math::Vec3 BoundsMin{ 0.0f, 0.0f, 0.0f };
    
    /** @brief Maximum corner of the axis-aligned bounding box. */
    Math::Vec3 BoundsMax{ 0.0f, 0.0f, 0.0f };
};

/**
 * @brief Defines how seed points are distributed within the fracture volume.
 * 
 * The seed pattern significantly affects the visual and physical properties
 * of the resulting fragments. Choose patterns based on the desired effect:
 * 
 * - Uniform: Even distribution, natural-looking general fractures
 * - Clustered: Dense groups, simulates material weaknesses
 * - Radial: Emanates from impact, realistic impact damage
 * - Structural: Follows geometry, architectural destruction
 * - Random: Pure randomness, chaotic fractures
 * - Custom: User-defined seed positions for artistic control
 */
enum class FractureSeedPattern : uint8_t
{
    /** @brief Evenly distributed seeds using 3D grid or Poisson disk sampling. */
    Uniform,
    
    /** @brief Seeds clustered around specific regions or points of interest. */
    Clustered,
    
    /** @brief Seeds radiate outward from the impact point with distance falloff. */
    Radial,
    
    /** @brief Seeds placed along structural features (edges, corners, thin sections). */
    Structural,
    
    /** @brief Purely random seed placement within the bounding volume. */
    Random,
    
    /** @brief User-provided seed positions via CustomSeeds vector. */
    Custom
};

/**
 * @brief Configuration parameters for the Voronoi fracturing algorithm.
 * 
 * These settings control the number, distribution, and constraints of the
 * resulting fracture pieces. Adjust based on performance requirements and
 * visual quality needs.
 * 
 * @code
 * FractureSettings settings;
 * settings.SeedCount = 16;
 * settings.Pattern = FractureSeedPattern::Radial;
 * settings.ImpactPoint = hitPosition;
 * settings.ImpactRadius = 2.5f;
 * settings.MinPieceVolume = 0.01f;  // Cull tiny fragments
 * @endcode
 */
struct FractureSettings
{
    /** 
     * @brief Number of seed points to generate (affects piece count).
     * @note Higher values produce more pieces but increase computation time.
     */
    uint32_t SeedCount{ 8 };
    
    /** @brief Distribution pattern for seed point generation. */
    FractureSeedPattern Pattern{ FractureSeedPattern::Uniform };
    
    /**
     * @brief World-space position of the impact or fracture origin.
     * @note Primarily used with Radial and Clustered patterns.
     */
    Math::Vec3 ImpactPoint{ 0.0f, 0.0f, 0.0f };
    
    /**
     * @brief Radius of influence for impact-based patterns.
     * @note Seeds are concentrated within this radius from ImpactPoint.
     */
    float ImpactRadius{ 1.0f };
    
    /**
     * @brief Minimum volume threshold for generated pieces.
     * @note Pieces smaller than this are merged or discarded.
     */
    float MinPieceVolume{ 0.001f };
    
    /**
     * @brief Maximum volume allowed for a single piece.
     * @note Pieces exceeding this may be subdivided further.
     */
    float MaxPieceVolume{ FLT_MAX };
    
    /**
     * @brief Whether to generate interior faces at fracture boundaries.
     * @note Set to false for open/hollow objects or performance savings.
     */
    bool GenerateInteriorFaces{ true };
    
    /**
     * @brief UV coordinate scaling for interior (fractured) surfaces.
     * @note Used for tiling interior material textures appropriately.
     */
    Math::Vec3 InteriorUVScale{ 1.0f, 1.0f, 1.0f };
    
    /**
     * @brief User-defined seed positions when Pattern is Custom.
     * @note Overrides SeedCount when Pattern == FractureSeedPattern::Custom.
     */
    std::vector<Math::Vec3> CustomSeeds;
};

/**
 * @brief Result structure returned by the Voronoi fracturing operation.
 * 
 * Contains all generated cells (fragments) along with diagnostic information.
 * Always check the Success flag before using the cell data.
 */
struct FractureResult
{
    /** @brief Collection of convex cells representing the fractured mesh. */
    std::vector<VoronoiCell> Cells;
    
    /** @brief Actual seed points used for the Voronoi tessellation. */
    std::vector<Math::Vec3> SeedPoints;
    
    /** @brief Indicates whether the fracture operation completed successfully. */
    bool Success{ false };
    
    /** @brief Human-readable error description if Success is false. */
    std::string ErrorMessage;
};

/**
 * @brief Geometric plane representation for mesh clipping operations.
 * 
 * Defined by the plane equation: Normal · P + Distance = 0
 * Points where Normal · P + Distance > 0 are considered "in front" of the plane.
 */
struct Plane
{
    /** @brief Unit normal vector perpendicular to the plane surface. */
    Math::Vec3 Normal{ 0.0f, 1.0f, 0.0f };
    
    /** @brief Signed distance from the origin along the normal direction. */
    float Distance{ 0.0f };
    
    /** @brief Constructs a plane from normal and distance. */
    Plane() = default;
    
    /** @brief Constructs a plane from normal and distance. */
    Plane(const Math::Vec3& normal, float distance)
        : Normal(normal), Distance(distance) {}
    
    /** @brief Constructs a plane from normal and a point on the plane. */
    Plane(const Math::Vec3& normal, const Math::Vec3& point)
        : Normal(normal), Distance(-glm::dot(normal, point)) {}
};

/**
 * @brief Static utility class for Voronoi-based mesh fracturing.
 * 
 * Provides algorithms to partition a 3D mesh into convex fragments using
 * Voronoi tessellation. The class is designed as a stateless utility with
 * all methods being static, enabling thread-safe concurrent fracturing of
 * multiple meshes.
 * 
 * ## Algorithm Overview
 * 
 * 1. **Seed Generation**: Distribute seed points within the mesh bounds
 *    according to the specified pattern.
 * 
 * 2. **Voronoi Tessellation**: For each seed point, compute the region of
 *    space closer to that seed than any other (the Voronoi cell).
 * 
 * 3. **Mesh Clipping**: Clip the original mesh against each Voronoi cell
 *    to produce the final fragments.
 * 
 * 4. **Post-processing**: Generate interior faces, compute volumes, and
 *    validate piece constraints.
 * 
 * ## Usage Example
 * 
 * @code
 * // Prepare mesh data
 * std::vector<Math::Vec3> vertices = GetMeshVertices();
 * std::vector<uint32_t> indices = GetMeshIndices();
 * 
 * // Configure fracture settings
 * FractureSettings settings;
 * settings.SeedCount = 12;
 * settings.Pattern = FractureSeedPattern::Radial;
 * settings.ImpactPoint = impactLocation;
 * settings.ImpactRadius = 1.5f;
 * 
 * // Perform fracture
 * FractureResult result = VoronoiFracturer::Fracture(vertices, indices, settings);
 * 
 * if (result.Success) {
 *     for (const auto& cell : result.Cells) {
 *         CreatePhysicsFragment(cell);
 *     }
 * }
 * @endcode
 * 
 * ## Performance Considerations
 * 
 * - Time complexity is approximately O(n * k * log(k)) where n is vertex count
 *   and k is seed count.
 * - Memory usage scales with both input mesh size and number of output cells.
 * - Consider pre-computing fractures for static destructibles.
 * 
 * @note This class cannot be instantiated. All methods are static.
 */
class VoronoiFracturer
{
public:
    // =========================================================================
    // Deleted Constructors (Static-Only Class)
    // =========================================================================
    
    VoronoiFracturer() = delete;
    VoronoiFracturer(const VoronoiFracturer&) = delete;
    VoronoiFracturer& operator=(const VoronoiFracturer&) = delete;
    VoronoiFracturer(VoronoiFracturer&&) = delete;
    VoronoiFracturer& operator=(VoronoiFracturer&&) = delete;
    
    // =========================================================================
    // Primary Fracturing Interface
    // =========================================================================
    
    /**
     * @brief Fractures a mesh into multiple convex pieces using Voronoi tessellation.
     * 
     * This is the main entry point for mesh fracturing. The input mesh is
     * partitioned into convex cells based on the seed point distribution
     * defined by the settings.
     * 
     * @param vertices      Input mesh vertex positions.
     * @param indices       Input mesh triangle indices (triplets).
     * @param settings      Configuration parameters for the fracture operation.
     * @return FractureResult containing all generated cells and status information.
     * 
     * @pre vertices.size() >= 4 (minimum for a tetrahedron)
     * @pre indices.size() >= 12 && indices.size() % 3 == 0
     * @pre settings.SeedCount >= 2 || !settings.CustomSeeds.empty()
     * 
     * @post result.Success == true if fracturing completed without errors
     * @post result.Cells.size() <= settings.SeedCount (some cells may be empty)
     * 
     * @note The input mesh should be closed (watertight) for best results.
     * @note Non-manifold geometry may produce unexpected results.
     */
    [[nodiscard]] static FractureResult Fracture(
        const std::vector<Math::Vec3>& vertices,
        const std::vector<uint32_t>& indices,
        const FractureSettings& settings);
    
    // =========================================================================
    // Seed Point Generation
    // =========================================================================
    
    /**
     * @brief Generates seed points within the specified bounding box.
     * 
     * Creates a set of 3D points that will serve as the centers of Voronoi
     * cells. The distribution pattern affects the shape and size of the
     * resulting fragments.
     * 
     * @param boundsMin     Minimum corner of the bounding volume.
     * @param boundsMax     Maximum corner of the bounding volume.
     * @param settings      Configuration including seed count and pattern.
     * @return Vector of seed point positions in world space.
     * 
     * @note For Custom pattern, returns settings.CustomSeeds directly.
     * @note Radial pattern requires valid ImpactPoint and ImpactRadius.
     */
    [[nodiscard]] static std::vector<Math::Vec3> GenerateSeeds(
        const Math::Vec3& boundsMin,
        const Math::Vec3& boundsMax,
        const FractureSettings& settings);
    
    // =========================================================================
    // Mesh Clipping Operations
    // =========================================================================
    
    /**
     * @brief Clips a mesh by a plane, producing front and back portions.
     * 
     * Splits the input mesh into two separate meshes along the specified
     * plane. Triangles crossing the plane are split, and new edges are
     * created along the intersection.
     * 
     * @param vertices          Input mesh vertex positions.
     * @param indices           Input mesh triangle indices.
     * @param plane             Clipping plane definition.
     * @param outFrontVertices  [out] Vertices on the positive side of the plane.
     * @param outFrontIndices   [out] Triangle indices for the front mesh.
     * @param outBackVertices   [out] Vertices on the negative side of the plane.
     * @param outBackIndices    [out] Triangle indices for the back mesh.
     * 
     * @note Vertices exactly on the plane are included in both outputs.
     * @note Interior faces at the clip boundary are not generated by this method;
     *       use GenerateCapFace() separately if needed.
     */
    static void ClipMeshByPlane(
        const std::vector<Math::Vec3>& vertices,
        const std::vector<uint32_t>& indices,
        const Plane& plane,
        std::vector<Math::Vec3>& outFrontVertices,
        std::vector<uint32_t>& outFrontIndices,
        std::vector<Math::Vec3>& outBackVertices,
        std::vector<uint32_t>& outBackIndices);
    
    // =========================================================================
    // Geometry Utilities
    // =========================================================================
    
    /**
     * @brief Computes the convex hull of a point cloud and returns it as a cell.
     * 
     * Constructs the minimal convex polyhedron enclosing all input points.
     * The resulting cell is triangulated and includes computed bounds and volume.
     * 
     * @param points    Set of 3D points to enclose.
     * @return VoronoiCell representing the convex hull geometry.
     * 
     * @pre points.size() >= 4 (minimum for non-degenerate 3D hull)
     * @note Uses the Quickhull algorithm internally for O(n log n) performance.
     */
    [[nodiscard]] static VoronoiCell ComputeConvexHull(
        const std::vector<Math::Vec3>& points);
    
    /**
     * @brief Calculates the volume of a Voronoi cell.
     * 
     * Computes the signed volume using the divergence theorem, summing
     * tetrahedral volumes formed by triangles and the origin.
     * 
     * @param cell      The cell whose volume to calculate.
     * @return Absolute volume in cubic units.
     * 
     * @note Assumes the cell mesh is closed and consistently wound.
     * @note Returns 0.0f for degenerate or empty cells.
     */
    [[nodiscard]] static float CalculateCellVolume(const VoronoiCell& cell);
    
    /**
     * @brief Triangulates a planar polygon defined by ordered vertices.
     * 
     * Converts a convex or simple concave polygon into a set of triangles.
     * Uses ear-clipping for concave polygons and fan triangulation for convex.
     * 
     * @param vertices  Ordered polygon vertices (counter-clockwise winding).
     * @return Vector of triangle indices referencing the input vertex array.
     * 
     * @pre vertices.size() >= 3
     * @note For convex polygons, produces (n-2) triangles for n vertices.
     * @note Self-intersecting polygons may produce incorrect results.
     */
    [[nodiscard]] static std::vector<uint32_t> TriangulateFace(
        const std::vector<Math::Vec3>& vertices);
    
private:
    // =========================================================================
    // Internal Implementation Details
    // =========================================================================
    
    /**
     * @brief Computes the perpendicular bisector plane between two points.
     * @param p1    First point.
     * @param p2    Second point.
     * @return Plane equidistant from both points, normal pointing toward p2.
     */
    [[nodiscard]] static Plane ComputeBisectorPlane(
        const Math::Vec3& p1,
        const Math::Vec3& p2);
    
    /**
     * @brief Generates uniformly distributed seed points using 3D grid sampling.
     */
    [[nodiscard]] static std::vector<Math::Vec3> GenerateUniformSeeds(
        const Math::Vec3& boundsMin,
        const Math::Vec3& boundsMax,
        uint32_t count);
    
    /**
     * @brief Generates clustered seed points around the impact location.
     */
    [[nodiscard]] static std::vector<Math::Vec3> GenerateClusteredSeeds(
        const Math::Vec3& boundsMin,
        const Math::Vec3& boundsMax,
        const Math::Vec3& center,
        float radius,
        uint32_t count);
    
    /**
     * @brief Generates radially distributed seeds emanating from impact point.
     */
    [[nodiscard]] static std::vector<Math::Vec3> GenerateRadialSeeds(
        const Math::Vec3& boundsMin,
        const Math::Vec3& boundsMax,
        const Math::Vec3& center,
        float radius,
        uint32_t count);
    
    /**
     * @brief Generates random seed points within the bounding volume.
     */
    [[nodiscard]] static std::vector<Math::Vec3> GenerateRandomSeeds(
        const Math::Vec3& boundsMin,
        const Math::Vec3& boundsMax,
        uint32_t count);
    
    /**
     * @brief Computes AABB bounds for a set of vertices.
     */
    static void ComputeBounds(
        const std::vector<Math::Vec3>& vertices,
        Math::Vec3& outMin,
        Math::Vec3& outMax);
    
    /**
     * @brief Calculates the centroid (center of mass) of a cell.
     */
    [[nodiscard]] static Math::Vec3 ComputeCentroid(
        const std::vector<Math::Vec3>& vertices);
    
    /**
     * @brief Clips a cell against all bisector planes from other seeds.
     */
    [[nodiscard]] static VoronoiCell ClipCellBySeedPlanes(
        const VoronoiCell& initialCell,
        const Math::Vec3& cellSeed,
        const std::vector<Math::Vec3>& allSeeds,
        size_t seedIndex);
    
    /**
     * @brief Generates a cap face for a clipped mesh boundary.
     */
    static void GenerateCapFace(
        const std::vector<Math::Vec3>& boundaryLoop,
        const Math::Vec3& normal,
        std::vector<Math::Vec3>& outVertices,
        std::vector<uint32_t>& outIndices);
    
    /**
     * @brief Validates and filters cells based on volume constraints.
     */
    static void FilterCellsByVolume(
        std::vector<VoronoiCell>& cells,
        float minVolume,
        float maxVolume);
};

} // namespace Physics
} // namespace Core
