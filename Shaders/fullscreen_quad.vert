#version 450

layout(location = 0) out vec2 fragUV;

void main() {
    // Generate fullscreen triangle vertices using vertex index
    // This creates a single triangle that covers the entire screen
    // Vertex 0: (-1, -1), UV (0, 0)
    // Vertex 1: (3, -1),  UV (2, 0)
    // Vertex 2: (-1, 3),  UV (0, 2)
    // The triangle is clipped to the viewport, resulting in a full-screen quad

    fragUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(fragUV * 2.0 - 1.0, 0.0, 1.0);

    // Flip Y for Vulkan coordinate system (Vulkan Y is top-to-bottom)
    fragUV.y = 1.0 - fragUV.y;
}
