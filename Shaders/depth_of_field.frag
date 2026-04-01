#version 450

// Depth of Field shader with Circle of Confusion calculation
// Supports both near and far field blur

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;
layout(set = 0, binding = 1) uniform sampler2D depthTexture;
layout(set = 0, binding = 2) uniform sampler2D cocTexture;

layout(push_constant) uniform PushConstants {
    float focalDistance;
    float focalRange;
    float maxBlur;
    float aperture;
    vec2 texelSize;
    float nearPlane;
    float farPlane;
    int horizontal;
    float padding;
} pc;

#include "post_process_common.glsl"

// Calculate Circle of Confusion
float CalculateCoC(float depth) {
    float linearDepth = LinearizeDepth(depth, pc.nearPlane, pc.farPlane);
    
    // Distance from focal plane
    float coc = (linearDepth - pc.focalDistance) / pc.focalRange;
    
    // Clamp to max blur
    return clamp(coc, -pc.maxBlur, pc.maxBlur);
}

// Bokeh-weighted blur sample
vec3 BokehSample(vec2 uv, float cocRadius, int samples) {
    vec3 result = vec3(0.0);
    float totalWeight = 0.0;
    
    // Golden angle spiral for sample distribution
    const float goldenAngle = 2.39996323;
    
    for (int i = 0; i < samples; ++i) {
        float r = sqrt(float(i) / float(samples));
        float theta = float(i) * goldenAngle;
        
        vec2 offset = vec2(cos(theta), sin(theta)) * r * cocRadius * pc.texelSize;
        vec2 sampleUV = uv + offset;
        
        vec3 sampleColor = texture(sceneColor, sampleUV).rgb;
        float sampleDepth = texture(depthTexture, sampleUV).r;
        float sampleCoC = abs(texture(cocTexture, sampleUV).r);
        
        // Weight by CoC overlap
        float weight = clamp(sampleCoC * 2.0, 0.0, 1.0);
        
        // Boost bright samples (bokeh highlight)
        float brightness = Luminance(sampleColor);
        weight *= 1.0 + brightness * 0.5;
        
        result += sampleColor * weight;
        totalWeight += weight;
    }
    
    return result / max(totalWeight, 0.001);
}

void main() {
    float depth = texture(depthTexture, fragUV).r;
    float coc = CalculateCoC(depth);
    
    vec3 sharpColor = texture(sceneColor, fragUV).rgb;
    
    // Early out for in-focus pixels
    if (abs(coc) < 0.01) {
        outColor = vec4(sharpColor, 1.0);
        return;
    }
    
    // Apply bokeh blur
    int samples = int(16.0 * abs(coc));
    samples = clamp(samples, 8, 64);
    
    vec3 blurredColor = BokehSample(fragUV, abs(coc) * pc.maxBlur * 32.0, samples);
    
    // Blend between sharp and blurred based on CoC
    float blendFactor = smoothstep(0.0, 0.5, abs(coc));
    vec3 finalColor = mix(sharpColor, blurredColor, blendFactor);
    
    outColor = vec4(finalColor, 1.0);
}
