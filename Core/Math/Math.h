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

} // namespace Math
} // namespace Core
