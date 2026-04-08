// RT Shadow Miss Shader - Called when ray doesn't hit anything (not in shadow)
#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT float shadowVisibility;

void main() {
    // Ray missed all geometry - surface is lit
    shadowVisibility = 1.0;
}
