#pragma once

#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include "Core/Math/Math.h"
#include "Core/RHI/RHIBuffer.h"

namespace Core {
namespace Renderer {

    // ============================================================================
    // Vertex Structures
    // ============================================================================

    // Standard vertex for static meshes
    struct Vertex {
        Math::Vec3 position;
        Math::Vec3 normal;
        Math::Vec2 texCoord;
        Math::Vec4 tangent;
    };

    // Maximum number of bone influences per vertex
    constexpr uint32_t MAX_BONE_INFLUENCES = 4;

    // Maximum bones per skeleton (matches typical GPU limits)
    constexpr uint32_t MAX_BONES = 256;

    // Skinned vertex for skeletal meshes (extends Vertex with bone data)
    struct SkinnedVertex {
        Math::Vec3 position;
        Math::Vec3 normal;
        Math::Vec2 texCoord;
        Math::Vec4 tangent;
        Math::UVec4 boneIndices;   // Up to 4 bone influences (indices into bone array)
        Math::Vec4 boneWeights;    // Weights for each bone influence (should sum to 1.0)

        SkinnedVertex() 
            : position(0.0f), normal(0.0f, 1.0f, 0.0f), texCoord(0.0f), tangent(0.0f)
            , boneIndices(0), boneWeights(0.0f) {}

        // Normalize bone weights to ensure they sum to 1.0
        void NormalizeBoneWeights() {
            float sum = boneWeights.x + boneWeights.y + boneWeights.z + boneWeights.w;
            if (sum > 0.0f) {
                boneWeights /= sum;
            } else {
                // Default to first bone with full weight if no weights set
                boneWeights = Math::Vec4(1.0f, 0.0f, 0.0f, 0.0f);
            }
        }
    };

    // ============================================================================
    // Skeletal Animation Structures
    // ============================================================================

    // Single bone in the skeleton hierarchy
    struct Bone {
        std::string Name;
        int32_t ParentIndex = -1;      // -1 for root bones
        Math::Mat4 InverseBindMatrix;  // Transforms from mesh space to bone space
        Math::Mat4 LocalTransform;     // Local transform relative to parent
        std::string RoleTag;           // Optional semantic role (hip, foot_l, hand_r, etc.)
        
        // Children indices (for traversal)
        std::vector<int32_t> ChildrenIndices;
    };

    // Complete skeleton definition
    struct Skeleton {
        std::string Name;
        std::vector<Bone> Bones;
        
        // Maps bone name to index for fast lookup
        std::unordered_map<std::string, int32_t> BoneNameToIndex;
        std::unordered_map<std::string, int32_t> RoleToBoneIndex;
        
        // Root bone indices (bones with no parent)
        std::vector<int32_t> RootBoneIndices;

        // Find bone index by name, returns -1 if not found
        int32_t FindBoneIndex(const std::string& name) const {
            auto it = BoneNameToIndex.find(name);
            return (it != BoneNameToIndex.end()) ? it->second : -1;
        }

        int32_t FindBoneByRole(const std::string& roleTag) const {
            auto it = RoleToBoneIndex.find(roleTag);
            return (it != RoleToBoneIndex.end()) ? it->second : -1;
        }

        // Get total number of bones
        uint32_t GetBoneCount() const { return static_cast<uint32_t>(Bones.size()); }

        // Check if skeleton is valid (has at least one bone)
        bool IsValid() const { return !Bones.empty(); }
    };

    // ============================================================================
    // Animation Keyframe Structures
    // ============================================================================

    // Interpolation modes for animation
    enum class AnimationInterpolation : uint8_t {
        Linear = 0,
        Step,
        CubicSpline
    };

    // Animation channel target
    enum class AnimationTargetPath : uint8_t {
        Translation = 0,
        Rotation,
        Scale,
        Weights  // For morph targets
    };

    // Single keyframe for translation/scale (Vec3)
    struct Vec3Keyframe {
        float Time;
        Math::Vec3 Value;
        // For cubic spline interpolation
        Math::Vec3 InTangent;
        Math::Vec3 OutTangent;
    };

    // Single keyframe for rotation (Quaternion)
    struct QuatKeyframe {
        float Time;
        Math::Quat Value;
        // For cubic spline interpolation
        Math::Quat InTangent;
        Math::Quat OutTangent;
    };

    // Animation channel - animates a single property of a single bone
    struct AnimationChannel {
        int32_t BoneIndex = -1;        // Target bone in skeleton
        AnimationTargetPath TargetPath;
        AnimationInterpolation Interpolation = AnimationInterpolation::Linear;
        
        // Keyframe data (only one type is used based on TargetPath)
        std::vector<Vec3Keyframe> Vec3Keyframes;   // For Translation/Scale
        std::vector<QuatKeyframe> QuatKeyframes;   // For Rotation
    };

    // Complete animation clip
    struct AnimationClip {
        std::string Name;
        float Duration = 0.0f;         // Total duration in seconds
        float TicksPerSecond = 30.0f;  // Sampling rate
        std::vector<AnimationChannel> Channels;
        bool Loop = true;

        // Find channel for a specific bone and target
        const AnimationChannel* FindChannel(int32_t boneIndex, AnimationTargetPath target) const {
            for (const auto& channel : Channels) {
                if (channel.BoneIndex == boneIndex && channel.TargetPath == target) {
                    return &channel;
                }
            }
            return nullptr;
        }
    };

    // ============================================================================
    // Mesh Primitive
    // ============================================================================

    struct Primitive {
        uint32_t firstIndex;
        uint32_t indexCount;
        uint32_t materialIndex;
    };

    struct VirtualGeometryAssociation {
        bool Enabled = false;
        bool Clusterized = false;
        bool FallbackActive = false;
        bool PagesResident = true;
        uint64_t MetadataKey = 0;
        uint32_t ClusterCount = 0;
        uint32_t PageCount = 0;
    };

    // ============================================================================
    // Mesh Class
    // ============================================================================

    class Mesh {
    public:
        Mesh();
        ~Mesh();

        // Load static mesh from GLTF
        bool LoadGLTF(const std::string& filepath);

        // Load skeletal mesh with skeleton and animations from GLTF
        bool LoadSkeletalGLTF(const std::string& filepath);

        // Check if this is a skeletal mesh
        bool IsSkeletal() const { return m_IsSkeletal && m_Skeleton.IsValid(); }

        // Get skeleton (only valid for skeletal meshes)
        const Skeleton& GetSkeleton() const { return m_Skeleton; }
        Skeleton& GetSkeleton() { return m_Skeleton; }

        // Get animations
        const std::vector<AnimationClip>& GetAnimations() const { return m_Animations; }
        
        // Find animation by name
        const AnimationClip* FindAnimation(const std::string& name) const {
            for (const auto& anim : m_Animations) {
                if (anim.Name == name) return &anim;
            }
            return nullptr;
        }

        // Static mesh data
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<Primitive> primitives;

        // Skinned mesh data (only populated for skeletal meshes)
        std::vector<SkinnedVertex> skinnedVertices;

        // GPU buffers
        std::shared_ptr<RHI::RHIBuffer> vertexBuffer;
        std::shared_ptr<RHI::RHIBuffer> indexBuffer;
        std::shared_ptr<RHI::RHIBuffer> boneBuffer;  // For bone matrices

        // Virtual geometry metadata/hooks (Phase 24)
        void SetVirtualGeometryAssociation(
            uint64_t metadataKey,
            uint32_t clusterCount,
            uint32_t pageCount,
            bool clusterized,
            bool fallbackActive);
        void SetVirtualGeometryPagesResident(bool pagesResident);
        void SetVirtualGeometryFallbackActive(bool fallbackActive);
        bool HasVirtualGeometry() const { return m_VirtualGeometry.Enabled; }
        bool IsVirtualGeometryClusterized() const { return m_VirtualGeometry.Clusterized; }
        bool AreVirtualGeometryPagesResident() const { return m_VirtualGeometry.PagesResident; }
        bool UsesVirtualGeometryFallback() const { return m_VirtualGeometry.FallbackActive; }
        const VirtualGeometryAssociation& GetVirtualGeometryAssociation() const { return m_VirtualGeometry; }

    private:
        bool m_IsSkeletal = false;
        Skeleton m_Skeleton;
        std::vector<AnimationClip> m_Animations;
        VirtualGeometryAssociation m_VirtualGeometry;

        // Internal GLTF loading helpers
        bool LoadSkeletonFromGLTF(void* gltfData);
        bool LoadAnimationsFromGLTF(void* gltfData);
        void BuildBoneHierarchy();
    };

    // ============================================================================
    // Utility Functions
    // ============================================================================

    // Convert interpolation enum to string
    inline const char* AnimationInterpolationToString(AnimationInterpolation interp) {
        switch (interp) {
            case AnimationInterpolation::Linear: return "Linear";
            case AnimationInterpolation::Step: return "Step";
            case AnimationInterpolation::CubicSpline: return "CubicSpline";
            default: return "Unknown";
        }
    }

    // Convert target path enum to string
    inline const char* AnimationTargetPathToString(AnimationTargetPath path) {
        switch (path) {
            case AnimationTargetPath::Translation: return "Translation";
            case AnimationTargetPath::Rotation: return "Rotation";
            case AnimationTargetPath::Scale: return "Scale";
            case AnimationTargetPath::Weights: return "Weights";
            default: return "Unknown";
        }
    }

    std::vector<float> ExtractMotionFeatureVector(
        const AnimationClip& clip,
        float sampleTime,
        uint32_t featureDimension = 8);

} // namespace Renderer
} // namespace Core
