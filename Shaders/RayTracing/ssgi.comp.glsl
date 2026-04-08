// Screen-Space Global Illumination Compute Shader
#version 460

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, rgba16f) uniform image2D indirectOutput;
layout(set = 0, binding = 1) uniform sampler2D gBufferAlbedo;
layout(set = 0, binding = 2) uniform sampler2D gBufferNormal;
layout(set = 0, binding = 3) uniform sampler2D gBufferDepth;
layout(set = 0, binding = 4) uniform sampler2D sceneColor;
layout(set = 0, binding = 5, r8) uniform image2D aoOutput;

layout(set = 1, binding = 0) uniform SSGIParams {
    mat4 invViewProj;
    mat4 viewProj;
    vec4 cameraPos;
    float radius;
    float intensity;
    float falloff;
    uint sampleCount;
    uint frameIndex;
    float aoStrength;
    vec2 screenSize;
} params;

// Fibonacci spiral sampling
const float PHI = 1.61803398875;
vec3 fibonacciHemisphere(uint i, uint n, vec3 normal) {
    float theta = 2.0 * 3.14159265 * float(i) / PHI;
    float z = 1.0 - (2.0 * float(i) + 1.0) / float(2 * n);
    float r = sqrt(1.0 - z * z);
    
    vec3 sampleDir = vec3(r * cos(theta), r * sin(theta), z);
    
    // Orient to normal
    vec3 up = abs(normal.y) < 0.999 ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);
    
    return normalize(tangent * sampleDir.x + bitangent * sampleDir.y + normal * sampleDir.z);
}

vec3 reconstructWorldPos(vec2 uv, float depth) {
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y;
    vec4 worldPos = params.invViewProj * clipPos;
    return worldPos.xyz / worldPos.w;
}

vec2 worldToScreen(vec3 worldPos) {
    vec4 clipPos = params.viewProj * vec4(worldPos, 1.0);
    clipPos.xy /= clipPos.w;
    clipPos.y = -clipPos.y;
    return clipPos.xy * 0.5 + 0.5;
}

void main() {
    ivec2 pixelCoord = ivec2(gl_GlobalInvocationID.xy);
    
    if (pixelCoord.x >= int(params.screenSize.x) || pixelCoord.y >= int(params.screenSize.y)) {
        return;
    }
    
    vec2 uv = (vec2(pixelCoord) + 0.5) / params.screenSize;
    
    float depth = textureLod(gBufferDepth, uv, 0).r;
    if (depth >= 1.0) {
        imageStore(indirectOutput, pixelCoord, vec4(0.0));
        imageStore(aoOutput, pixelCoord, vec4(1.0));
        return;
    }
    
    vec3 worldPos = reconstructWorldPos(uv, depth);
    vec3 normal = normalize(textureLod(gBufferNormal, uv, 0).xyz * 2.0 - 1.0);
    vec3 albedo = textureLod(gBufferAlbedo, uv, 0).rgb;
    
    vec3 indirect = vec3(0.0);
    float occlusion = 0.0;
    uint validSamples = 0;
    
    for (uint i = 0; i < params.sampleCount; i++) {
        vec3 sampleDir = fibonacciHemisphere(i + params.frameIndex * params.sampleCount, 
                                              params.sampleCount, normal);
        
        // March along the sample direction
        for (float t = 0.1; t < params.radius; t += params.radius * 0.1) {
            vec3 samplePos = worldPos + sampleDir * t;
            vec2 sampleUV = worldToScreen(samplePos);
            
            if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) {
                break;
            }
            
            float sampleDepth = textureLod(gBufferDepth, sampleUV, 0).r;
            vec3 reconstructed = reconstructWorldPos(sampleUV, sampleDepth);
            
            float dist = distance(samplePos, reconstructed);
            
            if (dist < params.radius * 0.2) {
                // Hit something - sample its color
                vec3 hitColor = textureLod(sceneColor, sampleUV, 2.0).rgb;
                vec3 hitNormal = normalize(textureLod(gBufferNormal, sampleUV, 0).xyz * 2.0 - 1.0);
                
                float weight = max(0.0, dot(hitNormal, -sampleDir));
                weight *= exp(-t * params.falloff);
                
                indirect += hitColor * weight;
                occlusion += 1.0;
                validSamples++;
                break;
            }
        }
    }
    
    if (validSamples > 0) {
        indirect /= float(validSamples);
        occlusion /= float(params.sampleCount);
    }
    
    indirect *= albedo * params.intensity;
    float ao = 1.0 - occlusion * params.aoStrength;
    
    imageStore(indirectOutput, pixelCoord, vec4(indirect, 1.0));
    imageStore(aoOutput, pixelCoord, vec4(ao));
}
