#version 450

// Skinned Shadow Vertex Shader
// Renders skeletal meshes to shadow maps with GPU skinning via SSBO

//=============================================================================
// Vertex Inputs (matches SkinnedVertex layout)
//=============================================================================

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;      // Not used for shadow, but keeps layout consistent
layout(location = 2) in vec2 inTexCoord;    // Not used for shadow
layout(location = 3) in vec4 inTangent;     // Not used for shadow
layout(location = 4) in uvec4 inBoneIndices;
layout(location = 5) in vec4 inBoneWeights;

//=============================================================================
// Uniform Buffers
//=============================================================================

layout(set = 0, binding = 0, std140) uniform ViewProjUBO {
    mat4 lightSpaceMatrix;
} ubo;

//=============================================================================
// Storage Buffers (Bone Matrices)
//=============================================================================

layout(std430, set = 1, binding = 0) readonly buffer BoneMatrixBuffer {
    mat4 boneMatrices[];
};

//=============================================================================
// Push Constants
//=============================================================================

layout(push_constant) uniform PushConstants {
    mat4 modelMatrix;
    uint boneOffset;
    uint padding0;
    uint padding1;
    uint padding2;
} push;

//=============================================================================
// Skinning
//=============================================================================

mat4 computeSkinMatrix() {
    uint offset = push.boneOffset;
    
    mat4 skinMatrix = mat4(0.0);
    skinMatrix += boneMatrices[offset + inBoneIndices.x] * inBoneWeights.x;
    skinMatrix += boneMatrices[offset + inBoneIndices.y] * inBoneWeights.y;
    skinMatrix += boneMatrices[offset + inBoneIndices.z] * inBoneWeights.z;
    skinMatrix += boneMatrices[offset + inBoneIndices.w] * inBoneWeights.w;
    
    return skinMatrix;
}

//=============================================================================
// Main
//=============================================================================

void main() {
    mat4 skinMatrix = computeSkinMatrix();
    vec4 skinnedPosition = skinMatrix * vec4(inPosition, 1.0);
    gl_Position = ubo.lightSpaceMatrix * push.modelMatrix * skinnedPosition;
}
