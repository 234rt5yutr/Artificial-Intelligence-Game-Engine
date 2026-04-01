#version 450

// 9-tap tent filter upsample for bloom
// Provides smooth, energy-conserving upsampling

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inputTexture;
layout(set = 0, binding = 1) uniform sampler2D currentMip;  // Current resolution mip for blending

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
    vec2 uv = fragUV;
    vec2 texelSize = pc.texelSize;
    
    // 9-tap tent filter (3x3 kernel with bilinear weights)
    // This provides smooth upsampling with minimal aliasing
    //
    // Weights:
    // 1/16  2/16  1/16
    // 2/16  4/16  2/16
    // 1/16  2/16  1/16
    
    vec3 a = texture(inputTexture, uv + vec2(-1.0, -1.0) * texelSize).rgb;
    vec3 b = texture(inputTexture, uv + vec2( 0.0, -1.0) * texelSize).rgb;
    vec3 c = texture(inputTexture, uv + vec2( 1.0, -1.0) * texelSize).rgb;
    
    vec3 d = texture(inputTexture, uv + vec2(-1.0,  0.0) * texelSize).rgb;
    vec3 e = texture(inputTexture, uv).rgb;
    vec3 f = texture(inputTexture, uv + vec2( 1.0,  0.0) * texelSize).rgb;
    
    vec3 g = texture(inputTexture, uv + vec2(-1.0,  1.0) * texelSize).rgb;
    vec3 h = texture(inputTexture, uv + vec2( 0.0,  1.0) * texelSize).rgb;
    vec3 i = texture(inputTexture, uv + vec2( 1.0,  1.0) * texelSize).rgb;
    
    // Apply tent filter weights
    vec3 upsample = (a + c + g + i) * (1.0 / 16.0);
    upsample += (b + d + f + h) * (2.0 / 16.0);
    upsample += e * (4.0 / 16.0);
    
    // Blend with current mip level using scatter parameter
    // Scatter controls how much energy spreads across mip levels
    vec3 currentColor = texture(currentMip, uv).rgb;
    vec3 result = mix(currentColor, upsample, pc.scatter);
    
    outColor = vec4(result, 1.0);
}
