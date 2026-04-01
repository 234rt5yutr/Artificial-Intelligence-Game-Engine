#version 450

// Skinned PBR Fragment Shader
// Extended from standard PBR fragment shader with additional outputs for skeletal meshes

//=============================================================================
// Inputs from Vertex Shader
//=============================================================================

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in vec4 inPosLightSpace;

//=============================================================================
// Material Textures
//=============================================================================

layout(set = 2, binding = 0) uniform sampler2D albedoMap;
layout(set = 2, binding = 1) uniform sampler2D normalMap;
layout(set = 2, binding = 2) uniform sampler2D metallicRoughnessMap;  // G = roughness, B = metallic
layout(set = 2, binding = 3) uniform sampler2D aoMap;
layout(set = 2, binding = 4) uniform sampler2D emissiveMap;

//=============================================================================
// Light Structures
//=============================================================================

struct DirectionalLight {
    vec4 direction;      // xyz = direction, w = intensity
    vec4 color;          // rgb = color, a = ambient intensity
};

struct PointLight {
    vec4 positionRadius; // xyz = position, w = radius
    vec4 colorIntensity; // rgb = color, a = intensity
};

//=============================================================================
// Uniform Buffers
//=============================================================================

layout(set = 0, binding = 1, std140) uniform LightingUBO {
    DirectionalLight directionalLight;
    vec4 viewPos;
    uint pointLightCount;
    uint gridSizeX;
    uint gridSizeY;
    uint gridSizeZ;
    float zNear;
    float zFar;
    vec2 padding;
} lighting;

//=============================================================================
// Storage Buffers (Forward+ Lighting)
//=============================================================================

layout(std430, set = 3, binding = 0) readonly buffer PointLightBuffer {
    PointLight pointLights[];
};

struct LightGrid {
    uint offset;
    uint count;
};

layout(std430, set = 3, binding = 1) readonly buffer LightGridBuffer {
    LightGrid lightGrid[];
};

layout(std430, set = 3, binding = 2) readonly buffer LightIndexBuffer {
    uint lightIndices[];
};

//=============================================================================
// Output
//=============================================================================

layout(location = 0) out vec4 outColor;

//=============================================================================
// Constants
//=============================================================================

const float PI = 3.14159265359;

//=============================================================================
// PBR Functions
//=============================================================================

// Normal mapping with TBN matrix
vec3 getNormalFromMap() {
    vec3 tangentNormal = texture(normalMap, inTexCoord).xyz * 2.0 - 1.0;
    
    vec3 N = normalize(inNormal);
    vec3 T = normalize(inTangent);
    vec3 B = normalize(inBitangent);
    mat3 TBN = mat3(T, B, N);
    
    return normalize(TBN * tangentNormal);
}

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// GGX/Trowbridge-Reitz normal distribution function
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return a2 / max(denom, 0.0000001);
}

// Schlick-GGX geometry function
float geometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Smith's method for geometry function
float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = geometrySchlickGGX(NdotL, roughness);
    float ggx2 = geometrySchlickGGX(NdotV, roughness);
    
    return ggx1 * ggx2;
}

// Calculate lighting contribution
vec3 calculateLighting(vec3 N, vec3 V, vec3 L, vec3 radiance, 
                       vec3 albedo, float metallic, float roughness, vec3 F0) {
    vec3 H = normalize(V + L);
    
    // Cook-Torrance BRDF
    float NDF = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    
    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;
    
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;
    
    float NdotL = max(dot(N, L), 0.0);
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

//=============================================================================
// Main Fragment Shader
//=============================================================================

void main() {
    // Sample material textures
    vec4 albedoSample = texture(albedoMap, inTexCoord);
    vec3 albedo = pow(albedoSample.rgb, vec3(2.2));  // sRGB to linear
    float alpha = albedoSample.a;
    
    vec3 metallicRoughness = texture(metallicRoughnessMap, inTexCoord).rgb;
    float metallic = metallicRoughness.b;
    float roughness = metallicRoughness.g;
    
    float ao = texture(aoMap, inTexCoord).r;
    vec3 emissive = texture(emissiveMap, inTexCoord).rgb;
    
    // Get normal from normal map
    vec3 N = getNormalFromMap();
    vec3 V = normalize(lighting.viewPos.xyz - inWorldPos);
    
    // Fresnel reflectance at normal incidence
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);
    
    // Accumulated lighting
    vec3 Lo = vec3(0.0);
    
    // Directional light
    vec3 L = normalize(-lighting.directionalLight.direction.xyz);
    vec3 radiance = lighting.directionalLight.color.rgb * lighting.directionalLight.direction.w;
    Lo += calculateLighting(N, V, L, radiance, albedo, metallic, roughness, F0);
    
    // Forward+ clustered point lights
    uint tileX = uint(gl_FragCoord.x) / 16;
    uint tileY = uint(gl_FragCoord.y) / 16;
    uint tileZ = uint(clamp(gl_FragCoord.z * float(lighting.gridSizeZ), 0.0, float(lighting.gridSizeZ - 1)));
    
    uint clusterIndex = tileX + (tileY * lighting.gridSizeX) + (tileZ * lighting.gridSizeX * lighting.gridSizeY);
    uint maxClusters = lighting.gridSizeX * lighting.gridSizeY * lighting.gridSizeZ;
    
    if (clusterIndex < maxClusters) {
        LightGrid grid = lightGrid[clusterIndex];
        
        for (uint i = 0; i < grid.count; ++i) {
            uint lightIndex = lightIndices[grid.offset + i];
            PointLight pLight = pointLights[lightIndex];
            
            vec3 lightVec = pLight.positionRadius.xyz - inWorldPos;
            float distance = length(lightVec);
            float radius = pLight.positionRadius.w;
            
            if (distance < radius) {
                vec3 L_p = normalize(lightVec);
                
                // Attenuation
                float attenuation = 1.0 / (distance * distance + 1.0);
                attenuation *= clamp(1.0 - (distance / radius), 0.0, 1.0);
                
                vec3 radiance_p = pLight.colorIntensity.rgb * pLight.colorIntensity.a * attenuation;
                Lo += calculateLighting(N, V, L_p, radiance_p, albedo, metallic, roughness, F0);
            }
        }
    }
    
    // Ambient lighting
    vec3 ambient = vec3(0.03) * albedo * ao * lighting.directionalLight.color.a;
    
    // Final color
    vec3 color = ambient + Lo + emissive;
    
    // HDR tonemapping (Reinhard)
    color = color / (color + vec3(1.0));
    
    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));
    
    outColor = vec4(color, alpha);
}
