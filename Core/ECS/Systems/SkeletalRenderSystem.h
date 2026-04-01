#pragma once

// SkeletalRenderSystem.h
// ECS system for rendering skeletal meshes with GPU skinning

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/SkeletalMeshComponent.h"
#include "Core/ECS/ParallelECS.h"
#include "Core/Renderer/GPUSkinning.h"
#include "Core/Renderer/Mesh.h"
#include "Core/Profile.h"
#include "Core/Log.h"
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>

namespace Core {
namespace ECS {

    //=========================================================================
    // Skeletal Draw Command
    //=========================================================================

    struct SkeletalDrawCommand {
        Math::Mat4 Transform;                    // World transform
        const Renderer::Mesh* Mesh;              // Skeletal mesh pointer
        uint32_t MaterialIndex;                  // Material slot
        uint32_t SkinningInstanceId;             // ID for GPU skinning system
        uint32_t BoneCount;                      // Number of bones
        bool CastShadows;                        // Shadow casting flag
        bool UseComputeSkinning;                 // Compute vs vertex shader skinning
    };

    //=========================================================================
    // Animation Update Result (for parallel processing)
    //=========================================================================

    struct AnimationUpdateResult {
        entt::entity Entity;
        uint32_t SkinningInstanceId;
        std::vector<Math::Mat4> SkinningMatrices;
    };

    //=========================================================================
    // Skeletal Render System
    //=========================================================================

    class SkeletalRenderSystem : public ParallelSystemBase {
    public:
        SkeletalRenderSystem();
        ~SkeletalRenderSystem();

        //---------------------------------------------------------------------
        // Initialization
        //---------------------------------------------------------------------

        void Initialize(std::shared_ptr<Renderer::GPUSkinningSystem> skinningSystem);
        void Shutdown();

        //---------------------------------------------------------------------
        // Update Methods
        //---------------------------------------------------------------------

        // Update animation and collect draw commands (sequential)
        void Update(Scene& scene, float deltaTime);

        // Parallel update for large scenes
        void UpdateParallel(Scene& scene, float deltaTime);

        // Update animations only (separate from rendering)
        void UpdateAnimations(Scene& scene, float deltaTime);

        // Collect draw commands only (after animation update)
        void CollectDrawCommands(Scene& scene);

        //---------------------------------------------------------------------
        // Draw Command Access
        //---------------------------------------------------------------------

        const std::vector<SkeletalDrawCommand>& GetDrawCommands() const { 
            return m_DrawCommands; 
        }

        void ClearDrawCommands() { m_DrawCommands.clear(); }

        //---------------------------------------------------------------------
        // Instance Management
        //---------------------------------------------------------------------

        // Register entity with GPU skinning system
        uint32_t RegisterEntity(Scene& scene, entt::entity entity);

        // Unregister entity from GPU skinning system
        void UnregisterEntity(entt::entity entity);

        // Get skinning instance ID for entity
        uint32_t GetSkinningInstanceId(entt::entity entity) const;

        //---------------------------------------------------------------------
        // Statistics
        //---------------------------------------------------------------------

        uint32_t GetAnimatedEntityCount() const { return m_AnimatedEntityCount; }
        uint32_t GetTotalSkeletalEntityCount() const { return m_TotalEntityCount; }
        uint32_t GetTotalBoneCount() const { return m_TotalBoneCount; }

        //---------------------------------------------------------------------
        // GPU Skinning Access
        //---------------------------------------------------------------------

        std::shared_ptr<Renderer::GPUSkinningSystem> GetSkinningSystem() const {
            return m_SkinningSystem;
        }

    private:
        // Animation evaluation
        void EvaluateAnimation(SkeletalMeshComponent& skeletal, float deltaTime);

        // Compute global bone poses from local poses
        void ComputeGlobalPoses(const Renderer::Skeleton& skeleton,
                               SkeletonPose& pose);

        // Compute skinning matrices from global poses
        void ComputeSkinningMatrices(const Renderer::Skeleton& skeleton,
                                    SkeletonPose& pose);

        // Sample animation at current time
        void SampleAnimation(const Renderer::AnimationClip& clip,
                           float time,
                           const Renderer::Skeleton& skeleton,
                           SkeletonPose& pose);

        // Interpolate keyframes
        Math::Vec3 InterpolateVec3(const std::vector<Renderer::Vec3Keyframe>& keyframes,
                                   float time,
                                   Renderer::AnimationInterpolation interp);

        Math::Quat InterpolateQuat(const std::vector<Renderer::QuatKeyframe>& keyframes,
                                   float time,
                                   Renderer::AnimationInterpolation interp);

    private:
        std::shared_ptr<Renderer::GPUSkinningSystem> m_SkinningSystem;
        std::vector<SkeletalDrawCommand> m_DrawCommands;
        
        // Entity to skinning instance ID mapping
        std::unordered_map<entt::entity, uint32_t> m_EntityToInstanceId;
        
        // Statistics
        std::atomic<uint32_t> m_AnimatedEntityCount{0};
        std::atomic<uint32_t> m_TotalEntityCount{0};
        std::atomic<uint32_t> m_TotalBoneCount{0};

        // Thread-local buffers for parallel animation updates
        ThreadLocalScratch<std::vector<AnimationUpdateResult>> m_ThreadLocalResults;
        std::mutex m_DrawCommandsMutex;

        bool m_Initialized = false;
    };

} // namespace ECS
} // namespace Core
