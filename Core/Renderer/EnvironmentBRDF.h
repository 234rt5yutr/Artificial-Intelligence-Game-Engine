#pragma once

// The environment specular term, as GLSL shared by every pass that needs it.
//
// Two passes have to agree on this exactly. The geometry pass adds it, because a
// metal with no reflection is black - ambient was applied as pure diffuse, which
// a metal does not have, so metals came out darker than the dielectrics around
// them and never showed their surroundings at all. The reflection pass then
// *replaces* it wherever a ray actually hit, and it can only subtract what it can
// reproduce. Two copies of this formula would drift and leave a visible seam at
// the edge of every reflection.

namespace Core {
namespace Renderer {

    inline const char* kEnvironmentSpecularGLSL = R"GLSL(
// A two-colour environment: sky above, ground below, blended across the horizon.
// It is not a captured cubemap, but it is the difference between a metal that
// reflects its surroundings and one that reflects nothing.
vec3 EnvironmentRadiance(vec3 direction, vec3 skyColor, vec3 groundColor) {
    float up = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    // Squared, so the horizon band is tight rather than a smooth wash over the
    // whole sphere.
    return mix(groundColor, skyColor, up * up);
}

// With a baked probe the environment is the room rather than a gradient.
// Roughness picks a mip: a mirror reads the sharp top level, a rough surface a
// blurred one. `probeParams.x` is zero until a bake completes, and the analytic
// sky covers that gap rather than a black reflection.
vec3 EnvironmentRadianceProbe(samplerCube probe, vec4 probeParams, vec3 direction,
                              float roughness, vec3 skyColor, vec3 groundColor) {
    if (probeParams.x < 0.5) {
        return EnvironmentRadiance(direction, skyColor, groundColor);
    }
    float mip = clamp(roughness, 0.0, 1.0) * max(probeParams.y - 1.0, 0.0);
    return textureLod(probe, direction, mip).rgb;
}

// Karis' analytic fit to the split-sum environment BRDF. The real thing is a
// precomputed lookup table; this is two lines and within a few percent, which is
// well inside the error of approximating the environment with two colours.
vec3 EnvBRDFApprox(vec3 f0, float roughness, float ndotv) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * ndotv)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}

// Ambient specular for a surface: what the environment contributes through the
// mirror lobe, before any traced reflection replaces it.
vec3 EnvironmentSpecular(vec3 normal, vec3 viewDir, vec3 f0, float roughness,
                         vec3 skyColor, vec3 groundColor) {
    vec3 reflection = reflect(-viewDir, normal);
    float ndotv = clamp(dot(normal, viewDir), 0.0, 1.0);
    return EnvironmentRadiance(reflection, skyColor, groundColor) *
           EnvBRDFApprox(f0, roughness, ndotv);
}

vec3 EnvironmentSpecularProbe(samplerCube probe, vec4 probeParams, vec3 normal, vec3 viewDir,
                              vec3 f0, float roughness, vec3 skyColor, vec3 groundColor) {
    vec3 reflection = reflect(-viewDir, normal);
    float ndotv = clamp(dot(normal, viewDir), 0.0, 1.0);
    return EnvironmentRadianceProbe(probe, probeParams, reflection, roughness,
                                    skyColor, groundColor) *
           EnvBRDFApprox(f0, roughness, ndotv);
}
)GLSL";

} // namespace Renderer
} // namespace Core
