#pragma once

// The cluster culling compute shader, shared by the main view and by shadow
// views.
//
// It lived inside GPUDrivenCuller.cpp until shadows needed the same test from a
// light's point of view. Copying it would have meant two shaders drifting apart
// on the exact frustum and cone maths, which is the kind of divergence that
// shows up as geometry present in the depth pass and missing from its shadow.
//
// The only difference between the two callers is what they bind and what they
// switch off: a shadow view has no hierarchical-Z to test against, so it passes
// `flags.x = 0` and the occlusion block never runs.

namespace Core {
namespace Renderer {

    inline constexpr const char* kClusterCullShaderSource = R"GLSL(
#version 450

layout(local_size_x = 64) in;

struct Cluster {
    vec4 centerRadius;
    vec4 coneAxisCutoff;
    uint firstIndex;
    uint indexCount;
    uint vertexOffset;
    uint pad;
};

struct Instance {
    mat4 transform;
    vec4 boundsCenterRadius;
    uint clusterBase;
    uint clusterCount;
    uint materialIndex;
    uint flags;
    uint vertexOffset;   // non-zero for a skinned instance's own arena slice
    uint pad0;
    uint pad1;
    uint pad2;
};

struct DrawCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};

layout(std430, binding = 0) readonly buffer Instances  { Instance instances[]; };
layout(std430, binding = 1) readonly buffer Clusters   { Cluster clusters[]; };
layout(std430, binding = 2) readonly buffer Offsets    { uint instanceOffsets[]; };
layout(std430, binding = 3) writeonly buffer Draws     { DrawCommand draws[]; };
layout(std430, binding = 4) buffer Retest              { uint retestFlags[]; };
layout(std430, binding = 5) buffer Counters            { uint counters[]; };
layout(binding = 6) uniform sampler2D uHZB;
layout(binding = 7) uniform CullParams {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 frustumPlanes[6];
    vec4 cameraPosition;
    vec4 hzbParams;   // x=width y=height z=mipCount w=phase (0 early, 1 late)
    vec4 flags;       // x=occlusion enabled y=cone enabled z=shadow view
    uvec4 counts;     // x=clusterSlots y=instanceCount
} pc;

// Locate the instance that owns a flat cluster slot. The CPU uploads a prefix
// sum of cluster counts rather than expanding every cluster every frame.
uint FindInstance(uint slot) {
    uint lo = 0u;
    uint hi = pc.counts.y;
    while (lo + 1u < hi) {
        uint mid = (lo + hi) >> 1u;
        if (instanceOffsets[mid] <= slot) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo;
}

void main() {
    uint slot = gl_GlobalInvocationID.x;
    if (slot >= pc.counts.x) {
        return;
    }

    uint instanceIndex = FindInstance(slot);
    Instance inst = instances[instanceIndex];
    uint localCluster = slot - instanceOffsets[instanceIndex];
    if (localCluster >= inst.clusterCount) {
        draws[slot].instanceCount = 0u;
        return;
    }

    Cluster cluster = clusters[inst.clusterBase + localCluster];

    draws[slot].indexCount = cluster.indexCount;
    draws[slot].firstIndex = cluster.firstIndex;
    // A skinned instance overrides the cluster's vertex base with its own posed
    // slice. Everything else about the draw is identical, which is the point:
    // skinned and static geometry share one indirect path.
    draws[slot].vertexOffset = int(inst.vertexOffset != 0u ? inst.vertexOffset
                                                          : cluster.vertexOffset);
    draws[slot].firstInstance = instanceIndex;

    bool shadowView = pc.flags.z > 0.5;
    // A light that does not cast shadows still has its instances in the list;
    // dropping them here keeps the caller from having to build a second scene.
    if (shadowView && (inst.flags & 1u) == 0u) {
        draws[slot].instanceCount = 0u;
        return;
    }

    bool latePhase = pc.hzbParams.w > 0.5;
    if (latePhase && retestFlags[slot] == 0u) {
        draws[slot].instanceCount = 0u;
        return;
    }

    vec3 worldCenter = (inst.transform * vec4(cluster.centerRadius.xyz, 1.0)).xyz;
    float scale = max(length(inst.transform[0].xyz),
                  max(length(inst.transform[1].xyz), length(inst.transform[2].xyz)));
    float radius = cluster.centerRadius.w * scale;

    if (!latePhase) {
        retestFlags[slot] = 0u;

        for (int i = 0; i < 6; ++i) {
            if (dot(pc.frustumPlanes[i].xyz, worldCenter) + pc.frustumPlanes[i].w < -radius) {
                draws[slot].instanceCount = 0u;
                atomicAdd(counters[2], 1u);
                return;
            }
        }

        // Backface cone: cull when the whole cluster faces away. Clusters whose
        // normals span more than a hemisphere carry a cutoff above 1 and never
        // trigger this. Skipped for shadow views, where a backfacing cluster
        // still occludes - dropping it punches holes in the shadow.
        if (!shadowView && pc.flags.y > 0.5 && cluster.coneAxisCutoff.w <= 1.0) {
            vec3 axis = normalize(mat3(inst.transform) * cluster.coneAxisCutoff.xyz);
            vec3 toCluster = worldCenter - pc.cameraPosition.xyz;
            float distance = length(toCluster);
            if (distance > radius && dot(toCluster / distance, axis) >= cluster.coneAxisCutoff.w) {
                draws[slot].instanceCount = 0u;
                atomicAdd(counters[3], 1u);
                return;
            }
        }
    }

    if (pc.flags.x > 0.5) {
        vec3 boundsMin = worldCenter - vec3(radius);
        vec3 boundsMax = worldCenter + vec3(radius);
        vec2 uvMin = vec2(1e9);
        vec2 uvMax = vec2(-1e9);
        float nearestDepth = 1e9;
        bool projectable = true;

        for (int corner = 0; corner < 8; ++corner) {
            vec3 point = vec3((corner & 1) != 0 ? boundsMax.x : boundsMin.x,
                              (corner & 2) != 0 ? boundsMax.y : boundsMin.y,
                              (corner & 4) != 0 ? boundsMax.z : boundsMin.z);
            vec4 clip = pc.viewProjection * vec4(point, 1.0);
            if (clip.w <= 1e-5) {
                projectable = false;   // straddles the near plane: assume visible
                break;
            }
            vec3 ndc = clip.xyz / clip.w;
            vec2 uv = ndc.xy * 0.5 + 0.5;
            uvMin = min(uvMin, uv);
            uvMax = max(uvMax, uv);
            nearestDepth = min(nearestDepth, ndc.z);
        }

        if (projectable) {
            uvMin = clamp(uvMin, vec2(0.0), vec2(1.0));
            uvMax = clamp(uvMax, vec2(0.0), vec2(1.0));
            vec2 sizePixels = (uvMax - uvMin) * pc.hzbParams.xy;
            float level = ceil(log2(max(max(sizePixels.x, sizePixels.y), 1.0)));
            level = clamp(level, 0.0, pc.hzbParams.z - 1.0);

            float farthest = max(
                max(textureLod(uHZB, vec2(uvMin.x, uvMin.y), level).r,
                    textureLod(uHZB, vec2(uvMax.x, uvMin.y), level).r),
                max(textureLod(uHZB, vec2(uvMin.x, uvMax.y), level).r,
                    textureLod(uHZB, vec2(uvMax.x, uvMax.y), level).r));

            if (nearestDepth > farthest + 1e-6) {
                draws[slot].instanceCount = 0u;
                if (!latePhase) {
                    retestFlags[slot] = 1u;   // phase 2 gets another look at it
                }
                atomicAdd(counters[4], 1u);
                return;
            }
        }
    }

    draws[slot].instanceCount = 1u;
    retestFlags[slot] = 0u;
    atomicAdd(counters[latePhase ? 1u : 0u], 1u);
}
)GLSL";

} // namespace Renderer
} // namespace Core
