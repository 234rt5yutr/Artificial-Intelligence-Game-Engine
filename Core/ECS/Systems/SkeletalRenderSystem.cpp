#include "SkeletalRenderSystem.h"
#include "Core/JobSystem/JobSystem.h"

namespace Core {
namespace ECS {

//=============================================================================
// Constructor / Destructor
//=============================================================================

SkeletalRenderSystem::SkeletalRenderSystem() = default;

SkeletalRenderSystem::~SkeletalRenderSystem() {
    if (m_Initialized) {
        Shutdown();
    }
}

//=============================================================================
// Initialization
//=============================================================================

void SkeletalRenderSystem::Initialize(
    std::shared_ptr<Renderer::GPUSkinningSystem> skinningSystem) {
    
    if (m_Initialized) {
        ENGINE_CORE_WARN("SkeletalRenderSystem already initialized");
        return;
    }

    m_SkinningSystem = skinningSystem;
    m_Initialized = true;

    ENGINE_CORE_INFO("SkeletalRenderSystem initialized");
}

void SkeletalRenderSystem::Shutdown() {
    if (!m_Initialized) {
        return;
    }

    m_EntityToInstanceId.clear();
    m_DrawCommands.clear();
    m_SkinningSystem.reset();
    m_Initialized = false;

    ENGINE_CORE_INFO("SkeletalRenderSystem shutdown");
}

//=============================================================================
// Update Methods
//=============================================================================

void SkeletalRenderSystem::Update(Scene& scene, float deltaTime) {
    PROFILE_FUNCTION();

    m_DrawCommands.clear();
    m_AnimatedEntityCount = 0;
    m_TotalEntityCount = 0;
    m_TotalBoneCount = 0;

    auto view = scene.View<TransformComponent, SkeletalMeshComponent>();
    m_DrawCommands.reserve(view.size_hint());

    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& skeletal = view.get<SkeletalMeshComponent>(entity);

        m_TotalEntityCount++;

        // Skip invalid or invisible meshes
        if (!skeletal.IsValid() || !skeletal.Visible) {
            continue;
        }

        if (skeletal.AssetGeneration != skeletal.LastBoundGeneration) {
            const uint32_t existingInstanceId = GetSkinningInstanceId(entity);
            if (existingInstanceId != 0) {
                UnregisterEntity(entity);
            }
            skeletal.LastBoundGeneration = skeletal.AssetGeneration;
        }
        if (skeletal.AnimationClipGeneration != skeletal.LastAnimationClipGeneration) {
            skeletal.LastAnimationClipGeneration = skeletal.AnimationClipGeneration;
        }

        // Ensure entity is registered with skinning system
        uint32_t instanceId = GetSkinningInstanceId(entity);
        if (instanceId == 0) {
            instanceId = RegisterEntity(scene, entity);
        }

        // Update animation if auto-update enabled
        if (skeletal.AutoUpdate && !skeletal.GraphRuntimeAuthoritative) {
            EvaluateAnimation(skeletal, deltaTime);
            m_AnimatedEntityCount++;
        }

        // Update skinning matrices in GPU system
        if (m_SkinningSystem && instanceId != 0) {
            m_SkinningSystem->UpdateFromComponent(instanceId, skeletal);
        }

        m_TotalBoneCount += skeletal.GetBoneCount();

        // Create draw command
        SkeletalDrawCommand cmd{};
        cmd.Transform = transform.WorldMatrix;
        cmd.Mesh = skeletal.MeshData.get();
        cmd.MaterialIndex = skeletal.MaterialIndex;
        cmd.SkinningInstanceId = instanceId;
        cmd.BoneCount = skeletal.GetBoneCount();
        cmd.CastShadows = skeletal.CastShadows;
        cmd.UseComputeSkinning = (m_SkinningSystem && 
            m_SkinningSystem->GetSkinningMode() == Renderer::SkinningMode::ComputeShader);

        m_DrawCommands.push_back(cmd);
    }

    // Upload bone matrices to GPU
    if (m_SkinningSystem) {
        m_SkinningSystem->UploadBoneMatrices();
    }

    ENGINE_CORE_TRACE("SkeletalRenderSystem: {} animated / {} total entities, {} bones",
                      m_AnimatedEntityCount.load(), m_TotalEntityCount.load(),
                      m_TotalBoneCount.load());
}

void SkeletalRenderSystem::UpdateParallel(Scene& scene, float deltaTime) {
    PROFILE_FUNCTION();

    m_DrawCommands.clear();
    m_AnimatedEntityCount = 0;
    m_TotalEntityCount = 0;
    m_TotalBoneCount = 0;
    m_ThreadLocalResults.Reset();

    auto& registry = scene.GetRegistry();
    auto view = registry.view<TransformComponent, SkeletalMeshComponent>();

    // Collect entities for parallel processing
    std::vector<entt::entity> entities;
    entities.reserve(view.size_hint());
    for (auto entity : view) {
        entities.push_back(entity);
    }

    if (entities.empty()) {
        return;
    }

    uint32_t entityCount = static_cast<uint32_t>(entities.size());
    m_TotalEntityCount = entityCount;

    // Fall back to sequential for small counts
    if (!ShouldRunParallel(entityCount)) {
        Update(scene, deltaTime);
        return;
    }

    // Phase 1: Parallel animation evaluation
    JobSystem::Context animCtx;
    JobSystem::Dispatch(animCtx, entityCount, GetBatchSize(), 
        [&](uint32_t index) {
            entt::entity entity = entities[index];
            auto& skeletal = registry.get<SkeletalMeshComponent>(entity);

            if (!skeletal.IsValid() || !skeletal.Visible) {
                return;
            }

            // Evaluate animation (this modifies skeletal.CurrentPose)
            if (skeletal.AutoUpdate && !skeletal.GraphRuntimeAuthoritative) {
                EvaluateAnimation(skeletal, deltaTime);
                m_AnimatedEntityCount.fetch_add(1, std::memory_order_relaxed);
            }

            m_TotalBoneCount.fetch_add(skeletal.GetBoneCount(), std::memory_order_relaxed);
        });
    JobSystem::Wait(animCtx);

    // Phase 2: Sequential draw command collection and GPU update
    // (GPU buffer updates are not thread-safe)
    for (auto entity : entities) {
        auto& transform = registry.get<TransformComponent>(entity);
        auto& skeletal = registry.get<SkeletalMeshComponent>(entity);

        if (!skeletal.IsValid() || !skeletal.Visible) {
            continue;
        }

        if (skeletal.AssetGeneration != skeletal.LastBoundGeneration) {
            const uint32_t existingInstanceId = GetSkinningInstanceId(entity);
            if (existingInstanceId != 0) {
                UnregisterEntity(entity);
            }
            skeletal.LastBoundGeneration = skeletal.AssetGeneration;
        }
        if (skeletal.AnimationClipGeneration != skeletal.LastAnimationClipGeneration) {
            skeletal.LastAnimationClipGeneration = skeletal.AnimationClipGeneration;
        }

        uint32_t instanceId = GetSkinningInstanceId(entity);
        if (instanceId == 0) {
            instanceId = RegisterEntity(scene, entity);
        }

        // Update skinning system
        if (m_SkinningSystem && instanceId != 0) {
            m_SkinningSystem->UpdateFromComponent(instanceId, skeletal);
        }

        // Create draw command
        SkeletalDrawCommand cmd{};
        cmd.Transform = transform.WorldMatrix;
        cmd.Mesh = skeletal.MeshData.get();
        cmd.MaterialIndex = skeletal.MaterialIndex;
        cmd.SkinningInstanceId = instanceId;
        cmd.BoneCount = skeletal.GetBoneCount();
        cmd.CastShadows = skeletal.CastShadows;
        cmd.UseComputeSkinning = (m_SkinningSystem && 
            m_SkinningSystem->GetSkinningMode() == Renderer::SkinningMode::ComputeShader);

        m_DrawCommands.push_back(cmd);
    }

    // Upload bone matrices
    if (m_SkinningSystem) {
        m_SkinningSystem->UploadBoneMatrices();
    }

    ENGINE_CORE_TRACE("SkeletalRenderSystem (parallel): {} animated / {} total, {} bones",
                      m_AnimatedEntityCount.load(), m_TotalEntityCount.load(),
                      m_TotalBoneCount.load());
}

void SkeletalRenderSystem::UpdateAnimations(Scene& scene, float deltaTime) {
    PROFILE_FUNCTION();

    auto view = scene.View<SkeletalMeshComponent>();

    for (auto entity : view) {
        auto& skeletal = view.get<SkeletalMeshComponent>(entity);

        if (!skeletal.IsValid() || !skeletal.AutoUpdate || skeletal.GraphRuntimeAuthoritative) {
            continue;
        }

        EvaluateAnimation(skeletal, deltaTime);
    }
}

void SkeletalRenderSystem::CollectDrawCommands(Scene& scene) {
    PROFILE_FUNCTION();

    m_DrawCommands.clear();

    auto view = scene.View<TransformComponent, SkeletalMeshComponent>();
    m_DrawCommands.reserve(view.size_hint());

    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& skeletal = view.get<SkeletalMeshComponent>(entity);

        if (!skeletal.IsValid() || !skeletal.Visible) {
            continue;
        }

        if (skeletal.AssetGeneration != skeletal.LastBoundGeneration) {
            const uint32_t existingInstanceId = GetSkinningInstanceId(entity);
            if (existingInstanceId != 0) {
                UnregisterEntity(entity);
            }
            skeletal.LastBoundGeneration = skeletal.AssetGeneration;
        }
        if (skeletal.AnimationClipGeneration != skeletal.LastAnimationClipGeneration) {
            skeletal.LastAnimationClipGeneration = skeletal.AnimationClipGeneration;
        }

        uint32_t instanceId = GetSkinningInstanceId(entity);

        SkeletalDrawCommand cmd{};
        cmd.Transform = transform.WorldMatrix;
        cmd.Mesh = skeletal.MeshData.get();
        cmd.MaterialIndex = skeletal.MaterialIndex;
        cmd.SkinningInstanceId = instanceId;
        cmd.BoneCount = skeletal.GetBoneCount();
        cmd.CastShadows = skeletal.CastShadows;
        cmd.UseComputeSkinning = (m_SkinningSystem && 
            m_SkinningSystem->GetSkinningMode() == Renderer::SkinningMode::ComputeShader);

        m_DrawCommands.push_back(cmd);
    }
}

//=============================================================================
// Instance Management
//=============================================================================

uint32_t SkeletalRenderSystem::RegisterEntity(Scene& scene, entt::entity entity) {
    if (!m_SkinningSystem) {
        return 0;
    }

    auto& registry = scene.GetRegistry();
    auto* skeletal = registry.try_get<SkeletalMeshComponent>(entity);
    if (!skeletal || !skeletal->IsValid()) {
        return 0;
    }

    uint32_t instanceId = m_SkinningSystem->RegisterSkeletalMesh(
        skeletal->MeshData.get(), skeletal->GetBoneCount());

    if (instanceId != 0) {
        m_EntityToInstanceId[entity] = instanceId;
    }

    return instanceId;
}

void SkeletalRenderSystem::UnregisterEntity(entt::entity entity) {
    auto it = m_EntityToInstanceId.find(entity);
    if (it == m_EntityToInstanceId.end()) {
        return;
    }

    if (m_SkinningSystem) {
        m_SkinningSystem->UnregisterSkeletalMesh(it->second);
    }

    m_EntityToInstanceId.erase(it);
}

uint32_t SkeletalRenderSystem::GetSkinningInstanceId(entt::entity entity) const {
    auto it = m_EntityToInstanceId.find(entity);
    return (it != m_EntityToInstanceId.end()) ? it->second : 0;
}

//=============================================================================
// Animation Evaluation
//=============================================================================

void SkeletalRenderSystem::EvaluateAnimation(SkeletalMeshComponent& skeletal, 
                                             float deltaTime) {
    if (!skeletal.HasSkeleton()) {
        return;
    }

    const auto& skeleton = skeletal.MeshData->GetSkeleton();
    auto& pose = skeletal.CurrentPose;

    // Process all active animations
    for (auto& anim : skeletal.ActiveAnimations) {
        if (!anim.IsPlaying() || !anim.Clip) {
            continue;
        }

        // Advance time
        anim.CurrentTime += deltaTime * anim.PlaybackSpeed;

        // Handle looping or end
        if (anim.CurrentTime >= anim.Clip->Duration) {
            if (anim.Loop) {
                anim.CurrentTime = std::fmod(anim.CurrentTime, anim.Clip->Duration);
            } else {
                anim.CurrentTime = anim.Clip->Duration;
                anim.State = AnimationPlaybackState::Stopped;
            }
        }

        // Sample animation into pose
        SampleAnimation(*anim.Clip, anim.CurrentTime, skeleton, pose);
    }

    // Compute global transforms from local poses
    ComputeGlobalPoses(skeleton, pose);

    // Compute final skinning matrices
    ComputeSkinningMatrices(skeleton, pose);
}

void SkeletalRenderSystem::ComputeGlobalPoses(const Renderer::Skeleton& skeleton,
                                              SkeletonPose& pose) {
    uint32_t boneCount = skeleton.GetBoneCount();

    // Process bones in order (parents before children due to how skeleton is stored)
    for (uint32_t i = 0; i < boneCount; ++i) {
        const auto& bone = skeleton.Bones[i];
        Math::Mat4 localMatrix = pose.LocalPoses[i].ToMatrix();

        if (bone.ParentIndex >= 0) {
            // Child bone: combine with parent's global transform
            pose.GlobalPoses[i] = pose.GlobalPoses[bone.ParentIndex] * localMatrix;
        } else {
            // Root bone: local = global
            pose.GlobalPoses[i] = localMatrix;
        }
    }
}

void SkeletalRenderSystem::ComputeSkinningMatrices(const Renderer::Skeleton& skeleton,
                                                   SkeletonPose& pose) {
    uint32_t boneCount = skeleton.GetBoneCount();

    for (uint32_t i = 0; i < boneCount; ++i) {
        // Skinning matrix = Global pose * Inverse bind matrix
        pose.SkinningMatrices[i] = pose.GlobalPoses[i] * skeleton.Bones[i].InverseBindMatrix;
    }
}

void SkeletalRenderSystem::SampleAnimation(const Renderer::AnimationClip& clip,
                                           float time,
                                           const Renderer::Skeleton& skeleton,
                                           SkeletonPose& pose) {
    // skeleton parameter reserved for future use (e.g., retargeting)
    (void)skeleton;
    
    // Sample each channel
    for (const auto& channel : clip.Channels) {
        if (channel.BoneIndex < 0 || 
            channel.BoneIndex >= static_cast<int32_t>(pose.LocalPoses.size())) {
            continue;
        }

        BonePose& bonePose = pose.LocalPoses[channel.BoneIndex];

        switch (channel.TargetPath) {
            case Renderer::AnimationTargetPath::Translation:
                bonePose.Translation = InterpolateVec3(channel.Vec3Keyframes, time, 
                                                       channel.Interpolation);
                break;

            case Renderer::AnimationTargetPath::Rotation:
                bonePose.Rotation = InterpolateQuat(channel.QuatKeyframes, time,
                                                    channel.Interpolation);
                break;

            case Renderer::AnimationTargetPath::Scale:
                bonePose.Scale = InterpolateVec3(channel.Vec3Keyframes, time,
                                                 channel.Interpolation);
                break;

            default:
                break;
        }
    }
}

Math::Vec3 SkeletalRenderSystem::InterpolateVec3(
    const std::vector<Renderer::Vec3Keyframe>& keyframes,
    float time,
    Renderer::AnimationInterpolation interp) {
    
    if (keyframes.empty()) {
        return Math::Vec3(0.0f);
    }

    if (keyframes.size() == 1 || time <= keyframes.front().Time) {
        return keyframes.front().Value;
    }

    if (time >= keyframes.back().Time) {
        return keyframes.back().Value;
    }

    // Find surrounding keyframes
    size_t nextIdx = 0;
    for (size_t i = 0; i < keyframes.size(); ++i) {
        if (keyframes[i].Time > time) {
            nextIdx = i;
            break;
        }
    }

    size_t prevIdx = (nextIdx > 0) ? nextIdx - 1 : 0;
    const auto& prev = keyframes[prevIdx];
    const auto& next = keyframes[nextIdx];

    float t = (time - prev.Time) / (next.Time - prev.Time);

    switch (interp) {
        case Renderer::AnimationInterpolation::Step:
            return prev.Value;

        case Renderer::AnimationInterpolation::Linear:
        default:
            return glm::mix(prev.Value, next.Value, t);

        case Renderer::AnimationInterpolation::CubicSpline: {
            // Hermite spline interpolation
            float t2 = t * t;
            float t3 = t2 * t;
            float dt = next.Time - prev.Time;

            Math::Vec3 p0 = prev.Value;
            Math::Vec3 m0 = prev.OutTangent * dt;
            Math::Vec3 p1 = next.Value;
            Math::Vec3 m1 = next.InTangent * dt;

            return (2.0f * t3 - 3.0f * t2 + 1.0f) * p0 +
                   (t3 - 2.0f * t2 + t) * m0 +
                   (-2.0f * t3 + 3.0f * t2) * p1 +
                   (t3 - t2) * m1;
        }
    }
}

Math::Quat SkeletalRenderSystem::InterpolateQuat(
    const std::vector<Renderer::QuatKeyframe>& keyframes,
    float time,
    Renderer::AnimationInterpolation interp) {
    
    if (keyframes.empty()) {
        return Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    if (keyframes.size() == 1 || time <= keyframes.front().Time) {
        return keyframes.front().Value;
    }

    if (time >= keyframes.back().Time) {
        return keyframes.back().Value;
    }

    // Find surrounding keyframes
    size_t nextIdx = 0;
    for (size_t i = 0; i < keyframes.size(); ++i) {
        if (keyframes[i].Time > time) {
            nextIdx = i;
            break;
        }
    }

    size_t prevIdx = (nextIdx > 0) ? nextIdx - 1 : 0;
    const auto& prev = keyframes[prevIdx];
    const auto& next = keyframes[nextIdx];

    float t = (time - prev.Time) / (next.Time - prev.Time);

    switch (interp) {
        case Renderer::AnimationInterpolation::Step:
            return prev.Value;

        case Renderer::AnimationInterpolation::Linear:
        default:
            return glm::slerp(prev.Value, next.Value, t);

        case Renderer::AnimationInterpolation::CubicSpline:
            // For quaternions, cubic spline is complex - use slerp as approximation
            return glm::slerp(prev.Value, next.Value, t);
    }
}

} // namespace ECS
} // namespace Core
