// RT Shadow Ray Generation Shader
#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadEXT float shadowVisibility;

layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;
layout(set = 0, binding = 1, r32f) uniform image2D shadowMask;
layout(set = 0, binding = 2) uniform sampler2D gBufferNormal;
layout(set = 0, binding = 3) uniform sampler2D gBufferDepth;

layout(set = 1, binding = 0) uniform LightData {
    vec4 positionType;      // xyz = position/direction, w = type (0=dir, 1=point, 2=spot)
    vec4 colorRange;        // rgb = color, a = range
    vec4 params;            // x = softRadius, y = shadowBias, z = normalBias, w = maxDist
} light;

layout(set = 1, binding = 1) uniform CameraData {
    mat4 viewProj;
    mat4 invViewProj;
    vec4 cameraPos;
} camera;

layout(push_constant) uniform PushConstants {
    uint frameIndex;
    uint samplesPerPixel;
    uint lightIndex;
    uint padding;
} pc;

// PCG hash for random numbers
uint pcg(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float random(uvec2 seed) {
    return float(pcg(seed.x ^ pcg(seed.y))) / float(0xFFFFFFFFu);
}

vec2 sampleDisk(vec2 seed) {
    float angle = random(uvec2(seed)) * 6.283185307;
    float radius = sqrt(random(uvec2(seed.yx + 0.5)));
    return vec2(cos(angle), sin(angle)) * radius;
}

vec3 reconstructWorldPos(vec2 uv, float depth) {
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y;  // Vulkan flip
    vec4 worldPos = camera.invViewProj * clipPos;
    return worldPos.xyz / worldPos.w;
}

void main() {
    ivec2 pixelCoord = ivec2(gl_LaunchIDEXT.xy);
    vec2 uv = (vec2(pixelCoord) + 0.5) / vec2(gl_LaunchSizeEXT.xy);
    
    float depth = textureLod(gBufferDepth, uv, 0).r;
    
    // Sky pixels are always lit
    if (depth >= 1.0) {
        imageStore(shadowMask, pixelCoord, vec4(1.0));
        return;
    }
    
    vec3 worldPos = reconstructWorldPos(uv, depth);
    vec3 normal = normalize(textureLod(gBufferNormal, uv, 0).xyz * 2.0 - 1.0);
    
    // Calculate light direction and distance
    vec3 lightDir;
    float lightDistance;
    
    if (light.positionType.w < 0.5) {
        // Directional light
        lightDir = normalize(-light.positionType.xyz);
        lightDistance = light.params.w;
    } else {
        // Point/Spot light
        vec3 toLight = light.positionType.xyz - worldPos;
        lightDistance = length(toLight);
        lightDir = toLight / max(lightDistance, 0.0001);
        
        // Check range
        if (lightDistance > light.colorRange.a) {
            imageStore(shadowMask, pixelCoord, vec4(1.0));
            return;
        }
    }
    
    // Check if surface faces away from light
    float NdotL = dot(normal, lightDir);
    if (NdotL <= 0.0) {
        imageStore(shadowMask, pixelCoord, vec4(0.0));
        return;
    }
    
    // Bias ray origin along normal
    vec3 origin = worldPos + normal * light.params.z + lightDir * light.params.y;
    
    float totalVisibility = 0.0;
    uint sampleCount = (light.params.x > 0.0) ? pc.samplesPerPixel : 1u;
    
    for (uint i = 0; i < sampleCount; i++) {
        vec3 rayDir = lightDir;
        
        // Soft shadow jittering
        if (light.params.x > 0.0) {
            uvec2 seed = uvec2(pixelCoord) + uvec2(pc.frameIndex * sampleCount + i, i);
            vec2 diskSample = sampleDisk(vec2(seed)) * light.params.x;
            
            // Create orthonormal basis around light direction
            vec3 tangent = abs(lightDir.y) < 0.999 
                ? normalize(cross(lightDir, vec3(0, 1, 0)))
                : normalize(cross(lightDir, vec3(1, 0, 0)));
            vec3 bitangent = cross(lightDir, tangent);
            
            rayDir = normalize(lightDir + tangent * diskSample.x + bitangent * diskSample.y);
        }
        
        shadowVisibility = 0.0;
        
        traceRayEXT(
            topLevelAS,
            gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT,
            0xFF,
            0, 0, 0,
            origin,
            0.001,
            rayDir,
            lightDistance - 0.01,
            0
        );
        
        totalVisibility += shadowVisibility;
    }
    
    float visibility = totalVisibility / float(sampleCount);
    imageStore(shadowMask, pixelCoord, vec4(visibility));
}
