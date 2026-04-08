// RT Reflection Miss Shader - Sample environment when ray misses geometry
#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec4 hitValue;

layout(set = 0, binding = 7) uniform samplerCube environmentMap;

void main() {
    // Sample environment cubemap
    vec3 envColor = textureLod(environmentMap, gl_WorldRayDirectionEXT, 0.0).rgb;
    hitValue = vec4(envColor, 0.0);  // alpha 0 indicates sky hit
}
