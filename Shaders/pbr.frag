#version 450

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inTangent;

layout(binding = 1) uniform sampler2D albedoMap;
layout(binding = 2) uniform sampler2D normalMap;
layout(binding = 3) uniform sampler2D metallicMap;
layout(binding = 4) uniform sampler2D roughnessMap;

layout(location = 0) out vec4 outColor;

struct Light {
    vec4 position;
    vec4 color;
};

layout(binding = 5) uniform LightBuffer {
    Light directionalLight; // simplistic light struct for initial PRB implementation
    vec3 viewPos;
    uint gridSizeX;
    uint gridSizeY;
    uint gridSizeZ; // 32
    float zNear;
    float zFar;
} uboLights;

struct PointLight {
    vec4 positionAndRadius;
    vec4 colorAndIntensity;
};

struct LightGrid {
    uint offset;
    uint count;
};

layout(std430, binding = 6) readonly buffer PointLightBuffer {
    PointLight pointLights[];
};

layout(std430, binding = 7) readonly buffer LightGridBuffer {
    LightGrid lightGrid[];
};

layout(std430, binding = 8) readonly buffer LightIndexBuffer {
    uint lightIndices[];
};

const float PI = 3.14159265359;

// Calculate normal from Normal Map
vec3 getNormalFromMap() {
    vec3 tangentNormal = texture(normalMap, inTexCoord).xyz * 2.0 - 1.0;

    vec3 Q1  = dFdx(inWorldPos);
    vec3 Q2  = dFdy(inWorldPos);
    vec2 st1 = dFdx(inTexCoord);
    vec2 st2 = dFdy(inTexCoord);

    vec3 N  = normalize(inNormal);
    vec3 T  = normalize(Q1*st2.t - Q2*st1.t);
    vec3 B  = -normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);

    return normalize(TBN * tangentNormal);
}

// Fresnel Schlick
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Distribution GGX
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return a2 / max(denom, 0.0000001);
}

// Geometry Schlick-GGX
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float denom = NdotV * (1.0 - k) + k;
    return NdotV / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

void main() {
    vec3 albedo = texture(albedoMap, inTexCoord).rgb;
    albedo = pow(albedo, vec3(2.2)); // convert to linear space

    float metallic = texture(metallicMap, inTexCoord).r;
    float roughness = texture(roughnessMap, inTexCoord).r;

    vec3 N = getNormalFromMap();
    vec3 V = normalize(uboLights.viewPos - inWorldPos);

    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, metallic);

    vec3 Lo = vec3(0.0);

    // Light calculation for a directional light
    vec3 L = normalize(uboLights.directionalLight.position.xyz - inWorldPos);
    vec3 H = normalize(V + L);
    float distance = length(uboLights.directionalLight.position.xyz - inWorldPos);
    float attenuation = 1.0; 
    vec3 radiance = uboLights.directionalLight.color.rgb * attenuation;

    float NDF = DistributionGGX(N, H, roughness);   
    float G   = GeometrySmith(N, V, L, roughness);      
    vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);
       
    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001; 
    vec3 specular     = numerator / denominator;
    
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;	  

    float NdotL = max(dot(N, L), 0.0);        
    Lo += (kD * albedo / PI + specular) * radiance * NdotL;

    vec3 ambient = vec3(0.03) * albedo * uboLights.directionalLight.color.a;
    vec3 color = ambient + Lo;

    // Forward+ clustered lighting
    uint tileX = uint(gl_FragCoord.x) / 16;
    uint tileY = uint(gl_FragCoord.y) / 16;
    
    // Simplistic Z-slice mapping based on linear depth approximation
    // In a full implementation, calculate linearDepth / zNear and standard clustering formula
    uint tileZ = uint(clamp(gl_FragCoord.z * float(uboLights.gridSizeZ), 0.0, float(uboLights.gridSizeZ - 1.0)));
    
    uint clusterIndex = tileX + (tileY * uboLights.gridSizeX) + (tileZ * uboLights.gridSizeX * uboLights.gridSizeY);

    uint maxClusters = uboLights.gridSizeX * uboLights.gridSizeY * uboLights.gridSizeZ;
    if (clusterIndex < maxClusters) {
        LightGrid grid = lightGrid[clusterIndex];
        
        for (uint i = 0; i < grid.count; ++i) {
            uint lightIndex = lightIndices[grid.offset + i];
            PointLight pLight = pointLights[lightIndex];
            
            vec3 L_p = pLight.positionAndRadius.xyz - inWorldPos;
            float dist = length(L_p);
            float radius = pLight.positionAndRadius.w;
            
            // Basic attenuation
            float attenuation_p = 1.0 / (dist * dist + 1.0);
            
            if (dist < radius) {
                L_p = normalize(L_p);
                vec3 H_p = normalize(V + L_p);
                
                float NDF_p = DistributionGGX(N, H_p, roughness);   
                float G_p   = GeometrySmith(N, V, L_p, roughness);      
                vec3 F_p    = fresnelSchlick(max(dot(H_p, V), 0.0), F0);
                   
                vec3 nominator_p    = NDF_p * G_p * F_p;
                float denominator_p = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L_p), 0.0) + 0.0001; 
                vec3 specular_p     = nominator_p / denominator_p;
                
                vec3 kS_p = F_p;
                vec3 kD_p = vec3(1.0) - kS_p;
                kD_p *= 1.0 - metallic;	  
            
                float NdotL_p = max(dot(N, L_p), 0.0);        
                
                vec3 radiance_p = pLight.colorAndIntensity.rgb * pLight.colorAndIntensity.w * attenuation_p;
                
                color += (kD_p * albedo / PI + specular_p) * radiance_p * NdotL_p;
            }
        }
    }

    // HDR tonemapping
    color = color / (color + vec3(1.0));
    // gamma correction
    color = pow(color, vec3(1.0/2.2)); 

    outColor = vec4(color, 1.0);
}
