#version 450

// Foliage Vertex Shader
// Instanced rendering with wind animation

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec4 fragColor;
layout(location = 4) out float fragDistanceToCamera;

// Instance data (matches FoliageInstance struct)
struct FoliageInstance {
    mat4 transform;
    vec4 color;
    vec4 windParams;    // xy=direction, z=strength, w=phase
    uint meshIndex;
    uint materialIndex;
    float scale;
    float padding;
};

layout(std430, binding = 0) readonly buffer InstanceBuffer {
    FoliageInstance instances[];
};

layout(std140, binding = 1) uniform RenderUniforms {
    mat4 view;
    mat4 projection;
    vec4 cameraPosition;    // xyz=pos, w=time
    vec4 lightDirection;    // xyz=dir, w=intensity
    vec4 lightColor;        // xyz=color, w=ambient
    vec4 windDirection;     // xyz=dir, w=time
    float windStrength;
    float windFrequency;
    float alphaCutoff;
    float padding;
};

// Wind animation function
vec3 calculateWind(vec3 worldPos, float height, vec4 instanceWindParams) {
    float time = cameraPosition.w;
    float phase = instanceWindParams.w;
    
    // Primary wave
    float wave1 = sin(time * windFrequency + phase + worldPos.x * 0.1) * 0.5 + 0.5;
    
    // Secondary wave for variation
    float wave2 = sin(time * windFrequency * 1.3 + phase * 1.7 + worldPos.z * 0.15) * 0.3;
    
    // Combine waves
    float windEffect = (wave1 + wave2) * windStrength * instanceWindParams.z;
    
    // Scale effect by height (more at top)
    windEffect *= height;
    
    // Apply in wind direction
    vec3 windDisplacement = windDirection.xyz * windEffect;
    
    return windDisplacement;
}

void main() {
    FoliageInstance instance = instances[gl_InstanceIndex];
    
    // Get world position from instance transform
    vec4 worldPos = instance.transform * vec4(inPosition, 1.0);
    
    // Apply wind animation (based on local height in model)
    float localHeight = max(0.0, inPosition.y);
    vec3 windOffset = calculateWind(worldPos.xyz, localHeight, instance.windParams);
    worldPos.xyz += windOffset;
    
    // Transform to clip space
    vec4 viewPos = view * worldPos;
    gl_Position = projection * viewPos;
    
    // Pass to fragment shader
    fragWorldPos = worldPos.xyz;
    
    // Transform normal (approximate for wind-affected geometry)
    mat3 normalMatrix = mat3(instance.transform);
    fragNormal = normalize(normalMatrix * inNormal);
    
    fragTexCoord = inTexCoord;
    fragColor = instance.color;
    fragDistanceToCamera = length(cameraPosition.xyz - worldPos.xyz);
}
