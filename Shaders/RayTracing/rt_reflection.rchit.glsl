// RT Reflection Closest Hit Shader
#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference2 : require

layout(location = 0) rayPayloadInEXT vec4 hitValue;
hitAttributeEXT vec2 attribs;

layout(set = 0, binding = 7) uniform samplerCube environmentMap;

// Material data
struct MaterialData {
    vec4 albedo;
    vec4 metallicRoughnessEmissive;
    int albedoTexture;
    int normalTexture;
    int metallicRoughnessTexture;
    int emissiveTexture;
};

layout(std430, set = 2, binding = 0) readonly buffer Materials {
    MaterialData materials[];
};

layout(set = 2, binding = 1) uniform sampler2D textures[];

// Geometry data
struct Vertex {
    vec3 position;
    vec3 normal;
    vec2 uv;
};

layout(std430, set = 2, binding = 2) readonly buffer Vertices {
    float vertexData[];
};

layout(std430, set = 2, binding = 3) readonly buffer Indices {
    uint indices[];
};

// Instance data
struct InstanceData {
    uint materialIndex;
    uint vertexOffset;
    uint indexOffset;
    uint padding;
};

layout(std430, set = 2, binding = 4) readonly buffer Instances {
    InstanceData instanceData[];
};

Vertex getVertex(uint index, uint offset) {
    uint base = (offset + index) * 8;
    Vertex v;
    v.position = vec3(vertexData[base], vertexData[base + 1], vertexData[base + 2]);
    v.normal = vec3(vertexData[base + 3], vertexData[base + 4], vertexData[base + 5]);
    v.uv = vec2(vertexData[base + 6], vertexData[base + 7]);
    return v;
}

void main() {
    InstanceData instance = instanceData[gl_InstanceCustomIndexEXT];
    MaterialData mat = materials[instance.materialIndex];
    
    // Get triangle vertices
    uint i0 = indices[instance.indexOffset + gl_PrimitiveID * 3 + 0];
    uint i1 = indices[instance.indexOffset + gl_PrimitiveID * 3 + 1];
    uint i2 = indices[instance.indexOffset + gl_PrimitiveID * 3 + 2];
    
    Vertex v0 = getVertex(i0, instance.vertexOffset);
    Vertex v1 = getVertex(i1, instance.vertexOffset);
    Vertex v2 = getVertex(i2, instance.vertexOffset);
    
    // Barycentric interpolation
    vec3 bary = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    
    vec2 uv = v0.uv * bary.x + v1.uv * bary.y + v2.uv * bary.z;
    vec3 normal = normalize(v0.normal * bary.x + v1.normal * bary.y + v2.normal * bary.z);
    
    // Transform normal to world space
    normal = normalize(gl_ObjectToWorldEXT * vec4(normal, 0.0));
    
    // Sample albedo
    vec3 albedo = mat.albedo.rgb;
    if (mat.albedoTexture >= 0) {
        albedo *= texture(textures[nonuniformEXT(mat.albedoTexture)], uv).rgb;
    }
    
    // Add emissive contribution
    float emissive = mat.metallicRoughnessEmissive.b;
    if (mat.emissiveTexture >= 0) {
        emissive *= texture(textures[nonuniformEXT(mat.emissiveTexture)], uv).r;
    }
    
    // Simple shading (ambient + emissive)
    vec3 color = albedo * 0.3 + albedo * emissive;
    
    hitValue = vec4(color, 1.0);
}
