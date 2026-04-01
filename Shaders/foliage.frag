#version 450

// Foliage Fragment Shader
// Alpha-tested vegetation with simple lighting

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec4 fragColor;
layout(location = 4) in float fragDistanceToCamera;

layout(location = 0) out vec4 outColor;

layout(binding = 2) uniform sampler2D albedoTexture;
layout(binding = 3) uniform sampler2D normalTexture;

layout(std140, binding = 1) uniform RenderUniforms {
    mat4 view;
    mat4 projection;
    vec4 cameraPosition;
    vec4 lightDirection;    // xyz=dir, w=intensity
    vec4 lightColor;        // xyz=color, w=ambient
    vec4 windDirection;
    float windStrength;
    float windFrequency;
    float alphaCutoff;
    float padding;
};

// Distance fade parameters
const float FADE_START = 80.0;
const float FADE_END = 100.0;

void main() {
    // Sample albedo texture
    vec4 albedo = texture(albedoTexture, fragTexCoord);
    
    // Alpha test
    if (albedo.a < alphaCutoff) {
        discard;
    }
    
    // Apply instance color tint
    vec3 color = albedo.rgb * fragColor.rgb;
    
    // Simple Lambert lighting
    vec3 normal = normalize(fragNormal);
    vec3 lightDir = normalize(-lightDirection.xyz);
    
    // Two-sided lighting for foliage
    float NdotL = abs(dot(normal, lightDir));
    
    // Ambient + diffuse
    float ambient = lightColor.w;
    float diffuse = NdotL * lightDirection.w;
    
    vec3 lighting = lightColor.rgb * (ambient + diffuse);
    color *= lighting;
    
    // Subsurface scattering approximation (leaves are translucent)
    float backLight = max(0.0, dot(-normal, lightDir)) * 0.3;
    color += albedo.rgb * lightColor.rgb * backLight * vec3(0.2, 0.4, 0.1);
    
    // Distance fade
    float fadeFactor = 1.0 - smoothstep(FADE_START, FADE_END, fragDistanceToCamera);
    
    // Output with distance-based alpha fade
    float finalAlpha = albedo.a * fragColor.a * fadeFactor;
    
    // Discard if faded out
    if (finalAlpha < 0.01) {
        discard;
    }
    
    outColor = vec4(color, finalAlpha);
}
