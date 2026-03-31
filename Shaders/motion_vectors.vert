#version 450

// Motion Vector Generation Vertex Shader
// Transforms vertex to both current and previous clip space

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;

layout(binding = 0) uniform MotionVectorUBO {
    mat4 currentModel;
    mat4 currentViewProjection;
    mat4 previousModel;
    mat4 previousViewProjection;
    vec2 jitterOffset;
    vec2 prevJitterOffset;
} ubo;

layout(location = 0) out vec4 outCurrentClipPos;
layout(location = 1) out vec4 outPreviousClipPos;
layout(location = 2) out vec3 outWorldPos;

void main() {
    // Current frame position
    vec4 worldPos = ubo.currentModel * vec4(inPosition, 1.0);
    vec4 clipPos = ubo.currentViewProjection * worldPos;
    
    // Apply jitter for TAA
    clipPos.xy += ubo.jitterOffset * clipPos.w;
    
    gl_Position = clipPos;
    outCurrentClipPos = clipPos;
    
    // Previous frame position
    vec4 prevWorldPos = ubo.previousModel * vec4(inPosition, 1.0);
    vec4 prevClipPos = ubo.previousViewProjection * prevWorldPos;
    prevClipPos.xy += ubo.prevJitterOffset * prevClipPos.w;
    
    outPreviousClipPos = prevClipPos;
    outWorldPos = worldPos.xyz;
}
