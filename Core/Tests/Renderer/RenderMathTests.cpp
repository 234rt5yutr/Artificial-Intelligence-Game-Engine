// Checks the renderer maths that decides what gets drawn.
//
// The gap analysis flagged that nothing tested the renderer at all. Most of it
// needs a device, but the part that decides *visibility* - frustum planes,
// cascade splits, froxel slicing, backface cones - is pure arithmetic, and it is
// exactly the part whose failures are invisible until something is missing from
// a frame. That maths was previously buried inside classes that own Vulkan
// objects (and duplicated between two of them); it now lives in RenderMath.h so
// it can be checked here.

#include "Core/Log.h"
#include "Core/Renderer/RenderMath.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>

// ctest runs these with -C Release, where NDEBUG turns assert() into a no-op:
// the condition is not evaluated, so any call inside one silently disappears and
// nothing is actually verified. CHECK always evaluates and always reports.
#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n",                 \
                         #expr, __FILE__, __LINE__);                           \
            std::abort();                                                      \
        }                                                                      \
    } while (false)

namespace {

using namespace Core::Renderer;
using Core::Math::Mat4;
using Core::Math::Vec3;
using Core::Math::Vec4;

Mat4 TestProjection(float nearPlane, float farPlane) {
    Mat4 projection = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, nearPlane, farPlane);
    // Every projection in the engine carries this flip for Vulkan; the maths
    // under test has to survive it.
    projection[1][1] *= -1.0f;
    return projection;
}

void TestNearFarRoundTrip() {
    // The cascade fitter and the light grid both recover these from a matrix
    // rather than being handed them, so a wrong sign here silently reshapes
    // every cascade and every froxel.
    const float cases[][2] = {{0.1f, 1000.0f}, {0.05f, 300.0f}, {1.0f, 10000.0f}};
    for (const auto& pair : cases) {
        float nearPlane = 0.0f;
        float farPlane = 0.0f;
        CHECK(Core::Math::ExtractNearFar(TestProjection(pair[0], pair[1]), nearPlane, farPlane));
        CHECK(std::abs(nearPlane - pair[0]) < pair[0] * 0.01f + 1e-4f);
        CHECK(std::abs(farPlane - pair[1]) < pair[1] * 0.01f);
    }

    // An identity matrix is not a projection. It has to be rejected rather than
    // producing a plausible-looking near of 0 - a scene with no camera hands
    // exactly this in, and it is why the light grid once culled everything.
    float nearPlane = 0.0f;
    float farPlane = 0.0f;
    CHECK(!Core::Math::ExtractNearFar(Mat4(1.0f), nearPlane, farPlane));
}

void TestFrustumPlanesContainTheFrustum() {
    const Mat4 projection = TestProjection(0.1f, 100.0f);
    const Mat4 view = glm::lookAt(Vec3(0.0f, 0.0f, 5.0f), Vec3(0.0f), Vec3(0.0f, 1.0f, 0.0f));
    const Mat4 viewProjection = projection * view;

    Vec4 planes[6];
    ExtractFrustumPlanes(viewProjection, planes);

    // Normalised, so a plane test is a signed distance in world units.
    for (const Vec4& plane : planes) {
        CHECK(std::abs(glm::length(Vec3(plane)) - 1.0f) < 1e-3f);
    }

    // A point at the origin is in front of the camera and inside.
    CHECK(SphereInFrustum(planes, Vec3(0.0f), 0.1f));
    // Far behind the camera: outside.
    CHECK(!SphereInFrustum(planes, Vec3(0.0f, 0.0f, 50.0f), 0.1f));
    // Beyond the far plane: outside.
    CHECK(!SphereInFrustum(planes, Vec3(0.0f, 0.0f, -200.0f), 0.1f));
    // Far off to the side: outside.
    CHECK(!SphereInFrustum(planes, Vec3(500.0f, 0.0f, -10.0f), 0.1f));
    // Straddling: conservatively inside, which is what the cull shader does.
    CHECK(SphereInFrustum(planes, Vec3(0.0f, 0.0f, 50.0f), 60.0f));
}

void TestFrustumPlanesAgreeWithProjection() {
    // Cross-check against the projection itself: any point that survives the
    // plane test must also land inside clip space. This is the invariant that
    // catches a sign or column/row mix-up, which normalised planes alone do not.
    const Mat4 projection = TestProjection(0.5f, 60.0f);
    const Mat4 view = glm::lookAt(Vec3(3.0f, 2.0f, 8.0f), Vec3(0.0f), Vec3(0.0f, 1.0f, 0.0f));
    const Mat4 viewProjection = projection * view;

    Vec4 planes[6];
    ExtractFrustumPlanes(viewProjection, planes);

    uint32_t agreed = 0;
    uint32_t inside = 0;
    for (int x = -20; x <= 20; x += 4) {
        for (int y = -20; y <= 20; y += 4) {
            for (int z = -20; z <= 20; z += 4) {
                const Vec3 point(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                const bool planeSaysInside = SphereInFrustum(planes, point, 0.0f);

                const Vec4 clip = viewProjection * Vec4(point, 1.0f);
                const bool clipSaysInside =
                    clip.w > 0.0f &&
                    std::abs(clip.x) <= clip.w && std::abs(clip.y) <= clip.w &&
                    clip.z >= 0.0f && clip.z <= clip.w;

                if (planeSaysInside == clipSaysInside) {
                    ++agreed;
                }
                inside += clipSaysInside ? 1u : 0u;
            }
        }
    }
    // The sample grid must actually contain visible points, or the check above
    // would pass by agreeing that everything is outside.
    CHECK(inside > 0);
    CHECK(agreed == 11u * 11u * 11u);
}

void TestCascadeSplitsAreOrderedAndBounded() {
    const float nearPlane = 0.1f;
    const float farPlane = 150.0f;

    for (float lambda : {0.0f, 0.5f, 0.85f, 1.0f}) {
        float splits[4] = {};
        ComputeCascadeSplits(nearPlane, farPlane, 4, lambda, splits);

        // Strictly increasing, inside the range, and the last one reaches the
        // far plane exactly - a cascade that stops short leaves a band of the
        // world unshadowed.
        CHECK(splits[0] > nearPlane);
        for (int i = 1; i < 4; ++i) {
            CHECK(splits[i] > splits[i - 1]);
        }
        CHECK(std::abs(splits[3] - farPlane) < 1e-3f);
    }

    // Logarithmic splits put more resolution near the camera than uniform ones,
    // which is the entire point of the scheme.
    float uniform[4] = {};
    float logarithmic[4] = {};
    ComputeCascadeSplits(nearPlane, farPlane, 4, 0.0f, uniform);
    ComputeCascadeSplits(nearPlane, farPlane, 4, 1.0f, logarithmic);
    CHECK(logarithmic[0] < uniform[0]);
    CHECK(logarithmic[1] < uniform[1]);

    // One cascade covers everything.
    float single[1] = {};
    ComputeCascadeSplits(nearPlane, farPlane, 1, 0.85f, single);
    CHECK(std::abs(single[0] - farPlane) < 1e-3f);
}

void TestCascadeSplitsRejectBadRanges() {
    float splits[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    // far <= near is nonsense; it must degrade to "everything in the last
    // cascade" rather than producing NaNs that propagate into every matrix.
    ComputeCascadeSplits(50.0f, 10.0f, 4, 0.85f, splits);
    for (float split : splits) {
        CHECK(std::isfinite(split));
    }
    ComputeCascadeSplits(0.0f, 100.0f, 4, 0.85f, splits);
    for (float split : splits) {
        CHECK(std::isfinite(split));
    }
    // A null target must not crash.
    ComputeCascadeSplits(0.1f, 100.0f, 4, 0.85f, nullptr);
    ComputeCascadeSplits(0.1f, 100.0f, 0, 0.85f, splits);
}

void TestZSlicesCoverTheDepthRange() {
    const float nearPlane = 0.1f;
    const float farPlane = 300.0f;
    const uint32_t slices = 32;

    float scale = 0.0f;
    float bias = 0.0f;
    ComputeZSliceParams(nearPlane, farPlane, slices, scale, bias);

    // The near plane lands in slice 0 and the far plane in the last one; if
    // either fell outside, lights at that depth would be culled from every
    // froxel.
    CHECK(ComputeZSlice(nearPlane, scale, bias, slices) == 0);
    CHECK(ComputeZSlice(farPlane, scale, bias, slices) == slices - 1);

    // Monotonic across the range.
    uint32_t previous = 0;
    for (float depth = nearPlane; depth < farPlane; depth *= 1.2f) {
        const uint32_t slice = ComputeZSlice(depth, scale, bias, slices);
        CHECK(slice >= previous);
        CHECK(slice < slices);
        previous = slice;
    }

    // Out-of-range depths clamp rather than indexing past the grid.
    CHECK(ComputeZSlice(-5.0f, scale, bias, slices) < slices);
    CHECK(ComputeZSlice(1e9f, scale, bias, slices) == slices - 1);
    CHECK(ComputeZSlice(10.0f, scale, bias, 0) == 0);
}

void TestBackfaceConeCulling() {
    // A cluster whose normals all point +X, sitting to the right of the camera,
    // is facing away and can be dropped.
    const Vec3 axis(1.0f, 0.0f, 0.0f);
    const float cutoff = 0.5f;
    CHECK(ClusterIsBackfacing(axis, cutoff, Vec3(10.0f, 0.0f, 0.0f), 1.0f, Vec3(0.0f)));

    // The same cluster seen from the other side is facing the camera.
    CHECK(!ClusterIsBackfacing(axis, cutoff, Vec3(10.0f, 0.0f, 0.0f), 1.0f, Vec3(20.0f, 0.0f, 0.0f)));

    // A cutoff above 1 marks a cluster whose normals span more than a
    // hemisphere; it must never be culled, whatever the angle.
    CHECK(!ClusterIsBackfacing(axis, 2.0f, Vec3(10.0f, 0.0f, 0.0f), 1.0f, Vec3(0.0f)));

    // The camera inside the cluster's bounds: no single direction describes it,
    // so it must stay visible.
    CHECK(!ClusterIsBackfacing(axis, cutoff, Vec3(0.5f, 0.0f, 0.0f), 5.0f, Vec3(0.0f)));

    // Perpendicular: silhouette, must not be culled.
    CHECK(!ClusterIsBackfacing(axis, cutoff, Vec3(0.0f, 0.0f, 10.0f), 1.0f, Vec3(0.0f)));
}

void TestEnvironmentBRDFApproximation() {
    const Vec3 goldF0(1.00f, 0.78f, 0.34f);
    const Vec3 dielectricF0(0.04f, 0.04f, 0.04f);
    const Vec3 sky(0.2f, 0.5f, 0.9f);
    const Vec3 ground(0.1f, 0.08f, 0.05f);

    // 1. EnvBRDFApprox at normal incidence (NdotV = 1) for a mirror (roughness = 0).
    // Result should closely preserve F0.
    const Vec3 mirrorGold = EnvBRDFApprox(goldF0, 0.0f, 1.0f);
    CHECK(std::abs(mirrorGold.r - goldF0.r) < 0.05f);
    CHECK(std::abs(mirrorGold.g - goldF0.g) < 0.05f);
    CHECK(std::abs(mirrorGold.b - goldF0.b) < 0.05f);

    // 2. Glancing angle Fresnel boost (NdotV ~ 0). Dielectrics should approach 1.0 specular reflection.
    const Vec3 glancingDielectric = EnvBRDFApprox(dielectricF0, 0.0f, 0.01f);
    CHECK(glancingDielectric.r > dielectricF0.r);
    CHECK(glancingDielectric.r > 0.8f);

    // 3. Environment radiance transitions smoothly from ground to sky.
    const Vec3 radUp = EnvironmentRadiance(Vec3(0.0f, 1.0f, 0.0f), sky, ground);
    const Vec3 radDown = EnvironmentRadiance(Vec3(0.0f, -1.0f, 0.0f), sky, ground);
    const Vec3 radHorizon = EnvironmentRadiance(Vec3(1.0f, 0.0f, 0.0f), sky, ground);

    CHECK(glm::distance(radUp, sky) < 1e-4f);
    CHECK(glm::distance(radDown, ground) < 1e-4f);
    CHECK(radHorizon.r >= ground.r && radHorizon.r <= sky.r);

    // 4. Full specular computation.
    const Vec3 spec = EnvironmentSpecular(Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f),
                                          goldF0, 0.1f, sky, ground);
    CHECK(spec.r > 0.0f && spec.g > 0.0f && spec.b > 0.0f);
}

} // namespace

int main() {
    Engine::Log::Init();

    TestNearFarRoundTrip();
    TestFrustumPlanesContainTheFrustum();
    TestFrustumPlanesAgreeWithProjection();
    TestCascadeSplitsAreOrderedAndBounded();
    TestCascadeSplitsRejectBadRanges();
    TestZSlicesCoverTheDepthRange();
    TestBackfaceConeCulling();
    TestEnvironmentBRDFApproximation();

    std::printf("RenderMathTests: all checks passed\n");
    return 0;
}
