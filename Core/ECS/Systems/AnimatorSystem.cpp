#include "Core/ECS/Systems/AnimatorSystem.h"
#include <algorithm>
#include <cmath>

namespace Core {
namespace ECS {

//=============================================================================
// Forward Declarations - Helper Functions
//=============================================================================

namespace {

// Linear interpolation for Vec3 keyframes
Math::Vec3 InterpolateVec3Keyframes(const std::vector<Renderer::Vec3Keyframe>& keyframes,
                                     float time,
                                     Renderer::AnimationInterpolation interp);

// Linear interpolation for Quat keyframes  
Math::Quat InterpolateQuatKeyframes(const std::vector<Renderer::QuatKeyframe>& keyframes,
                                     float time,
                                     Renderer::AnimationInterpolation interp);

// Sample animation clip at given time into skeleton pose
void SampleAnimationClip(const Renderer::AnimationClip& clip,
                         float time,
                         const Renderer::Skeleton& skeleton,
                         SkeletonPose& pose);

// Update animation time for current state
void UpdateAnimationTime(AnimatorComponent& animator,
                         const SkeletalMeshComponent& skeletal,
                         float deltaTime);

} // anonymous namespace

//=============================================================================
// Initialization
//=============================================================================

void AnimatorSystem::Initialize() {
    if (m_Initialized) {
        ENGINE_CORE_WARN("AnimatorSystem already initialized");
        return;
    }

    m_Statistics.Reset();
    m_Initialized = true;

    ENGINE_CORE_INFO("AnimatorSystem initialized");
}

void AnimatorSystem::Shutdown() {
    if (!m_Initialized) {
        return;
    }

    m_Statistics.Reset();
    m_Initialized = false;

    ENGINE_CORE_INFO("AnimatorSystem shutdown");
}

//=============================================================================
// Update Methods
//=============================================================================

void AnimatorSystem::Update(Scene& scene, float deltaTime) {
    PROFILE_SCOPE("AnimatorSystem::Update");

    m_Statistics.Reset();

    auto view = scene.View<AnimatorComponent, SkeletalMeshComponent>();

    for (auto entity : view) {
        auto& animator = view.get<AnimatorComponent>(entity);
        auto& skeletal = view.get<SkeletalMeshComponent>(entity);

        // Skip if not playing or invalid
        if (!animator.RuntimeState.IsPlaying || !skeletal.IsValid()) {
            continue;
        }

        ProcessEntity(entity, animator, skeletal, deltaTime);
    }

    ENGINE_CORE_TRACE("AnimatorSystem: {} entities, {} transitions, {} evaluations",
                      m_Statistics.AnimatedEntityCount,
                      m_Statistics.ActiveTransitionCount,
                      m_Statistics.StateMachineEvaluations);
}

void AnimatorSystem::UpdateParallel(Scene& scene, float deltaTime) {
    PROFILE_SCOPE("AnimatorSystem::UpdateParallel");

    // Reset statistics with atomic counters
    m_AtomicEntityCount.store(0, std::memory_order_relaxed);
    m_AtomicTransitionCount.store(0, std::memory_order_relaxed);
    m_AtomicEvaluationCount.store(0, std::memory_order_relaxed);
    m_ThreadLocalResults.Reset();

    auto& registry = scene.GetRegistry();
    auto view = registry.view<AnimatorComponent, SkeletalMeshComponent>();

    // Collect entities for parallel processing
    std::vector<entt::entity> entities;
    entities.reserve(view.size_hint());
    for (auto entity : view) {
        entities.push_back(entity);
    }

    if (entities.empty()) {
        m_Statistics.Reset();
        return;
    }

    uint32_t entityCount = static_cast<uint32_t>(entities.size());

    // Fall back to sequential for small counts
    if (!ShouldRunParallel(entityCount)) {
        Update(scene, deltaTime);
        return;
    }

    // Parallel state machine evaluation and pose computation
    JobSystem::Context ctx;
    JobSystem::Dispatch(ctx, entityCount, GetBatchSize(),
        [&](uint32_t index) {
            entt::entity entity = entities[index];
            auto& animator = registry.get<AnimatorComponent>(entity);
            auto& skeletal = registry.get<SkeletalMeshComponent>(entity);

            if (!animator.RuntimeState.IsPlaying || !skeletal.IsValid()) {
                return;
            }

            // Ensure animator is initialized
            animator.EnsureInitialized();

            // Evaluate state machine
            bool transitionTriggered = EvaluateStateMachine(animator, deltaTime);
            
            m_AtomicEntityCount.fetch_add(1, std::memory_order_relaxed);
            m_AtomicEvaluationCount.fetch_add(1, std::memory_order_relaxed);
            
            if (transitionTriggered || animator.RuntimeState.CurrentTransition.IsActive()) {
                m_AtomicTransitionCount.fetch_add(1, std::memory_order_relaxed);
            }

            // Compute blended pose
            SkeletonPose blendedPose = BlendAnimationStates(animator, skeletal, deltaTime);

            // Store result in thread-local buffer
            auto& results = m_ThreadLocalResults.Get();
            StateMachineEvalResult result;
            result.Entity = entity;
            result.TransitionTriggered = transitionTriggered;
            result.BlendWeight = animator.RuntimeState.CurrentTransition.Progress;
            result.ResultPose = std::move(blendedPose);
            results.push_back(std::move(result));
        });
    JobSystem::Wait(ctx);

    // Sequential phase: Apply poses to components (modifies shared state)
    {
        std::lock_guard<std::mutex> lock(m_StateMutex);
        
        // Aggregate all thread-local results
        auto aggregatedResults = m_ThreadLocalResults.Aggregate(
            [](std::vector<StateMachineEvalResult> acc, const std::vector<StateMachineEvalResult>& local) {
                acc.insert(acc.end(), local.begin(), local.end());
                return acc;
            });

        for (auto& result : aggregatedResults) {
            auto& skeletal = registry.get<SkeletalMeshComponent>(result.Entity);
            ApplyPoseToComponent(result.ResultPose, skeletal);
        }
    }

    // Update statistics from atomics
    m_Statistics.AnimatedEntityCount = m_AtomicEntityCount.load();
    m_Statistics.ActiveTransitionCount = m_AtomicTransitionCount.load();
    m_Statistics.StateMachineEvaluations = m_AtomicEvaluationCount.load();

    ENGINE_CORE_TRACE("AnimatorSystem (parallel): {} entities, {} transitions, {} evaluations",
                      m_Statistics.AnimatedEntityCount,
                      m_Statistics.ActiveTransitionCount,
                      m_Statistics.StateMachineEvaluations);
}

//=============================================================================
// Entity Processing
//=============================================================================

void AnimatorSystem::ProcessEntity(entt::entity entity,
                                   AnimatorComponent& animator,
                                   SkeletalMeshComponent& skeletal,
                                   float deltaTime) {
    (void)entity;  // Entity ID available for debugging/events
    
    // Ensure animator is initialized
    animator.EnsureInitialized();

    // 1. Update animation time
    UpdateAnimationTime(animator, skeletal, deltaTime);

    // 2. Evaluate state machine and check for transitions
    bool transitionTriggered = EvaluateStateMachine(animator, deltaTime);

    m_Statistics.AnimatedEntityCount++;
    m_Statistics.StateMachineEvaluations++;

    if (transitionTriggered || animator.RuntimeState.CurrentTransition.IsActive()) {
        m_Statistics.ActiveTransitionCount++;
    }

    // 3. Sample and blend poses
    SkeletonPose blendedPose = BlendAnimationStates(animator, skeletal, deltaTime);

    // 4. Apply the final pose to the skeletal mesh component
    ApplyPoseToComponent(blendedPose, skeletal);
}

//=============================================================================
// State Machine Evaluation
//=============================================================================

bool AnimatorSystem::EvaluateStateMachine(AnimatorComponent& animator, float deltaTime) {
    auto& runtime = animator.RuntimeState;
    const auto& sm = animator.StateMachine;

    // Get current state
    const AnimationState* currentState = sm.FindState(runtime.CurrentStateName);
    if (!currentState) {
        if (!sm.DefaultStateName.empty()) {
            runtime.CurrentStateName = sm.DefaultStateName;
            currentState = sm.FindState(runtime.CurrentStateName);
            ENGINE_CORE_DEBUG("AnimatorSystem: Reset to default state '{}'", sm.DefaultStateName);
        }
        if (!currentState) {
            ENGINE_CORE_WARN("AnimatorSystem: No valid current state");
            return false;
        }
    }

    // Update current transition if active
    if (runtime.CurrentTransition.IsActive()) {
        const auto* transition = runtime.CurrentTransition.Transition;
        if (transition) {
            float transitionDuration = transition->TransitionDuration;
            if (transitionDuration > 0.0f) {
                runtime.CurrentTransition.Progress += deltaTime / transitionDuration;
            } else {
                runtime.CurrentTransition.Progress = 1.0f;
            }

            // Complete transition when progress reaches 1
            if (runtime.CurrentTransition.Progress >= 1.0f) {
                runtime.CurrentTransition.Progress = 1.0f;
                runtime.CurrentTransition.IsComplete = true;

                // Move to target state
                runtime.CurrentStateName = transition->TargetState;
                runtime.CurrentStateTime = 0.0f;
                runtime.NormalizedTime = 0.0f;
                runtime.PreviousStateName.clear();
                runtime.PreviousStateTime = 0.0f;
                runtime.CurrentTransition.Reset();

                ENGINE_CORE_DEBUG("AnimatorSystem: Transition complete to '{}'", runtime.CurrentStateName);
                return true;
            }
        }
        return false;  // Still in transition, don't start new ones
    }

    // Update animation time for current state
    // Note: Animation time is updated in ProcessEntity, we just use it here for transition evaluation

    // Get transitions from current state
    auto transitions = sm.GetTransitionsFrom(runtime.CurrentStateName);

    // Sort by priority (already sorted in state machine, but ensure order)
    std::sort(transitions.begin(), transitions.end(),
        [](const AnimationTransition* a, const AnimationTransition* b) {
            return a->Priority > b->Priority;
        });

    // Evaluate transitions
    for (const AnimationTransition* transition : transitions) {
        // Skip self-transitions that aren't allowed
        if (transition->TargetState == runtime.CurrentStateName && !transition->CanTransitionToSelf) {
            continue;
        }

        // Evaluate conditions
        if (transition->EvaluateConditions(animator.Parameters, runtime.NormalizedTime)) {
            // Consume any trigger parameters used in this transition
            for (const auto& condition : transition->Conditions) {
                auto it = animator.Parameters.find(condition.ParameterName);
                if (it != animator.Parameters.end() && 
                    it->second.Type == AnimatorParameterType::Trigger) {
                    it->second.ConsumeTrigger();
                }
            }

            // Start the transition
            runtime.PreviousStateName = runtime.CurrentStateName;
            runtime.PreviousStateTime = runtime.CurrentStateTime;
            runtime.CurrentTransition.Transition = transition;
            runtime.CurrentTransition.Progress = 0.0f;
            runtime.CurrentTransition.SourceTime = runtime.CurrentStateTime;
            runtime.CurrentTransition.IsComplete = false;

            ENGINE_CORE_DEBUG("AnimatorSystem: Starting transition '{}' -> '{}'",
                            runtime.CurrentStateName, transition->TargetState);

            return true;
        }
    }

    return false;
}

//=============================================================================
// Animation Blending
//=============================================================================

SkeletonPose AnimatorSystem::BlendAnimationStates(const AnimatorComponent& animator,
                                                   const SkeletalMeshComponent& skeletal,
                                                   float deltaTime) {
    const auto& runtime = animator.RuntimeState;
    const auto& sm = animator.StateMachine;

    // Get skeleton information
    if (!skeletal.HasSkeleton()) {
        return SkeletonPose{};
    }

    const auto& skeleton = skeletal.MeshData->GetSkeleton();
    uint32_t boneCount = skeleton.GetBoneCount();

    SkeletonPose resultPose;
    resultPose.Resize(boneCount);

    // Get current state
    const AnimationState* currentState = sm.FindState(runtime.CurrentStateName);
    if (!currentState) {
        // Return bind pose if no valid state
        resultPose = skeletal.BindPose;
        return resultPose;
    }

    // Find animation clip for current state
    const AnimationClip* currentClip = skeletal.MeshData->FindAnimation(currentState->AnimationClipName);
    
    // Calculate current animation time
    float currentTime = runtime.CurrentStateTime;
    float currentDuration = currentClip ? currentClip->Duration : 1.0f;
    
    // Apply speed multiplier and handle looping
    float effectiveTime = currentTime * currentState->SpeedMultiplier;
    if (currentState->Loop && currentDuration > 0.0f) {
        effectiveTime = std::fmod(effectiveTime, currentDuration);
    } else if (currentDuration > 0.0f) {
        effectiveTime = std::min(effectiveTime, currentDuration);
    }

    // Sample current state animation
    SkeletonPose currentPose;
    currentPose.Resize(boneCount);
    
    if (currentClip) {
        SampleAnimationClip(*currentClip, effectiveTime, skeleton, currentPose);
    } else {
        // Use bind pose if no clip found
        currentPose = skeletal.BindPose;
        ENGINE_CORE_WARN("AnimatorSystem: Animation clip '{}' not found for state '{}'",
                        currentState->AnimationClipName, currentState->Name);
    }

    // Check if we're in a transition
    if (runtime.CurrentTransition.IsActive()) {
        const auto* transition = runtime.CurrentTransition.Transition;
        if (transition) {
            // Get previous state for blending
            const AnimationState* previousState = sm.FindState(runtime.PreviousStateName);
            
            if (previousState) {
                const AnimationClip* previousClip = skeletal.MeshData->FindAnimation(previousState->AnimationClipName);
                
                SkeletonPose previousPose;
                previousPose.Resize(boneCount);
                
                if (previousClip) {
                    float previousTime = runtime.PreviousStateTime * previousState->SpeedMultiplier;
                    // Continue playing previous animation during transition
                    previousTime += deltaTime * previousState->SpeedMultiplier;
                    
                    if (previousState->Loop && previousClip->Duration > 0.0f) {
                        previousTime = std::fmod(previousTime, previousClip->Duration);
                    }
                    
                    SampleAnimationClip(*previousClip, previousTime, skeleton, previousPose);
                } else {
                    previousPose = skeletal.BindPose;
                }

                // Lerp between poses based on transition progress
                float blendWeight = runtime.CurrentTransition.Progress;
                for (uint32_t i = 0; i < boneCount; ++i) {
                    resultPose.LocalPoses[i] = BonePose::Lerp(
                        previousPose.LocalPoses[i],
                        currentPose.LocalPoses[i],
                        blendWeight
                    );
                }
            } else {
                resultPose = currentPose;
            }
        } else {
            resultPose = currentPose;
        }
    } else {
        resultPose = currentPose;
    }

    return resultPose;
}

//=============================================================================
// Pose Application
//=============================================================================

void AnimatorSystem::ApplyPoseToComponent(const SkeletonPose& pose, 
                                          SkeletalMeshComponent& skeletal) {
    if (!skeletal.HasSkeleton() || !pose.IsValid()) {
        return;
    }

    // Copy local poses to component
    size_t boneCount = std::min(pose.LocalPoses.size(), skeletal.CurrentPose.LocalPoses.size());
    for (size_t i = 0; i < boneCount; ++i) {
        skeletal.CurrentPose.LocalPoses[i] = pose.LocalPoses[i];
    }

    // The SkeletalRenderSystem will compute global poses and skinning matrices
    // We just need to mark that pose has been updated
}

//=============================================================================
// Animation Sampling (Helper Functions)
//=============================================================================

namespace {

// Linear interpolation for Vec3 keyframes
Math::Vec3 InterpolateVec3Keyframes(const std::vector<Renderer::Vec3Keyframe>& keyframes,
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

// Linear interpolation for Quat keyframes
Math::Quat InterpolateQuatKeyframes(const std::vector<Renderer::QuatKeyframe>& keyframes,
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

// Sample animation clip at given time into skeleton pose
void SampleAnimationClip(const Renderer::AnimationClip& clip,
                         float time,
                         const Renderer::Skeleton& skeleton,
                         SkeletonPose& pose) {
    // Sample each channel
    for (const auto& channel : clip.Channels) {
        if (channel.BoneIndex < 0 || 
            channel.BoneIndex >= static_cast<int32_t>(pose.LocalPoses.size())) {
            continue;
        }

        BonePose& bonePose = pose.LocalPoses[channel.BoneIndex];

        switch (channel.TargetPath) {
            case Renderer::AnimationTargetPath::Translation:
                bonePose.Translation = InterpolateVec3Keyframes(
                    channel.Vec3Keyframes, time, channel.Interpolation);
                break;

            case Renderer::AnimationTargetPath::Rotation:
                bonePose.Rotation = InterpolateQuatKeyframes(
                    channel.QuatKeyframes, time, channel.Interpolation);
                break;

            case Renderer::AnimationTargetPath::Scale:
                bonePose.Scale = InterpolateVec3Keyframes(
                    channel.Vec3Keyframes, time, channel.Interpolation);
                break;

            default:
                break;
        }
    }

    // Mark skeleton as used (suppress unused parameter warning)
    (void)skeleton;
}

// Update animation time for current state
void UpdateAnimationTime(AnimatorComponent& animator,
                         const SkeletalMeshComponent& skeletal,
                         float deltaTime) {
    auto& runtime = animator.RuntimeState;
    const auto& sm = animator.StateMachine;

    // Get current state
    const AnimationState* currentState = sm.FindState(runtime.CurrentStateName);
    if (!currentState) {
        return;
    }

    // Find animation clip for current state
    const Renderer::AnimationClip* clip = nullptr;
    if (skeletal.MeshData) {
        clip = skeletal.MeshData->FindAnimation(currentState->AnimationClipName);
    }

    float duration = clip ? clip->Duration : 1.0f;
    if (duration <= 0.0f) {
        duration = 1.0f;  // Prevent division by zero
    }

    // Update time with speed multiplier
    runtime.CurrentStateTime += deltaTime * currentState->SpeedMultiplier;

    // Handle looping or completion
    if (currentState->Loop) {
        if (runtime.CurrentStateTime >= duration) {
            runtime.CurrentStateTime = std::fmod(runtime.CurrentStateTime, duration);
        }
    } else {
        // Non-looping: clamp to duration
        if (runtime.CurrentStateTime >= duration) {
            runtime.CurrentStateTime = duration;
        }
    }

    // Update normalized time (0 to 1)
    runtime.NormalizedTime = runtime.CurrentStateTime / duration;
}

} // anonymous namespace

} // namespace ECS
} // namespace Core
