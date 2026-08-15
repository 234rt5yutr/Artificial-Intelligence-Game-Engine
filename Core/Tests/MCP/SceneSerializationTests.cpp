// Vector parsing for the MCP scene tools.
//
// Every position, rotation, and scale an agent sends arrives through these, and
// the array form - the one the asset and render tools have always used and the
// one a caller writes by default - silently produced the origin. Entities
// spawned with a position all landed on top of each other, which is invisible
// until something depends on where things are: level-of-detail selection reads
// distance to camera, and every instance measured the same.

#include "Core/MCP/SceneSerialization.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

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

using namespace Core;
using Json = nlohmann::json;

bool NearlyEqual(const Math::Vec3& a, const Math::Vec3& b, float tolerance = 1e-4f) {
    return glm::length(a - b) < tolerance;
}

void TestVec3AcceptsBothForms() {
    CHECK(NearlyEqual(MCP::DeserializeVec3(Json::array({1.0, -2.0, 3.5})),
                      Math::Vec3(1.0f, -2.0f, 3.5f)));
    CHECK(NearlyEqual(MCP::DeserializeVec3(Json{{"x", 1.0}, {"y", -2.0}, {"z", 3.5}}),
                      Math::Vec3(1.0f, -2.0f, 3.5f)));
    // A bare number is uniform, which is what "scale": 2 has to mean.
    CHECK(NearlyEqual(MCP::DeserializeVec3(Json(2.0)), Math::Vec3(2.0f)));
    // Anything else keeps the caller's default rather than inventing a zero.
    CHECK(NearlyEqual(MCP::DeserializeVec3(Json("nonsense"), Math::Vec3(7.0f)), Math::Vec3(7.0f)));
    CHECK(NearlyEqual(MCP::DeserializeVec3(Json::array({1.0, 2.0}), Math::Vec3(7.0f)),
                      Math::Vec3(7.0f)));
}

void TestTransformRoundTripsAPosition() {
    Json transform;
    transform["position"] = Json::array({0.0, 0.0, -40.0});
    transform["scale"] = Json::array({2.0, 2.0, 2.0});

    const ECS::TransformComponent parsed = MCP::DeserializeTransform(transform);
    CHECK(NearlyEqual(parsed.Position, Math::Vec3(0.0f, 0.0f, -40.0f)));
    CHECK(NearlyEqual(parsed.Scale, Math::Vec3(2.0f)));
    // Without this the transform system never recomputes the world matrix, and
    // the entity stays at the origin however it was spawned.
    CHECK(parsed.IsDirty);
}

void TestEulerDegreesAcceptsBothForms() {
    const Math::Vec3 fromArray = MCP::DeserializeEulerDegrees(Json::array({90.0, 0.0, 0.0}));
    const Math::Vec3 fromObject =
        MCP::DeserializeEulerDegrees(Json{{"pitch", 90.0}, {"yaw", 0.0}, {"roll", 0.0}});
    CHECK(NearlyEqual(fromArray, fromObject));
    CHECK(std::fabs(fromArray.x - glm::radians(90.0f)) < 1e-4f);
}

} // namespace

int main() {
    TestVec3AcceptsBothForms();
    TestTransformRoundTripsAPosition();
    TestEulerDegreesAcceptsBothForms();

    std::printf("SceneSerializationTests: all checks passed\n");
    return 0;
}
