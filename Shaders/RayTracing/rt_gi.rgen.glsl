// Ray-Traced Global Illumination Ray Generation Shader
#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadEXT vec4 giPayload;

layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;
layout(set = 0, binding = 1, rgba16f) uniform image2D giOutput;
layout(set = 0, binding = 2) uniform sampler2D gBufferAlbedo;
layout(set = 0, binding = 3) uniform sampler2D gBufferNormal;
layout(set = 0, binding = 4) uniform sampler2D gBufferDepth;

layout(set = 1, binding = 0) uniform RTGIParams {
    mat4 invViewProj;
    vec4 cameraPos;
    uint maxBounces;
    uint samplesPerPixel;
    float maxDistance;
    float intensity;
    uint frameIndex;
    float russianRouletteDepth;
    vec2 screenSize;
} params;

// PCG random
uint pcg(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float random(inout uint seed) {
    seed = pcg(seed);
    return float(seed) / float(0xFFFFFFFFu);
}

vec3 cosineWeightedHemisphere(vec2 Xi, vec3 N) {
    float phi = 2.0 * 3.14159265 * Xi.x;
    float cosTheta = sqrt(Xi.y);
    float sinTheta = sqrt(1.0 - Xi.y);
    
    vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    
    vec3 up = abs(N.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    
    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

vec3 reconstructWorldPos(vec2 uv, float depth) {
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y;
    vec4 worldPos = params.invViewProj * clipPos;
    return worldPos.xyz / worldPos.w;
}

void main() {
    ivec2 pixelCoord = ivec2(gl_LaunchIDEXT.xy);
    vec2 uv = (vec2(pixelCoord) + 0.5) / params.screenSize;
    
    float depth = textureLod(gBufferDepth, uv, 0).r;
    if (depth >= 1.0) {
        imageStore(giOutput, pixelCoord, vec4(0.0));
        return;
    }
    
    vec3 worldPos = reconstructWorldPos(uv, depth);
    vec3 normal = normalize(textureLod(gBufferNormal, uv, 0).xyz * 2.0 - 1.0);
    vec3 albedo = textureLod(gBufferAlbedo, uv, 0).rgb;
    
    vec3 totalIndirect = vec3(0.0);
    uint seed = uint(pixelCoord.x * 1973 + pixelCoord.y * 9277 + params.frameIndex * 26699);
    
    for (uint s = 0; s < params.samplesPerPixel; s++) {
        vec2 Xi = vec2(random(seed), random(seed));
        vec3 rayDir = cosineWeightedHemisphere(Xi, normal);
        
        vec3 origin = worldPos + normal * 0.001;
        
        giPayload = vec4(0.0);
        
        traceRayEXT(
            topLevelAS,
            gl_RayFlagsOpaqueEXT,
            0xFF,
            0, 1, 0,
            origin,
            0.001,
            rayDir,
            params.maxDistance,
            0
        );
        
        // Weight by cosine term (already included in cosine-weighted sampling)
        totalIndirect += giPayload.rgb;
    }
    
    vec3 indirect = (totalIndirect / float(params.samplesPerPixel)) * albedo * params.intensity;
    
    imageStore(giOutput, pixelCoord, vec4(indirect, 1.0));
}
