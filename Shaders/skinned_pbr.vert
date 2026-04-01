#version 450

// Skinned PBR Vertex Shader
// Performs GPU skinning directly in the vertex shader using SSBOs
// Alternative to compute shader skinning - better for simpler pipelines

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

layout(set = 0, binding = 0, std140) uniform TransformUBO {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 modelViewProj;
    vec4 cameraPos;
} ubo;

//=============================================================================
// Storage Buffers (Bone Matrices)
//=============================================================================

// Bone matrices SSBO - contains final skinning matrices
// Each matrix = globalBoneTransform * inverseBindMatrix
layout(std430, set = 1, binding = 0) readonly buffer BoneMatrixBuffer {
    mat4 boneMatrices[];
};

// Optional: Per-instance bone matrix offsets for instanced skeletal rendering
layout(std430, set = 1, binding = 1) readonly buffer BoneOffsetBuffer {
    uint boneOffsets[];  // Offset into boneMatrices for each instance
};

//=============================================================================
// Push Constants (for per-draw data)
//=============================================================================

layout(push_constant) uniform PushConstants {
    uint boneOffset;     // Offset into bone matrix buffer
    uint instanceId;     // For instanced rendering
    uint flags;          // Bit 0: use bone offsets buffer
    uint padding;
} pc;

//=============================================================================
// Vertex Outputs
//=============================================================================

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragTangent;
layout(location = 4) out vec3 fragBitangent;
layout(location = 5) out vec4 fragPosLightSpace;  // For shadow mapping

//=============================================================================
// Skinning Functions
//=============================================================================

mat4 computeSkinMatrix() {
    // Determine bone matrix offset
    uint offset = pc.boneOffset;
    if ((pc.flags & 1u) != 0u) {
        offset = boneOffsets[pc.instanceId];
    }
    
    // Compute weighted sum of bone matrices
    mat4 skinMatrix = mat4(0.0);
    
    skinMatrix += boneMatrices[offset + inBoneIndices.x] * inBoneWeights.x;
    skinMatrix += boneMatrices[offset + inBoneIndices.y] * inBoneWeights.y;
    skinMatrix += boneMatrices[offset + inBoneIndices.z] * inBoneWeights.z;
    skinMatrix += boneMatrices[offset + inBoneIndices.w] * inBoneWeights.w;
    
    return skinMatrix;
}

//=============================================================================
// Main Vertex Shader
//=============================================================================

void main() {
    // Compute skinning matrix from bone weights
    mat4 skinMatrix = computeSkinMatrix();
    
    // Transform position through skinning, then model transform
    vec4 skinnedPosition = skinMatrix * vec4(inPosition, 1.0);
    vec4 worldPos = ubo.model * skinnedPosition;
    
    // Final clip-space position
    gl_Position = ubo.proj * ubo.view * worldPos;
    
    // Pass world position to fragment shader
    fragPos = worldPos.xyz;
    fragTexCoord = inTexCoord;
    
    // Transform normal through skinning matrix (using upper 3x3)
    // For model matrix, use inverse-transpose for non-uniform scaling
    mat3 skinNormalMatrix = mat3(skinMatrix);
    mat3 modelNormalMatrix = transpose(inverse(mat3(ubo.model)));
    mat3 normalMatrix = modelNormalMatrix * skinNormalMatrix;
    
    // Transform normal and tangent
    fragNormal = normalize(normalMatrix * inNormal);
    fragTangent = normalize(normalMatrix * inTangent.xyz);
    
    // Compute bitangent (tangent.w contains handedness)
    fragBitangent = cross(fragNormal, fragTangent) * inTangent.w;
    
    // Light space position for shadow mapping (if shadow matrix is provided)
    // Note: In a full implementation, this would use a separate shadow UBO
    fragPosLightSpace = vec4(0.0);  // Placeholder
}
