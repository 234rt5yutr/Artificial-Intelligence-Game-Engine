#version 450

// Bilateral blur for SSAO
// Preserves edges by weighting samples based on depth difference

layout(location = 0) in vec2 fragUV;
layout(location = 0) out float outAO;

layout(set = 0, binding = 0) uniform sampler2D aoTexture;
layout(set = 0, binding = 1) uniform sampler2D depthTexture;

layout(push_constant) uniform PushConstants {
    vec2 texelSize;
    int horizontal;
    float depthThreshold;
} pc;

#include "post_process_common.glsl"

void main() {
    vec2 direction = pc.horizontal == 1 ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    
    float centerAO = texture(aoTexture, fragUV).r;
    float centerDepth = texture(depthTexture, fragUV).r;
    
    float result = centerAO;
    float totalWeight = 1.0;
    
    // 4-tap bilateral blur in each direction
    const float weights[4] = float[4](0.324, 0.232, 0.0855, 0.0205);
    const float offsets[4] = float[4](1.0, 2.0, 3.0, 4.0);
    
    for (int i = 0; i < 4; ++i) {
        vec2 offset = direction * pc.texelSize * offsets[i];
        
        // Positive direction
        vec2 sampleUV = fragUV + offset;
        float sampleAO = texture(aoTexture, sampleUV).r;
        float sampleDepth = texture(depthTexture, sampleUV).r;
        
        // Bilateral weight based on depth difference
        float depthDiff = abs(centerDepth - sampleDepth);
        float bilateralWeight = exp(-depthDiff / pc.depthThreshold);
        float weight = weights[i] * bilateralWeight;
        
        result += sampleAO * weight;
        totalWeight += weight;
        
        // Negative direction
        sampleUV = fragUV - offset;
        sampleAO = texture(aoTexture, sampleUV).r;
        sampleDepth = texture(depthTexture, sampleUV).r;
        
        depthDiff = abs(centerDepth - sampleDepth);
        bilateralWeight = exp(-depthDiff / pc.depthThreshold);
        weight = weights[i] * bilateralWeight;
        
        result += sampleAO * weight;
        totalWeight += weight;
    }
    
    outAO = result / totalWeight;
}
