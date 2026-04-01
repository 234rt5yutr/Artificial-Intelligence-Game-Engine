#version 450

// Per-pixel motion blur based on velocity buffer
// Uses tile-based optimization for performance

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;
layout(set = 0, binding = 1) uniform sampler2D velocityTexture;
layout(set = 0, binding = 2) uniform sampler2D tileMaxTexture;
layout(set = 0, binding = 3) uniform CameraMotion {
    mat4 currentViewProj;
    mat4 previousViewProj;
    float shutterSpeed;
    float padding[3];
} camera;

layout(push_constant) uniform PushConstants {
    float scale;
    int samples;
    float maxVelocity;
    vec2 texelSize;
    float centerWeight;
    float padding;
} pc;

#include "post_process_common.glsl"

// Soft depth comparison for proper motion blur ordering
float SoftDepthCompare(float depthA, float depthB) {
    const float softness = 0.001;
    return clamp(1.0 - (depthA - depthB) / softness, 0.0, 1.0);
}

// Cone-shaped weight for velocity spreading
float Cone(float dist, float velocity) {
    return clamp(1.0 - dist / velocity, 0.0, 1.0);
}

// Cylinder-shaped weight (constant across velocity length)
float Cylinder(float dist, float velocity) {
    return 1.0 - smoothstep(0.95 * velocity, 1.05 * velocity, dist);
}

void main() {
    vec2 velocity = texture(velocityTexture, fragUV).xy;
    
    // Scale velocity
    velocity *= pc.scale;
    
    // Clamp to max velocity
    float velocityLength = length(velocity);
    if (velocityLength > pc.maxVelocity * pc.texelSize.x) {
        velocity = normalize(velocity) * pc.maxVelocity * pc.texelSize.x;
        velocityLength = pc.maxVelocity * pc.texelSize.x;
    }
    
    // Early out for static pixels
    if (velocityLength < 0.5 * pc.texelSize.x) {
        outColor = texture(sceneColor, fragUV);
        return;
    }
    
    // Check tile max for early out
    vec2 tileMax = texture(tileMaxTexture, fragUV).xy;
    float tileMaxLength = length(tileMax);
    
    if (tileMaxLength < 0.5 * pc.texelSize.x) {
        outColor = texture(sceneColor, fragUV);
        return;
    }
    
    // Sample along velocity vector
    vec3 result = vec3(0.0);
    float totalWeight = 0.0;
    
    vec3 centerColor = texture(sceneColor, fragUV).rgb;
    float centerDepth = texture(velocityTexture, fragUV).z;  // Depth stored in Z
    
    // Add center sample with higher weight
    result += centerColor * pc.centerWeight;
    totalWeight += pc.centerWeight;
    
    // Sample in both directions
    for (int i = 1; i <= pc.samples; ++i) {
        float t = float(i) / float(pc.samples);
        
        // Forward sample
        vec2 sampleUV = fragUV + velocity * t;
        if (sampleUV.x >= 0.0 && sampleUV.x <= 1.0 && sampleUV.y >= 0.0 && sampleUV.y <= 1.0) {
            vec3 sampleColor = texture(sceneColor, sampleUV).rgb;
            vec2 sampleVel = texture(velocityTexture, sampleUV).xy * pc.scale;
            float sampleDepth = texture(velocityTexture, sampleUV).z;
            
            // Weight by velocity and depth
            float sampleVelLen = length(sampleVel);
            float weight = Cone(velocityLength * t, velocityLength);
            weight *= SoftDepthCompare(centerDepth, sampleDepth);
            
            result += sampleColor * weight;
            totalWeight += weight;
        }
        
        // Backward sample
        sampleUV = fragUV - velocity * t;
        if (sampleUV.x >= 0.0 && sampleUV.x <= 1.0 && sampleUV.y >= 0.0 && sampleUV.y <= 1.0) {
            vec3 sampleColor = texture(sceneColor, sampleUV).rgb;
            vec2 sampleVel = texture(velocityTexture, sampleUV).xy * pc.scale;
            float sampleDepth = texture(velocityTexture, sampleUV).z;
            
            float sampleVelLen = length(sampleVel);
            float weight = Cone(velocityLength * t, velocityLength);
            weight *= SoftDepthCompare(centerDepth, sampleDepth);
            
            result += sampleColor * weight;
            totalWeight += weight;
        }
    }
    
    result /= max(totalWeight, 0.001);
    
    outColor = vec4(result, 1.0);
}
