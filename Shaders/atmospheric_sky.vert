#version 450

// Fullscreen quad vertex shader for atmospheric skybox
// Uses inverted depth (1.0 at near plane, 0.0 at far plane) for proper skybox rendering

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outViewRay;

layout(push_constant) uniform PushConstants {
    mat4 invViewProjection;
} pc;

void main() {
    // Generate fullscreen triangle vertices
    // Vertex 0: (-1, -1), Vertex 1: (3, -1), Vertex 2: (-1, 3)
    outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 pos = outUV * 2.0 - 1.0;

    // Output position with Z = 0.0 for inverted depth (skybox at far plane)
    // Using 0.0 ensures skybox renders behind all other geometry
    gl_Position = vec4(pos, 0.0, 1.0);

    // Calculate world-space view ray direction
    // Transform clip-space position to world space using inverse view-projection
    vec4 worldPos = pc.invViewProjection * vec4(pos.x, pos.y, 1.0, 1.0);
    outViewRay = worldPos.xyz / worldPos.w;
}
