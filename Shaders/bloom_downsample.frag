#version 450

// 13-tap Karis average downsample for physically-based bloom
// Based on Call of Duty: Advanced Warfare presentation

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inputTexture;

layout(push_constant) uniform PushConstants {
    float threshold;
    float softKnee;
    float intensity;
    float scatter;
    vec2 texelSize;
    int mipLevel;
    int isUpsampling;
} pc;

#include "post_process_common.glsl"

// Karis average to prevent fireflies (bright spots causing bloom artifacts)
vec3 KarisAverage(vec3 c1, vec3 c2, vec3 c3, vec3 c4) {
    float w1 = 1.0 / (1.0 + Luminance(c1));
    float w2 = 1.0 / (1.0 + Luminance(c2));
    float w3 = 1.0 / (1.0 + Luminance(c3));
    float w4 = 1.0 / (1.0 + Luminance(c4));
    return (c1 * w1 + c2 * w2 + c3 * w3 + c4 * w4) / (w1 + w2 + w3 + w4);
}

// Apply threshold with soft knee transition
vec3 ApplyThreshold(vec3 color) {
    if (pc.threshold <= 0.0) return color;
    return SoftThreshold(color, pc.threshold, pc.softKnee * pc.threshold);
}

void main() {
    vec2 uv = fragUV;
    vec2 texelSize = pc.texelSize;
    
    // 13-tap filter pattern:
    //   A   B   C
    //     D   E
    //   F   G   H
    //     I   J
    //   K   L   M
    
    vec3 A = texture(inputTexture, uv + vec2(-1.0, -1.0) * texelSize).rgb;
    vec3 B = texture(inputTexture, uv + vec2( 0.0, -1.0) * texelSize).rgb;
    vec3 C = texture(inputTexture, uv + vec2( 1.0, -1.0) * texelSize).rgb;
    
    vec3 D = texture(inputTexture, uv + vec2(-0.5, -0.5) * texelSize).rgb;
    vec3 E = texture(inputTexture, uv + vec2( 0.5, -0.5) * texelSize).rgb;
    
    vec3 F = texture(inputTexture, uv + vec2(-1.0,  0.0) * texelSize).rgb;
    vec3 G = texture(inputTexture, uv).rgb;
    vec3 H = texture(inputTexture, uv + vec2( 1.0,  0.0) * texelSize).rgb;
    
    vec3 I = texture(inputTexture, uv + vec2(-0.5,  0.5) * texelSize).rgb;
    vec3 J = texture(inputTexture, uv + vec2( 0.5,  0.5) * texelSize).rgb;
    
    vec3 K = texture(inputTexture, uv + vec2(-1.0,  1.0) * texelSize).rgb;
    vec3 L = texture(inputTexture, uv + vec2( 0.0,  1.0) * texelSize).rgb;
    vec3 M = texture(inputTexture, uv + vec2( 1.0,  1.0) * texelSize).rgb;
    
    vec3 result;
    
    // First mip level uses Karis average to prevent fireflies
    if (pc.mipLevel == 0) {
        // Apply threshold
        A = ApplyThreshold(A);
        B = ApplyThreshold(B);
        C = ApplyThreshold(C);
        D = ApplyThreshold(D);
        E = ApplyThreshold(E);
        F = ApplyThreshold(F);
        G = ApplyThreshold(G);
        H = ApplyThreshold(H);
        I = ApplyThreshold(I);
        J = ApplyThreshold(J);
        K = ApplyThreshold(K);
        L = ApplyThreshold(L);
        M = ApplyThreshold(M);
        
        // 5 groups of 4 samples with Karis average
        vec3 g1 = KarisAverage(A, B, D, G);
        vec3 g2 = KarisAverage(B, C, E, H);
        vec3 g3 = KarisAverage(F, G, I, K);
        vec3 g4 = KarisAverage(G, H, J, L);
        vec3 g5 = KarisAverage(D, E, I, J);
        
        // Weighted combination
        result = g5 * 0.5 + (g1 + g2 + g3 + g4) * 0.125;
    } else {
        // Standard 13-tap filter for subsequent mips
        vec3 d0 = (D + E + I + J) * 0.25;
        vec3 d1 = (A + B + G + F) * 0.25;
        vec3 d2 = (B + C + H + G) * 0.25;
        vec3 d3 = (F + G + L + K) * 0.25;
        vec3 d4 = (G + H + M + L) * 0.25;
        
        result = d0 * 0.5 + (d1 + d2 + d3 + d4) * 0.125;
    }
    
    outColor = vec4(result, 1.0);
}
