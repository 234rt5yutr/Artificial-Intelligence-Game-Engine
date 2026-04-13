#include "Mesh.h"
#include "Core/Log.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <algorithm>
#include <cstring>

namespace Core {
namespace Renderer {

Mesh::Mesh() = default;
Mesh::~Mesh() = default;

void Mesh::SetVirtualGeometryAssociation(
    const uint64_t metadataKey,
    const uint32_t clusterCount,
    const uint32_t pageCount,
    const bool clusterized,
    const bool fallbackActive) {
    m_VirtualGeometry.Enabled = true;
    m_VirtualGeometry.MetadataKey = metadataKey;
    m_VirtualGeometry.ClusterCount = clusterCount;
    m_VirtualGeometry.PageCount = pageCount;
    m_VirtualGeometry.Clusterized = clusterized;
    m_VirtualGeometry.FallbackActive = fallbackActive;
    m_VirtualGeometry.PagesResident = !clusterized;
}

void Mesh::SetVirtualGeometryPagesResident(const bool pagesResident) {
    m_VirtualGeometry.PagesResident = pagesResident;
}

void Mesh::SetVirtualGeometryFallbackActive(const bool fallbackActive) {
    m_VirtualGeometry.FallbackActive = fallbackActive;
}

// ============================================================================
// Static Mesh Loading
// ============================================================================

bool Mesh::LoadGLTF(const std::string& filepath) {
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, filepath.c_str(), &data);

    if (result != cgltf_result_success) {
        ENGINE_CORE_ERROR("Failed to parse GLTF file: {}", filepath);
        return false;
    }

    result = cgltf_load_buffers(&options, data, filepath.c_str());
    if (result != cgltf_result_success) {
        ENGINE_CORE_ERROR("Failed to load buffers for GLTF file: {}", filepath);
        cgltf_free(data);
        return false;
    }

    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0;

    for (cgltf_size i = 0; i < data->meshes_count; ++i) {
        const cgltf_mesh& mesh = data->meshes[i];
        
        for (cgltf_size j = 0; j < mesh.primitives_count; ++j) {
            const cgltf_primitive& primitive = mesh.primitives[j];
            
            // Only support triangles
            if (primitive.type != cgltf_primitive_type_triangles) continue;

            uint32_t firstIndex = indexOffset;
            uint32_t indexCount = 0;

            // Load Indices
            if (primitive.indices) {
                cgltf_accessor* accessor = primitive.indices;
                indexCount = (uint32_t)accessor->count;
                
                for (cgltf_size k = 0; k < accessor->count; ++k) {
                    uint32_t index = (uint32_t)cgltf_accessor_read_index(accessor, k);
                    indices.push_back(index + vertexOffset);
                }
                indexOffset += indexCount;
            }

            // Determine vertex count from the first attribute
            uint32_t vertexCount = 0;
            if (primitive.attributes_count > 0) {
                vertexCount = (uint32_t)primitive.attributes[0].data->count;
            }

            uint32_t primitiveVertexOffset = (uint32_t)vertices.size();
            vertices.resize(vertices.size() + vertexCount);

            // Load Attributes
            for (cgltf_size k = 0; k < primitive.attributes_count; ++k) {
                const cgltf_attribute& attribute = primitive.attributes[k];
                cgltf_accessor* accessor = attribute.data;

                for (cgltf_size v = 0; v < accessor->count; ++v) {
                    Vertex& vertex = vertices[primitiveVertexOffset + v];
                    float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                    
                    cgltf_accessor_read_float(accessor, v, values, 4);

                    if (attribute.type == cgltf_attribute_type_position) {
                        vertex.position = Math::Vec3(values[0], values[1], values[2]);
                    } else if (attribute.type == cgltf_attribute_type_normal) {
                        vertex.normal = Math::Vec3(values[0], values[1], values[2]);
                    } else if (attribute.type == cgltf_attribute_type_texcoord) {
                        vertex.texCoord = Math::Vec2(values[0], values[1]);
                    } else if (attribute.type == cgltf_attribute_type_tangent) {
                        vertex.tangent = Math::Vec4(values[0], values[1], values[2], values[3]);
                    }
                }
            }
            vertexOffset += vertexCount;

            // Register primitive
            Primitive p;
            p.firstIndex = firstIndex;
            p.indexCount = indexCount;
            
            // Map material index
            p.materialIndex = 0; // Default
            if (primitive.material) {
                p.materialIndex = static_cast<uint32_t>(primitive.material - data->materials);
            }
            
            primitives.push_back(p);
        }
    }

    cgltf_free(data);
    ENGINE_CORE_INFO("Successfully loaded GLTF mesh: {0} ({1} vertices, {2} indices, {3} primitives)", 
                     filepath, vertices.size(), indices.size(), primitives.size());
    return true;
}

// ============================================================================
// Skeletal Mesh Loading
// ============================================================================

bool Mesh::LoadSkeletalGLTF(const std::string& filepath) {
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, filepath.c_str(), &data);

    if (result != cgltf_result_success) {
        ENGINE_CORE_ERROR("Failed to parse GLTF file: {}", filepath);
        return false;
    }

    result = cgltf_load_buffers(&options, data, filepath.c_str());
    if (result != cgltf_result_success) {
        ENGINE_CORE_ERROR("Failed to load buffers for GLTF file: {}", filepath);
        cgltf_free(data);
        return false;
    }

    // Check if this file has skins (skeletal data)
    bool hasSkin = data->skins_count > 0;
    
    if (!hasSkin) {
        ENGINE_CORE_WARN("GLTF file has no skeleton data, loading as static mesh: {}", filepath);
        cgltf_free(data);
        return LoadGLTF(filepath);
    }

    // Load skeleton first
    if (!LoadSkeletonFromGLTF(data)) {
        ENGINE_CORE_ERROR("Failed to load skeleton from GLTF file: {}", filepath);
        cgltf_free(data);
        return false;
    }

    // Load animations
    LoadAnimationsFromGLTF(data);

    // Load mesh data with bone weights
    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0;

    for (cgltf_size i = 0; i < data->meshes_count; ++i) {
        const cgltf_mesh& mesh = data->meshes[i];
        
        for (cgltf_size j = 0; j < mesh.primitives_count; ++j) {
            const cgltf_primitive& primitive = mesh.primitives[j];
            
            if (primitive.type != cgltf_primitive_type_triangles) continue;

            uint32_t firstIndex = indexOffset;
            uint32_t indexCount = 0;

            // Load Indices
            if (primitive.indices) {
                cgltf_accessor* accessor = primitive.indices;
                indexCount = (uint32_t)accessor->count;
                
                for (cgltf_size k = 0; k < accessor->count; ++k) {
                    uint32_t index = (uint32_t)cgltf_accessor_read_index(accessor, k);
                    indices.push_back(index + vertexOffset);
                }
                indexOffset += indexCount;
            }

            // Determine vertex count
            uint32_t vertexCount = 0;
            if (primitive.attributes_count > 0) {
                vertexCount = (uint32_t)primitive.attributes[0].data->count;
            }

            uint32_t primitiveVertexOffset = (uint32_t)skinnedVertices.size();
            skinnedVertices.resize(skinnedVertices.size() + vertexCount);

            // Initialize all vertices with default values
            for (uint32_t v = 0; v < vertexCount; ++v) {
                skinnedVertices[primitiveVertexOffset + v] = SkinnedVertex();
            }

            // Load Attributes
            for (cgltf_size k = 0; k < primitive.attributes_count; ++k) {
                const cgltf_attribute& attribute = primitive.attributes[k];
                cgltf_accessor* accessor = attribute.data;

                for (cgltf_size v = 0; v < accessor->count; ++v) {
                    SkinnedVertex& vertex = skinnedVertices[primitiveVertexOffset + v];

                    if (attribute.type == cgltf_attribute_type_position) {
                        float values[3];
                        cgltf_accessor_read_float(accessor, v, values, 3);
                        vertex.position = Math::Vec3(values[0], values[1], values[2]);
                    } 
                    else if (attribute.type == cgltf_attribute_type_normal) {
                        float values[3];
                        cgltf_accessor_read_float(accessor, v, values, 3);
                        vertex.normal = Math::Vec3(values[0], values[1], values[2]);
                    } 
                    else if (attribute.type == cgltf_attribute_type_texcoord) {
                        float values[2];
                        cgltf_accessor_read_float(accessor, v, values, 2);
                        vertex.texCoord = Math::Vec2(values[0], values[1]);
                    } 
                    else if (attribute.type == cgltf_attribute_type_tangent) {
                        float values[4];
                        cgltf_accessor_read_float(accessor, v, values, 4);
                        vertex.tangent = Math::Vec4(values[0], values[1], values[2], values[3]);
                    }
                    else if (attribute.type == cgltf_attribute_type_joints) {
                        // Bone indices - can be uint8 or uint16 in GLTF
                        cgltf_uint jointIndices[4] = {0, 0, 0, 0};
                        cgltf_accessor_read_uint(accessor, v, jointIndices, 4);
                        vertex.boneIndices = Math::UVec4(jointIndices[0], jointIndices[1], jointIndices[2], jointIndices[3]);
                    }
                    else if (attribute.type == cgltf_attribute_type_weights) {
                        // Bone weights
                        float weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                        cgltf_accessor_read_float(accessor, v, weights, 4);
                        vertex.boneWeights = Math::Vec4(weights[0], weights[1], weights[2], weights[3]);
                    }
                }
            }

            // Normalize bone weights for all vertices in this primitive
            for (uint32_t v = 0; v < vertexCount; ++v) {
                skinnedVertices[primitiveVertexOffset + v].NormalizeBoneWeights();
            }

            vertexOffset += vertexCount;

            // Register primitive
            Primitive p;
            p.firstIndex = firstIndex;
            p.indexCount = indexCount;
            p.materialIndex = 0;
            if (primitive.material) {
                p.materialIndex = static_cast<uint32_t>(primitive.material - data->materials);
            }
            primitives.push_back(p);
        }
    }

    m_IsSkeletal = true;
    cgltf_free(data);
    
    ENGINE_CORE_INFO("Successfully loaded skeletal GLTF: {} ({} vertices, {} indices, {} bones, {} animations)", 
                     filepath, skinnedVertices.size(), indices.size(), 
                     m_Skeleton.GetBoneCount(), m_Animations.size());
    return true;
}

// ============================================================================
// Skeleton Loading
// ============================================================================

bool Mesh::LoadSkeletonFromGLTF(void* gltfData) {
    cgltf_data* data = static_cast<cgltf_data*>(gltfData);
    
    if (data->skins_count == 0) {
        ENGINE_CORE_WARN("No skins found in GLTF file");
        return false;
    }

    // Use the first skin
    const cgltf_skin& skin = data->skins[0];
    
    if (skin.name) {
        m_Skeleton.Name = skin.name;
    } else {
        m_Skeleton.Name = "DefaultSkeleton";
    }

    // Reserve space for bones
    m_Skeleton.Bones.resize(skin.joints_count);

    // Load inverse bind matrices
    std::vector<Math::Mat4> inverseBindMatrices(skin.joints_count, Math::Mat4(1.0f));
    if (skin.inverse_bind_matrices) {
        for (cgltf_size i = 0; i < skin.joints_count; ++i) {
            float matrix[16];
            cgltf_accessor_read_float(skin.inverse_bind_matrices, i, matrix, 16);
            // cgltf stores matrices in column-major order (same as glm)
            inverseBindMatrices[i] = glm::make_mat4(matrix);
        }
    }

    // Create node to bone index mapping
    std::unordered_map<const cgltf_node*, int32_t> nodeToBoneIndex;
    for (cgltf_size i = 0; i < skin.joints_count; ++i) {
        nodeToBoneIndex[skin.joints[i]] = static_cast<int32_t>(i);
    }

    // Process each joint
    for (cgltf_size i = 0; i < skin.joints_count; ++i) {
        const cgltf_node* jointNode = skin.joints[i];
        Bone& bone = m_Skeleton.Bones[i];

        // Set bone name
        if (jointNode->name) {
            bone.Name = jointNode->name;
        } else {
            bone.Name = "Bone_" + std::to_string(i);
        }

        // Set inverse bind matrix
        bone.InverseBindMatrix = inverseBindMatrices[i];

        // Calculate local transform from node
        if (jointNode->has_matrix) {
            bone.LocalTransform = glm::make_mat4(jointNode->matrix);
        } else {
            // Build from TRS
            Math::Vec3 translation(0.0f);
            Math::Quat rotation = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f); // Identity
            Math::Vec3 scale(1.0f);

            if (jointNode->has_translation) {
                translation = Math::Vec3(jointNode->translation[0], 
                                         jointNode->translation[1], 
                                         jointNode->translation[2]);
            }
            if (jointNode->has_rotation) {
                rotation = Math::Quat(jointNode->rotation[3],  // w
                                      jointNode->rotation[0],  // x
                                      jointNode->rotation[1],  // y
                                      jointNode->rotation[2]); // z
            }
            if (jointNode->has_scale) {
                scale = Math::Vec3(jointNode->scale[0], 
                                   jointNode->scale[1], 
                                   jointNode->scale[2]);
            }

            bone.LocalTransform = glm::translate(Math::Mat4(1.0f), translation) *
                                  glm::mat4_cast(rotation) *
                                  glm::scale(Math::Mat4(1.0f), scale);
        }

        // Find parent bone
        bone.ParentIndex = -1;
        if (jointNode->parent) {
            auto parentIt = nodeToBoneIndex.find(jointNode->parent);
            if (parentIt != nodeToBoneIndex.end()) {
                bone.ParentIndex = parentIt->second;
            }
        }

        // Add to name lookup
        m_Skeleton.BoneNameToIndex[bone.Name] = static_cast<int32_t>(i);
    }

    // Build hierarchy (children indices and root bones)
    BuildBoneHierarchy();

    ENGINE_CORE_INFO("Loaded skeleton '{}' with {} bones", m_Skeleton.Name, m_Skeleton.GetBoneCount());
    return true;
}

// ============================================================================
// Animation Loading
// ============================================================================

bool Mesh::LoadAnimationsFromGLTF(void* gltfData) {
    cgltf_data* data = static_cast<cgltf_data*>(gltfData);

    if (data->animations_count == 0) {
        ENGINE_CORE_INFO("No animations found in GLTF file");
        return true;
    }

    // Build node to bone index mapping using the first skin
    std::unordered_map<const cgltf_node*, int32_t> nodeToBoneIndex;
    if (data->skins_count > 0) {
        const cgltf_skin& skin = data->skins[0];
        for (cgltf_size i = 0; i < skin.joints_count; ++i) {
            nodeToBoneIndex[skin.joints[i]] = static_cast<int32_t>(i);
        }
    }

    m_Animations.reserve(data->animations_count);

    for (cgltf_size animIdx = 0; animIdx < data->animations_count; ++animIdx) {
        const cgltf_animation& gltfAnim = data->animations[animIdx];
        
        AnimationClip clip;
        if (gltfAnim.name) {
            clip.Name = gltfAnim.name;
        } else {
            clip.Name = "Animation_" + std::to_string(animIdx);
        }

        clip.Duration = 0.0f;

        // Process each channel
        for (cgltf_size chanIdx = 0; chanIdx < gltfAnim.channels_count; ++chanIdx) {
            const cgltf_animation_channel& gltfChannel = gltfAnim.channels[chanIdx];
            
            if (!gltfChannel.target_node) continue;

            // Find bone index
            auto boneIt = nodeToBoneIndex.find(gltfChannel.target_node);
            if (boneIt == nodeToBoneIndex.end()) continue;

            AnimationChannel channel;
            channel.BoneIndex = boneIt->second;

            // Determine target path
            switch (gltfChannel.target_path) {
                case cgltf_animation_path_type_translation:
                    channel.TargetPath = AnimationTargetPath::Translation;
                    break;
                case cgltf_animation_path_type_rotation:
                    channel.TargetPath = AnimationTargetPath::Rotation;
                    break;
                case cgltf_animation_path_type_scale:
                    channel.TargetPath = AnimationTargetPath::Scale;
                    break;
                case cgltf_animation_path_type_weights:
                    channel.TargetPath = AnimationTargetPath::Weights;
                    break;
                default:
                    continue;
            }

            // Get sampler
            const cgltf_animation_sampler* sampler = gltfChannel.sampler;
            if (!sampler || !sampler->input || !sampler->output) continue;

            // Determine interpolation
            switch (sampler->interpolation) {
                case cgltf_interpolation_type_linear:
                    channel.Interpolation = AnimationInterpolation::Linear;
                    break;
                case cgltf_interpolation_type_step:
                    channel.Interpolation = AnimationInterpolation::Step;
                    break;
                case cgltf_interpolation_type_cubic_spline:
                    channel.Interpolation = AnimationInterpolation::CubicSpline;
                    break;
            }

            // Read timestamps
            const cgltf_accessor* timeAccessor = sampler->input;
            const cgltf_accessor* valueAccessor = sampler->output;
            size_t keyframeCount = timeAccessor->count;

            std::vector<float> timestamps(keyframeCount);
            for (size_t k = 0; k < keyframeCount; ++k) {
                cgltf_accessor_read_float(timeAccessor, k, &timestamps[k], 1);
                clip.Duration = std::max(clip.Duration, timestamps[k]);
            }

            // Read values based on target path
            if (channel.TargetPath == AnimationTargetPath::Rotation) {
                // Quaternion keyframes
                channel.QuatKeyframes.resize(keyframeCount);
                
                bool isCubic = (channel.Interpolation == AnimationInterpolation::CubicSpline);
                (void)isCubic; // Used in condition below
                
                for (size_t k = 0; k < keyframeCount; ++k) {
                    QuatKeyframe& kf = channel.QuatKeyframes[k];
                    kf.Time = timestamps[k];
                    
                    if (isCubic) {
                        float inTangent[4], value[4], outTangent[4];
                        cgltf_accessor_read_float(valueAccessor, k * 3, inTangent, 4);
                        cgltf_accessor_read_float(valueAccessor, k * 3 + 1, value, 4);
                        cgltf_accessor_read_float(valueAccessor, k * 3 + 2, outTangent, 4);
                        
                        kf.InTangent = Math::Quat(inTangent[3], inTangent[0], inTangent[1], inTangent[2]);
                        kf.Value = Math::Quat(value[3], value[0], value[1], value[2]);
                        kf.OutTangent = Math::Quat(outTangent[3], outTangent[0], outTangent[1], outTangent[2]);
                    } else {
                        float value[4];
                        cgltf_accessor_read_float(valueAccessor, k, value, 4);
                        kf.Value = Math::Quat(value[3], value[0], value[1], value[2]);
                    }
                }
            } else {
                // Vec3 keyframes (translation/scale)
                channel.Vec3Keyframes.resize(keyframeCount);
                
                bool isCubic = (channel.Interpolation == AnimationInterpolation::CubicSpline);
                
                for (size_t k = 0; k < keyframeCount; ++k) {
                    Vec3Keyframe& kf = channel.Vec3Keyframes[k];
                    kf.Time = timestamps[k];
                    
                    if (isCubic) {
                        float inTangent[3], value[3], outTangent[3];
                        cgltf_accessor_read_float(valueAccessor, k * 3, inTangent, 3);
                        cgltf_accessor_read_float(valueAccessor, k * 3 + 1, value, 3);
                        cgltf_accessor_read_float(valueAccessor, k * 3 + 2, outTangent, 3);
                        
                        kf.InTangent = Math::Vec3(inTangent[0], inTangent[1], inTangent[2]);
                        kf.Value = Math::Vec3(value[0], value[1], value[2]);
                        kf.OutTangent = Math::Vec3(outTangent[0], outTangent[1], outTangent[2]);
                    } else {
                        float value[3];
                        cgltf_accessor_read_float(valueAccessor, k, value, 3);
                        kf.Value = Math::Vec3(value[0], value[1], value[2]);
                    }
                }
            }

            clip.Channels.push_back(std::move(channel));
        }

        if (!clip.Channels.empty()) {
            ENGINE_CORE_INFO("  Loaded animation '{}': {} channels, {:.2f}s duration", 
                           clip.Name, clip.Channels.size(), clip.Duration);
            m_Animations.push_back(std::move(clip));
        }
    }

    return true;
}

// ============================================================================
// Helper Functions
// ============================================================================

void Mesh::BuildBoneHierarchy() {
    // Clear existing hierarchy data
    m_Skeleton.RootBoneIndices.clear();
    for (auto& bone : m_Skeleton.Bones) {
        bone.ChildrenIndices.clear();
    }

    // Build children lists and find roots
    for (int32_t i = 0; i < static_cast<int32_t>(m_Skeleton.Bones.size()); ++i) {
        const Bone& bone = m_Skeleton.Bones[i];
        
        if (bone.ParentIndex >= 0 && bone.ParentIndex < static_cast<int32_t>(m_Skeleton.Bones.size())) {
            m_Skeleton.Bones[bone.ParentIndex].ChildrenIndices.push_back(i);
        } else {
            m_Skeleton.RootBoneIndices.push_back(i);
        }
    }

    ENGINE_CORE_TRACE("Skeleton hierarchy built: {} root bones", m_Skeleton.RootBoneIndices.size());
}

} // namespace Renderer
} // namespace Core
