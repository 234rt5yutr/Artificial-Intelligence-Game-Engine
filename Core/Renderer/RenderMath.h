#pragma once

// Pure renderer maths.
//
// These live here for two reasons. Frustum plane extraction was written twice -
// once in the GPU-driven culler and once in the shadow renderer - and two copies
// of a sign convention is exactly the kind of thing that drifts and then shows
// up as geometry present in one pass and missing from another.
//
// The second reason is that everything below is a function of matrices and
// numbers, with no device in it. Kept inside the classes that own Vulkan
// objects, none of it could be tested; here, all of it can.

#include "Core/Math/Math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Core {
namespace Renderer {

    // Gribb-Hartmann extraction, normalised so a sphere test is a plain signed
    // distance. Order is left, right, bottom, top, near, far.
    inline void ExtractFrustumPlanes(const Math::Mat4& viewProjection, Math::Vec4 outPlanes[6]) {
        const Math::Mat4& m = viewProjection;
        // glm is column-major: m[column][row].
        for (int i = 0; i < 3; ++i) {
            outPlanes[i * 2 + 0] = Math::Vec4(m[0][3] + m[0][i], m[1][3] + m[1][i],
                                              m[2][3] + m[2][i], m[3][3] + m[3][i]);
            outPlanes[i * 2 + 1] = Math::Vec4(m[0][3] - m[0][i], m[1][3] - m[1][i],
                                              m[2][3] - m[2][i], m[3][3] - m[3][i]);
        }
        for (int i = 0; i < 6; ++i) {
            const float length = glm::length(Math::Vec3(outPlanes[i]));
            if (length > 1e-6f) {
                outPlanes[i] /= length;
            }
        }
    }

    // Conservative: a sphere straddling a plane counts as inside. Matches what
    // the cull shader does, so CPU and GPU agree on borderline cases.
    inline bool SphereInFrustum(const Math::Vec4 planes[6], const Math::Vec3& center, float radius) {
        for (int i = 0; i < 6; ++i) {
            if (glm::dot(Math::Vec3(planes[i]), center) + planes[i].w < -radius) {
                return false;
            }
        }
        return true;
    }

    // Practical split scheme: blends a uniform and a logarithmic distribution.
    // Uniform alone wastes the near cascades; logarithmic alone collapses the far
    // ones. `lambda` is 0 for uniform, 1 for fully logarithmic.
    //
    // Writes `count` far distances in view space; the last is always `farPlane`.
    inline void ComputeCascadeSplits(float nearPlane, float farPlane, uint32_t count,
                                     float lambda, float* outSplits) {
        if (count == 0 || outSplits == nullptr) {
            return;
        }
        if (!(farPlane > nearPlane) || !(nearPlane > 0.0f)) {
            for (uint32_t i = 0; i < count; ++i) {
                outSplits[i] = farPlane;
            }
            return;
        }

        const float range = farPlane - nearPlane;
        const float ratio = farPlane / nearPlane;
        for (uint32_t i = 0; i < count; ++i) {
            const float fraction = static_cast<float>(i + 1) / static_cast<float>(count);
            const float logSplit = nearPlane * std::pow(ratio, fraction);
            const float uniformSplit = nearPlane + range * fraction;
            outSplits[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
        }
        outSplits[count - 1] = farPlane;
    }

    // Exponential froxel slicing: slice = log(viewDepth) * scale + bias. Two
    // multiply-adds in the shader, and the slices track perspective rather than
    // screen depth.
    inline void ComputeZSliceParams(float nearPlane, float farPlane, uint32_t sliceCount,
                                    float& outScale, float& outBias) {
        const float logRatio = std::log(std::max(farPlane, 1e-4f) / std::max(nearPlane, 1e-4f));
        outScale = logRatio > 1e-6f ? static_cast<float>(sliceCount) / logRatio : 1.0f;
        outBias = -(static_cast<float>(sliceCount) * std::log(std::max(nearPlane, 1e-4f))) /
                  std::max(logRatio, 1e-6f);
    }

    inline uint32_t ComputeZSlice(float viewDepth, float scale, float bias, uint32_t sliceCount) {
        if (sliceCount == 0) {
            return 0;
        }
        const float slice = std::log(std::max(viewDepth, 1e-4f)) * scale + bias;
        return static_cast<uint32_t>(std::clamp(slice, 0.0f,
                                                static_cast<float>(sliceCount) - 1.0f));
    }

    // meshoptimizer's backface cone test. A cluster is entirely backfacing when
    // the view direction agrees with its average normal past the cutoff. Clusters
    // whose normals span more than a hemisphere carry a cutoff above 1 and can
    // never trigger this.
    inline bool ClusterIsBackfacing(const Math::Vec3& coneAxis, float coneCutoff,
                                    const Math::Vec3& clusterCenter, float clusterRadius,
                                    const Math::Vec3& cameraPosition) {
        if (coneCutoff > 1.0f) {
            return false;
        }
        const Math::Vec3 toCluster = clusterCenter - cameraPosition;
        const float distance = glm::length(toCluster);
        if (distance <= clusterRadius) {
            return false;   // camera is inside the cluster's bounds
        }
        return glm::dot(toCluster / distance, coneAxis) >= coneCutoff;
    }

} // namespace Renderer
} // namespace Core
