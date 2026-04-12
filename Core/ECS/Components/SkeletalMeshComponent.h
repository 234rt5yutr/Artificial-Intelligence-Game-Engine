#pragma once

// SkeletalMeshComponent
// ECS component for entities with skeletal animation support
// Contains mesh data, skeleton, animations, and runtime pose information

#include "Core/Renderer/Mesh.h"
#include "Core/Math/Math.h"
#include <glm/gtx/matrix_decompose.hpp>
#include <memory>
#include <string>
#include <vector>
#include <array>
#include <cstdint>

namespace Core {
namespace ECS {

    // ============================================================================
    // Bone Pose - Runtime state of a single bone
    // ============================================================================

    struct BonePose {
        Math::Vec3 Translation{0.0f};
        Math::Quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};  // Identity quaternion
        Math::Vec3 Scale{1.0f};

        // Compute the local transform matrix
        Math::Mat4 ToMatrix() const {
            return glm::translate(Math::Mat4(1.0f), Translation) *
                   glm::mat4_cast(Rotation) *
                   glm::scale(Math::Mat4(1.0f), Scale);
        }

        // Linear interpolation between two poses
        static BonePose Lerp(const BonePose& a, const BonePose& b, float t) {
            BonePose result;
            result.Translation = glm::mix(a.Translation, b.Translation, t);
            result.Rotation = glm::slerp(a.Rotation, b.Rotation, t);
            result.Scale = glm::mix(a.Scale, b.Scale, t);
            return result;
        }
    };

    // ============================================================================
    // Skeleton Pose - Complete pose of all bones
    // ============================================================================

    struct SkeletonPose {
        std::vector<BonePose> LocalPoses;      // Local space poses (relative to parent)
        std::vector<Math::Mat4> GlobalPoses;   // World space transforms
        std::vector<Math::Mat4> SkinningMatrices; // Final matrices for GPU skinning

        void Resize(size_t boneCount) {
            LocalPoses.resize(boneCount);
            GlobalPoses.resize(boneCount, Math::Mat4(1.0f));
            SkinningMatrices.resize(boneCount, Math::Mat4(1.0f));
        }

        size_t GetBoneCount() const { return LocalPoses.size(); }
        bool IsValid() const { return !LocalPoses.empty(); }
    };

    // ============================================================================
    // Animation Playback State
    // ============================================================================

    enum class AnimationPlaybackState : uint8_t {
        Stopped = 0,
        Playing,
        Paused
    };

    // ============================================================================
    // Animation Instance - Active animation on a skeletal mesh
    // ============================================================================

    struct AnimationInstance {
        std::string AnimationName;
        const Renderer::AnimationClip* Clip = nullptr;
        
        float CurrentTime = 0.0f;
        float PlaybackSpeed = 1.0f;
        float BlendWeight = 1.0f;        // For animation blending
        
        bool Loop = true;
        AnimationPlaybackState State = AnimationPlaybackState::Stopped;

        bool IsPlaying() const { return State == AnimationPlaybackState::Playing; }
        bool IsStopped() const { return State == AnimationPlaybackState::Stopped; }
        bool IsPaused() const { return State == AnimationPlaybackState::Paused; }

        void Reset() {
            CurrentTime = 0.0f;
            State = AnimationPlaybackState::Stopped;
        }
    };

    // ============================================================================
    // Skeletal Mesh Component
    // ============================================================================

    struct SkeletalMeshComponent {
        // Mesh data (includes skeleton and animations)
        std::shared_ptr<Renderer::Mesh> MeshData;

        // Runtime pose data
        SkeletonPose CurrentPose;
        SkeletonPose BindPose;           // Reference pose from the skeleton

        // Active animations (supports blending multiple)
        std::vector<AnimationInstance> ActiveAnimations;
        static constexpr size_t MAX_BLEND_ANIMATIONS = 4;

        // Material properties
        uint32_t MaterialIndex = 0;

        // Rendering flags
        bool Visible = true;
        bool CastShadows = true;
        bool ReceiveShadows = true;

        // Animation settings
        bool AutoUpdate = true;          // Update animation in system
        bool RootMotionEnabled = false;  // Apply root bone motion to transform

        // Debug flags
        bool ShowSkeleton = false;       // Render skeleton for debugging
        bool ShowBoneNames = false;

        // Mesh path for serialization
        std::string MeshPath;

        // Hot-reload generation tracking (Stage 23)
        uint64_t AssetGeneration = 0;
        uint64_t LastBoundGeneration = 0;
        uint64_t AnimationClipGeneration = 0;
        uint64_t LastAnimationClipGeneration = 0;

        // ========================================================================
        // Constructors
        // ========================================================================

        SkeletalMeshComponent() = default;

        SkeletalMeshComponent(std::shared_ptr<Renderer::Mesh> mesh)
            : MeshData(mesh) 
        {
            InitializeFromMesh();
        }

        SkeletalMeshComponent(std::shared_ptr<Renderer::Mesh> mesh, const std::string& path)
            : MeshData(mesh), MeshPath(path)
        {
            InitializeFromMesh();
        }

        // ========================================================================
        // Validation
        // ========================================================================

        bool IsValid() const { 
            return MeshData != nullptr && MeshData->IsSkeletal(); 
        }

        bool HasSkeleton() const {
            return IsValid() && MeshData->GetSkeleton().IsValid();
        }

        uint32_t GetBoneCount() const {
            return HasSkeleton() ? MeshData->GetSkeleton().GetBoneCount() : 0;
        }

        // ========================================================================
        // Animation Control
        // ========================================================================

        // Play an animation by name
        bool PlayAnimation(const std::string& name, bool loop = true, float blendTime = 0.0f) {
            if (!IsValid()) return false;

            const auto* clip = MeshData->FindAnimation(name);
            if (!clip) return false;

            // Check if already playing
            for (auto& anim : ActiveAnimations) {
                if (anim.AnimationName == name) {
                    anim.State = AnimationPlaybackState::Playing;
                    anim.Loop = loop;
                    return true;
                }
            }

            // Add new animation instance
            if (blendTime > 0.0f && !ActiveAnimations.empty()) {
                // Blend in new animation
                AnimationInstance instance;
                instance.AnimationName = name;
                instance.Clip = clip;
                instance.Loop = loop;
                instance.BlendWeight = 0.0f;
                instance.State = AnimationPlaybackState::Playing;
                ActiveAnimations.push_back(instance);
            } else {
                // Replace all with new animation
                ActiveAnimations.clear();
                AnimationInstance instance;
                instance.AnimationName = name;
                instance.Clip = clip;
                instance.Loop = loop;
                instance.BlendWeight = 1.0f;
                instance.State = AnimationPlaybackState::Playing;
                ActiveAnimations.push_back(instance);
            }

            return true;
        }

        // Stop animation by name
        void StopAnimation(const std::string& name) {
            for (auto& anim : ActiveAnimations) {
                if (anim.AnimationName == name) {
                    anim.State = AnimationPlaybackState::Stopped;
                    anim.CurrentTime = 0.0f;
                }
            }
        }

        // Stop all animations
        void StopAllAnimations() {
            for (auto& anim : ActiveAnimations) {
                anim.State = AnimationPlaybackState::Stopped;
                anim.CurrentTime = 0.0f;
            }
        }

        // Pause animation
        void PauseAnimation(const std::string& name) {
            for (auto& anim : ActiveAnimations) {
                if (anim.AnimationName == name) {
                    anim.State = AnimationPlaybackState::Paused;
                }
            }
        }

        // Resume animation
        void ResumeAnimation(const std::string& name) {
            for (auto& anim : ActiveAnimations) {
                if (anim.AnimationName == name && anim.State == AnimationPlaybackState::Paused) {
                    anim.State = AnimationPlaybackState::Playing;
                }
            }
        }

        // Get animation names available on this mesh
        std::vector<std::string> GetAnimationNames() const {
            std::vector<std::string> names;
            if (!IsValid()) return names;

            const auto& animations = MeshData->GetAnimations();
            names.reserve(animations.size());
            for (const auto& anim : animations) {
                names.push_back(anim.Name);
            }
            return names;
        }

        // ========================================================================
        // Bone Access
        // ========================================================================

        // Get bone index by name
        int32_t GetBoneIndex(const std::string& name) const {
            if (!HasSkeleton()) return -1;
            return MeshData->GetSkeleton().FindBoneIndex(name);
        }

        // Get bone name by index
        const std::string& GetBoneName(int32_t index) const {
            static const std::string empty;
            if (!HasSkeleton() || index < 0 || index >= static_cast<int32_t>(GetBoneCount())) {
                return empty;
            }
            return MeshData->GetSkeleton().Bones[index].Name;
        }

        // Get current bone transform in model space
        Math::Mat4 GetBoneTransform(int32_t index) const {
            if (index < 0 || index >= static_cast<int32_t>(CurrentPose.GetBoneCount())) {
                return Math::Mat4(1.0f);
            }
            return CurrentPose.GlobalPoses[index];
        }

        // Set bone local pose (for procedural animation/IK)
        void SetBoneLocalPose(int32_t index, const BonePose& pose) {
            if (index >= 0 && index < static_cast<int32_t>(CurrentPose.GetBoneCount())) {
                CurrentPose.LocalPoses[index] = pose;
            }
        }

        // ========================================================================
        // Factory Methods
        // ========================================================================

        // Create from a loaded skeletal mesh
        static SkeletalMeshComponent Create(std::shared_ptr<Renderer::Mesh> mesh, 
                                            const std::string& path = "") {
            SkeletalMeshComponent comp;
            comp.MeshData = mesh;
            comp.MeshPath = path;
            comp.InitializeFromMesh();
            return comp;
        }

    private:
        // Initialize pose data from mesh skeleton
        void InitializeFromMesh() {
            if (!IsValid() || !HasSkeleton()) return;

            const auto& skeleton = MeshData->GetSkeleton();
            uint32_t boneCount = skeleton.GetBoneCount();

            // Initialize poses
            CurrentPose.Resize(boneCount);
            BindPose.Resize(boneCount);

            // Set bind pose from skeleton
            for (uint32_t i = 0; i < boneCount; ++i) {
                const auto& bone = skeleton.Bones[i];
                
                // Decompose local transform to TRS
                Math::Vec3 scale, translation, skew;
                Math::Vec4 perspective;
                Math::Quat rotation;
                
                glm::decompose(bone.LocalTransform, scale, rotation, translation, skew, perspective);

                BonePose pose;
                pose.Translation = translation;
                pose.Rotation = rotation;
                pose.Scale = scale;

                BindPose.LocalPoses[i] = pose;
                CurrentPose.LocalPoses[i] = pose;
            }
        }
    };

} // namespace ECS
} // namespace Core
