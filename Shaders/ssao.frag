#version 450

// Screen Space Ambient Occlusion (SSAO)
// Uses hemisphere sampling with noise for contact shadow calculation

layout(location = 0) in vec2 fragUV;
layout(location = 0) out float outAO;

layout(set = 0, binding = 0) uniform sampler2D depthTexture;
layout(set = 0, binding = 1) uniform sampler2D normalTexture;
layout(set = 0, binding = 2) uniform sampler2D noiseTexture;

layout(set = 0, binding = 3) uniform SampleKernel {
    vec4 samples[64];
} kernel;

layout(push_constant) uniform PushConstants {
    mat4 projection;
    float radius;
    float bias;
    float intensity;
    int kernelSize;
    vec2 screenSize;
    float nearPlane;
    float farPlane;
} pc;

#include "post_process_common.glsl"

// Reconstruct view-space position from depth
vec3 GetViewPosition(vec2 uv) {
    float depth = texture(depthTexture, uv).r;
    float linearDepth = LinearizeDepth(depth, pc.nearPlane, pc.farPlane);
    
    // Reconstruct position using projection matrix inverse
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = inverse(pc.projection) * clipPos;
    return viewPos.xyz / viewPos.w;
}

// Get view-space normal from G-buffer
vec3 GetViewNormal(vec2 uv) {
    vec3 normal = texture(normalTexture, uv).xyz * 2.0 - 1.0;
    return normalize(normal);
}

void main() {
    vec3 fragPos = GetViewPosition(fragUV);
    vec3 normal = GetViewNormal(fragUV);
    
    // Tile noise texture over screen
    vec2 noiseScale = pc.screenSize / 4.0;
    vec3 randomVec = texture(noiseTexture, fragUV * noiseScale).xyz;
    
    // Create TBN matrix for hemisphere orientation
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);
    
    // Sample occlusion
    float occlusion = 0.0;
    int validSamples = 0;
    
    for (int i = 0; i < pc.kernelSize; ++i) {
        // Get sample position in view space
        vec3 sampleDir = TBN * kernel.samples[i].xyz;
        vec3 samplePos = fragPos + sampleDir * pc.radius;
        
        // Project sample position to screen space
        vec4 offset = pc.projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;
        
        // Skip samples outside screen
        if (offset.x < 0.0 || offset.x > 1.0 || offset.y < 0.0 || offset.y > 1.0) {
            continue;
        }
        
        // Get depth at sample position
        float sampleDepth = GetViewPosition(offset.xy).z;
        
        // Range check & accumulate occlusion
        float rangeCheck = smoothstep(0.0, 1.0, pc.radius / abs(fragPos.z - sampleDepth));
        occlusion += (sampleDepth >= samplePos.z + pc.bias ? 1.0 : 0.0) * rangeCheck;
        validSamples++;
    }
    
    // Normalize and invert (1 = no occlusion, 0 = full occlusion)
    if (validSamples > 0) {
        occlusion = 1.0 - (occlusion / float(validSamples));
    } else {
        occlusion = 1.0;
    }
    
    // Apply intensity
    outAO = pow(occlusion, pc.intensity);
}
