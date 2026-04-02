#version 450
#extension GL_ARB_separate_shader_objects : enable

// MSDF Text Vertex Shader
// Transforms glyph quads from screen-space to clip-space

layout(push_constant) uniform PushConstants {
    mat4 projectionMatrix;    // Orthographic projection (screen-space to clip-space)
    vec4 textColor;           // Base text color (RGBA)
    vec4 outlineColor;        // Outline color (RGBA)
    vec4 shadowColor;         // Drop shadow color (RGBA)
    float outlineWidth;       // Outline width in pixels (0 = disabled)
    float shadowOffset;       // Shadow offset in pixels (0 = disabled)
    float distanceRange;      // MSDF distance range (typically 4.0)
    float fontSize;           // Font size in pixels
} pc;

layout(location = 0) in vec2 inPosition;      // Screen-space position
layout(location = 1) in vec2 inTexCoord;      // UV coordinates into atlas
layout(location = 2) in vec4 inColor;         // Per-vertex color tint (optional)

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out float fragDistanceRange;

void main() {
    // Transform from screen-space to clip-space
    gl_Position = pc.projectionMatrix * vec4(inPosition, 0.0, 1.0);
    
    fragTexCoord = inTexCoord;
    fragColor = inColor * pc.textColor;
    
    // Scale distance range based on font size (for consistent edges at all sizes)
    fragDistanceRange = pc.distanceRange / pc.fontSize;
}
