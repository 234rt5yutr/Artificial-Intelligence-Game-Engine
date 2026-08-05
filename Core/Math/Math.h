#pragma once

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
// Vulkan clips depth to [0, 1]; GLM defaults to OpenGL's [-1, 1]. Without this
// every glm::perspective in the engine produced clip-space z that put the near
// half of the frustum behind z = 0, where Vulkan clips it away - and the HZB,
// occlusion, and GI reprojection all read that z back assuming [0, 1].
// Also set as a compile definition in CMakeLists.txt so translation units that
// include glm without going through this header agree.
#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cmath>

namespace Core {
namespace Math {

    // Vectors
    using Vec2 = glm::vec2;
    using Vec3 = glm::vec3;
    using Vec4 = glm::vec4;

    using IVec2 = glm::ivec2;
    using IVec3 = glm::ivec3;
    using IVec4 = glm::ivec4;

    using UVec2 = glm::uvec2;
    using UVec3 = glm::uvec3;
    using UVec4 = glm::uvec4;

    // Matrices
    using Mat3 = glm::mat3;
    using Mat4 = glm::mat4;

    // Quaternion
    using Quat = glm::quat;

    // Recovers the near and far planes from a perspective projection built for
    // Vulkan's [0, 1] depth range. Sub-pixel jitter only touches [2][0] and
    // [2][1], so it does not disturb this.
    //
    // Both the shadow cascades and the clustered light grid need the camera's
    // depth range, and neither is handed it - they get a projection matrix.
    inline bool ExtractNearFar(const Mat4& projection, float& nearPlane, float& farPlane) {
        const float p22 = projection[2][2];
        const float p32 = projection[3][2];
        if (std::abs(p22) < 1e-6f || std::abs(p22 + 1.0f) < 1e-6f) {
            return false;
        }
        const float recoveredNear = p32 / p22;
        const float recoveredFar = p32 / (p22 + 1.0f);
        if (!(recoveredNear > 0.0f) || !(recoveredFar > recoveredNear)) {
            return false;
        }
        nearPlane = recoveredNear;
        farPlane = recoveredFar;
        return true;
    }

} // namespace Math
} // namespace Core
