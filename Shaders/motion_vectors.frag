#version 450

// Motion Vector Generation Fragment Shader
// Outputs screen-space velocity for TAA reprojection

layout(location = 0) in vec4 inCurrentClipPos;
layout(location = 1) in vec4 inPreviousClipPos;
layout(location = 2) in vec3 inWorldPos;

layout(location = 0) out vec2 outMotionVector;

layout(push_constant) uniform PushConstants {
    vec2 jitterOffset;      // Current frame jitter
    vec2 prevJitterOffset;  // Previous frame jitter
} pushConstants;

void main() {
    // Convert clip space to NDC
    vec2 currentNDC = inCurrentClipPos.xy / inCurrentClipPos.w;
    vec2 previousNDC = inPreviousClipPos.xy / inPreviousClipPos.w;
    
    // Remove jitter from current and previous positions
    currentNDC -= pushConstants.jitterOffset;
    previousNDC -= pushConstants.prevJitterOffset;
    
    // Convert NDC to UV space [0, 1]
    vec2 currentUV = currentNDC * 0.5 + 0.5;
    vec2 previousUV = previousNDC * 0.5 + 0.5;
    
    // Motion vector = current - previous (where the pixel came from)
    outMotionVector = currentUV - previousUV;
}
