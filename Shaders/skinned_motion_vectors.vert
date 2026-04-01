#version 450

// Skinned Motion Vector Generation Vertex Shader
// Transforms skinned vertices to both current and previous clip space for TAA

//=============================================================================
// Vertex Inputs (matches SkinnedVertex layout)
//=============================================================================

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in uvec4 inBoneIndices;
layout(location = 5) in vec4 inBoneWeights;

//=============================================================================
// Uniform Buffers
//=============================================================================

layout(set = 0, binding = 0, std140) uniform MotionVectorUBO {
    mat4 currentModel;
    mat4 currentViewProjection;
    mat4 previousModel;
    mat4 previousViewProjection;
    vec2 jitterOffset;
    vec2 prevJitterOffset;
} ubo;

//=============================================================================
// Storage Buffers (Bone Matrices - Current and Previous Frame)
//=============================================================================

// Current frame bone matrices
layout(std430, set = 1, binding = 0) readonly buffer CurrentBoneMatrixBuffer {
    mat4 currentBoneMatrices[];
};

// Previous frame bone matrices (for motion blur/TAA)
layout(std430, set = 1, binding = 1) readonly buffer PreviousBoneMatrixBuffer {
    mat4 previousBoneMatrices[];
};

//=============================================================================
// Push Constants
//=============================================================================

layout(push_constant) uniform PushConstants {
    uint currentBoneOffset;
    uint previousBoneOffset;
    uint flags;          // Bit 0: has previous frame data
    uint padding;
} push;

//=============================================================================
// Outputs
//=============================================================================

layout(location = 0) out vec4 outCurrentClipPos;
layout(location = 1) out vec4 outPreviousClipPos;
layout(location = 2) out vec3 outWorldPos;

//=============================================================================
// Skinning Functions
//=============================================================================

mat4 computeCurrentSkinMatrix() {
    uint offset = push.currentBoneOffset;
    
    mat4 skinMatrix = mat4(0.0);
    skinMatrix += currentBoneMatrices[offset + inBoneIndices.x] * inBoneWeights.x;
    skinMatrix += currentBoneMatrices[offset + inBoneIndices.y] * inBoneWeights.y;
    skinMatrix += currentBoneMatrices[offset + inBoneIndices.z] * inBoneWeights.z;
    skinMatrix += currentBoneMatrices[offset + inBoneIndices.w] * inBoneWeights.w;
    
    return skinMatrix;
}

mat4 computePreviousSkinMatrix() {
    // If no previous frame data, use current
    if ((push.flags & 1u) == 0u) {
        return computeCurrentSkinMatrix();
    }
    
    uint offset = push.previousBoneOffset;
    
    mat4 skinMatrix = mat4(0.0);
    skinMatrix += previousBoneMatrices[offset + inBoneIndices.x] * inBoneWeights.x;
    skinMatrix += previousBoneMatrices[offset + inBoneIndices.y] * inBoneWeights.y;
    skinMatrix += previousBoneMatrices[offset + inBoneIndices.z] * inBoneWeights.z;
    skinMatrix += previousBoneMatrices[offset + inBoneIndices.w] * inBoneWeights.w;
    
    return skinMatrix;
}

//=============================================================================
// Main
//=============================================================================

void main() {
    // Current frame skinned position
    mat4 currentSkinMatrix = computeCurrentSkinMatrix();
    vec4 currentSkinnedPos = currentSkinMatrix * vec4(inPosition, 1.0);
    vec4 worldPos = ubo.currentModel * currentSkinnedPos;
    vec4 clipPos = ubo.currentViewProjection * worldPos;
    
    // Apply TAA jitter
    clipPos.xy += ubo.jitterOffset * clipPos.w;
    
    gl_Position = clipPos;
    outCurrentClipPos = clipPos;
    outWorldPos = worldPos.xyz;
    
    // Previous frame skinned position (for motion vectors)
    mat4 previousSkinMatrix = computePreviousSkinMatrix();
    vec4 prevSkinnedPos = previousSkinMatrix * vec4(inPosition, 1.0);
    vec4 prevWorldPos = ubo.previousModel * prevSkinnedPos;
    vec4 prevClipPos = ubo.previousViewProjection * prevWorldPos;
    prevClipPos.xy += ubo.prevJitterOffset * prevClipPos.w;
    
    outPreviousClipPos = prevClipPos;
}
