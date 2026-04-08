// RT GI Closest Hit Shader
#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) rayPayloadInEXT vec4 giPayload;
hitAttributeEXT vec2 attribs;

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

layout(std430, set = 2, binding = 2) readonly buffer Vertices {
    float vertexData[];
};

layout(std430, set = 2, binding = 3) readonly buffer Indices {
    uint indices[];
};

struct InstanceData {
    uint materialIndex;
    uint vertexOffset;
    uint indexOffset;
    uint padding;
};

layout(std430, set = 2, binding = 4) readonly buffer Instances {
    InstanceData instanceData[];
};

vec2 getVertexUV(uint index, uint offset) {
    uint base = (offset + index) * 8 + 6;
    return vec2(vertexData[base], vertexData[base + 1]);
}

void main() {
    InstanceData instance = instanceData[gl_InstanceCustomIndexEXT];
    MaterialData mat = materials[instance.materialIndex];
    
    uint i0 = indices[instance.indexOffset + gl_PrimitiveID * 3 + 0];
    uint i1 = indices[instance.indexOffset + gl_PrimitiveID * 3 + 1];
    uint i2 = indices[instance.indexOffset + gl_PrimitiveID * 3 + 2];
    
    vec3 bary = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    vec2 uv = getVertexUV(i0, instance.vertexOffset) * bary.x +
              getVertexUV(i1, instance.vertexOffset) * bary.y +
              getVertexUV(i2, instance.vertexOffset) * bary.z;
    
    vec3 albedo = mat.albedo.rgb;
    if (mat.albedoTexture >= 0) {
        albedo *= texture(textures[nonuniformEXT(mat.albedoTexture)], uv).rgb;
    }
    
    float emissive = mat.metallicRoughnessEmissive.b;
    if (mat.emissiveTexture >= 0) {
        emissive *= texture(textures[nonuniformEXT(mat.emissiveTexture)], uv).r;
    }
    
    giPayload = vec4(albedo + albedo * emissive, gl_HitTEXT);
}
