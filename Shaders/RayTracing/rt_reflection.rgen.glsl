// RT Reflection Ray Generation Shader
#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadEXT vec4 hitValue;

layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;
layout(set = 0, binding = 1, rgba16f) uniform image2D reflectionOutput;
layout(set = 0, binding = 2) uniform sampler2D gBufferAlbedo;
layout(set = 0, binding = 3) uniform sampler2D gBufferNormal;
layout(set = 0, binding = 4) uniform sampler2D gBufferMetallicRoughness;
layout(set = 0, binding = 5) uniform sampler2D gBufferDepth;
layout(set = 0, binding = 6) uniform sampler2D sceneColor;
layout(set = 0, binding = 7) uniform samplerCube environmentMap;

layout(set = 1, binding = 0) uniform CameraData {
    mat4 viewProj;
    mat4 invViewProj;
    mat4 prevViewProj;
    vec4 cameraPos;
} camera;

layout(push_constant) uniform PushConstants {
    uint frameIndex;
    uint maxBounces;
    float maxRayDistance;
    float roughnessThreshold;
    float metallicThreshold;
    float fireflySuppression;
    uint flags;
    uint padding;
} pc;

// PCG hash
uint pcg(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

vec2 random2(uvec2 seed) {
    return vec2(
        float(pcg(seed.x ^ pcg(seed.y))) / float(0xFFFFFFFFu),
        float(pcg(seed.y ^ pcg(seed.x + 1u))) / float(0xFFFFFFFFu)
    );
}

vec3 reconstructWorldPos(vec2 uv, float depth) {
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y;
    vec4 worldPos = camera.invViewProj * clipPos;
    return worldPos.xyz / worldPos.w;
}

// GGX importance sampling
vec3 importanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    
    float phi = 2.0 * 3.14159265359 * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a2 - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    
    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;
    
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    
    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

// Fresnel-Schlick
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    ivec2 pixelCoord = ivec2(gl_LaunchIDEXT.xy);
    vec2 uv = (vec2(pixelCoord) + 0.5) / vec2(gl_LaunchSizeEXT.xy);
    
    float depth = textureLod(gBufferDepth, uv, 0).r;
    
    // Sky pixels - no reflections needed
    if (depth >= 1.0) {
        imageStore(reflectionOutput, pixelCoord, vec4(0.0));
        return;
    }
    
    // Get material properties
    vec4 metallicRoughness = textureLod(gBufferMetallicRoughness, uv, 0);
    float metallic = metallicRoughness.r;
    float roughness = metallicRoughness.g;
    
    // Skip non-reflective surfaces
    if (metallic < pc.metallicThreshold && roughness > pc.roughnessThreshold) {
        // Use environment map fallback
        vec3 worldPos = reconstructWorldPos(uv, depth);
        vec3 normal = normalize(textureLod(gBufferNormal, uv, 0).xyz * 2.0 - 1.0);
        vec3 viewDir = normalize(camera.cameraPos.xyz - worldPos);
        vec3 reflectDir = reflect(-viewDir, normal);
        vec3 envColor = textureLod(environmentMap, reflectDir, roughness * 8.0).rgb;
        imageStore(reflectionOutput, pixelCoord, vec4(envColor * metallic * 0.5, 0.0));
        return;
    }
    
    vec3 worldPos = reconstructWorldPos(uv, depth);
    vec3 normal = normalize(textureLod(gBufferNormal, uv, 0).xyz * 2.0 - 1.0);
    vec3 albedo = textureLod(gBufferAlbedo, uv, 0).rgb;
    vec3 viewDir = normalize(camera.cameraPos.xyz - worldPos);
    
    // Generate sample direction with importance sampling
    uvec2 seed = uvec2(pixelCoord) + uvec2(pc.frameIndex, 0);
    vec2 Xi = random2(seed);
    
    vec3 halfVec = importanceSampleGGX(Xi, normal, max(roughness, 0.001));
    vec3 rayDir = reflect(-viewDir, halfVec);
    
    // Ensure ray goes outward
    if (dot(rayDir, normal) <= 0.0) {
        rayDir = reflect(rayDir, normal);
    }
    
    // Offset origin to avoid self-intersection
    vec3 origin = worldPos + normal * 0.001;
    
    hitValue = vec4(0.0);
    
    traceRayEXT(
        topLevelAS,
        gl_RayFlagsOpaqueEXT,
        0xFF,
        0, 1, 0,
        origin,
        0.001,
        rayDir,
        pc.maxRayDistance,
        0
    );
    
    // Calculate Fresnel
    float NdotV = max(dot(normal, viewDir), 0.0);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 fresnel = fresnelSchlick(NdotV, F0);
    
    // Apply reflection with fresnel
    vec3 reflection = hitValue.rgb * fresnel;
    
    // Firefly suppression
    float luminance = dot(reflection, vec3(0.299, 0.587, 0.114));
    if (luminance > pc.fireflySuppression) {
        reflection *= pc.fireflySuppression / luminance;
    }
    
    imageStore(reflectionOutput, pixelCoord, vec4(reflection, hitValue.a));
}
