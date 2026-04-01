#version 450

// Bloom composite pass - additively blend bloom with original scene

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;
layout(set = 0, binding = 1) uniform sampler2D bloomTexture;

layout(push_constant) uniform PushConstants {
    float threshold;
    float softKnee;
    float intensity;
    float scatter;
    vec2 texelSize;
    int mipLevel;
    int isUpsampling;
} pc;

void main() {
    vec3 scene = texture(sceneColor, fragUV).rgb;
    vec3 bloom = texture(bloomTexture, fragUV).rgb;
    
    // Additive blend with intensity control
    vec3 result = scene + bloom * pc.intensity;
    
    outColor = vec4(result, 1.0);
}
