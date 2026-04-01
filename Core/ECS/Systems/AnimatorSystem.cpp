#include "Core/ECS/Systems/AnimatorSystem.h"
#include <algorithm>
#include <cmath>

namespace Core {
namespace ECS {

//=============================================================================
// Forward Declarations - Helper Functions
//=============================================================================

// Forward declarations moved to implementation section below

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
// Animation Sampling Helper Functions
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
            ENGINE_CORE_TRACE("AnimatorSystem: Reset to default state '{}'", sm.DefaultStateName);
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

                ENGINE_CORE_TRACE("AnimatorSystem: Transition complete to '{}'", runtime.CurrentStateName);
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

            ENGINE_CORE_TRACE("AnimatorSystem: Starting transition '{}' -> '{}'",
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
    const Renderer::AnimationClip* currentClip = skeletal.MeshData->FindAnimation(currentState->AnimationClipName);
    
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
                const Renderer::AnimationClip* previousClip = skeletal.MeshData->FindAnimation(previousState->AnimationClipName);
                
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

                // Apply transition curve to the linear progress
                float blendWeight = ApplyTransitionCurve(
                    runtime.CurrentTransition.Progress, 
                    transition->CurveType);
                    
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

    // Apply animation layers on top of the base pose
    if (!animator.Layers.empty()) {
        resultPose = BlendLayers(animator, skeletal, resultPose);
    }

    return resultPose;
}

//=============================================================================
// Transition Curves (Step 12.4)
//=============================================================================

float AnimatorSystem::ApplyTransitionCurve(float t, TransitionCurveType curveType) {
    // Clamp input to [0, 1]
    t = std::max(0.0f, std::min(1.0f, t));

    switch (curveType) {
        case TransitionCurveType::Linear:
            return t;

        case TransitionCurveType::EaseIn:
            // Quadratic ease-in: accelerates from zero velocity
            return t * t;

        case TransitionCurveType::EaseOut:
            // Quadratic ease-out: decelerates to zero velocity
            return t * (2.0f - t);

        case TransitionCurveType::EaseInOut:
            // Smoothstep: S-curve with smooth start and end
            // f(t) = 3t^2 - 2t^3
            return t * t * (3.0f - 2.0f * t);

        case TransitionCurveType::Cubic:
            // Smoother step: even smoother S-curve (Hermite basis)
            // f(t) = 6t^5 - 15t^4 + 10t^3
            return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);

        default:
            return t;
    }
}

//=============================================================================
// Blend Tree Evaluation (Step 12.4)
//=============================================================================

SkeletonPose AnimatorSystem::EvaluateBlendTree(const BlendTree& tree,
                                                const AnimatorComponent& animator,
                                                const SkeletalMeshComponent& skeletal) {
    if (!skeletal.HasSkeleton() || !tree.IsValid()) {
        return SkeletonPose{};
    }

    const auto& skeleton = skeletal.MeshData->GetSkeleton();
    uint32_t boneCount = skeleton.GetBoneCount();

    // Get blend parameters from animator
    float paramX = animator.GetFloat(tree.ParameterX);
    float paramY = animator.GetFloat(tree.ParameterY);

    // Create a mutable copy of the tree to compute weights
    BlendTree mutableTree = tree;
    mutableTree.ComputeWeights(paramX, paramY);

    // Get all active clips with their weights
    auto activeClips = mutableTree.GetActiveClips();

    if (activeClips.empty()) {
        // Return bind pose if no active clips
        return skeletal.BindPose;
    }

    // If only one clip is active, sample it directly
    if (activeClips.size() == 1) {
        const auto& [clipName, weight] = activeClips[0];
        const auto* clip = skeletal.MeshData->FindAnimation(clipName);
        
        if (clip) {
            SkeletonPose pose;
            pose.Resize(boneCount);
            
            // Use animator's current state time for the blend tree
            float time = std::fmod(animator.RuntimeState.CurrentStateTime, clip->Duration);
            SampleAnimationClip(*clip, time, skeleton, pose);
            return pose;
        }
        return skeletal.BindPose;
    }

    // Multiple active clips - blend them together
    SkeletonPose blendedPose;
    blendedPose.Resize(boneCount);

    // Initialize with identity/zero pose
    for (uint32_t i = 0; i < boneCount; ++i) {
        blendedPose.LocalPoses[i].Translation = Math::Vec3(0.0f);
        blendedPose.LocalPoses[i].Rotation = Math::Quat(0.0f, 0.0f, 0.0f, 0.0f);  // Will accumulate
        blendedPose.LocalPoses[i].Scale = Math::Vec3(0.0f);
    }

    float totalWeight = 0.0f;
    std::vector<std::pair<SkeletonPose, float>> sampledPoses;
    sampledPoses.reserve(activeClips.size());

    // Sample all active clips
    for (const auto& [clipName, weight] : activeClips) {
        if (weight <= 0.0001f) continue;

        const auto* clip = skeletal.MeshData->FindAnimation(clipName);
        if (!clip) continue;

        SkeletonPose clipPose;
        clipPose.Resize(boneCount);

        float time = std::fmod(animator.RuntimeState.CurrentStateTime, clip->Duration);
        SampleAnimationClip(*clip, time, skeleton, clipPose);

        sampledPoses.emplace_back(std::move(clipPose), weight);
        totalWeight += weight;
    }

    if (sampledPoses.empty() || totalWeight <= 0.0001f) {
        return skeletal.BindPose;
    }

    // Normalize weights and blend poses
    // Use weighted average for translation and scale
    // Use normalized quaternion blending for rotation
    for (uint32_t boneIdx = 0; boneIdx < boneCount; ++boneIdx) {
        Math::Vec3 blendedTranslation(0.0f);
        Math::Vec3 blendedScale(0.0f);
        Math::Quat blendedRotation(0.0f, 0.0f, 0.0f, 0.0f);

        for (const auto& [pose, weight] : sampledPoses) {
            float normalizedWeight = weight / totalWeight;
            const auto& bonePose = pose.LocalPoses[boneIdx];

            blendedTranslation += bonePose.Translation * normalizedWeight;
            blendedScale += bonePose.Scale * normalizedWeight;

            // Quaternion blending: accumulate weighted quaternions
            // Note: We need to handle quaternion sign to avoid rotation issues
            Math::Quat q = bonePose.Rotation;
            
            // Ensure quaternions are in the same hemisphere
            if (boneIdx == 0 || glm::dot(blendedRotation, q) >= 0.0f) {
                blendedRotation += q * normalizedWeight;
            } else {
                blendedRotation += (-q) * normalizedWeight;
            }
        }

        // Normalize the accumulated quaternion
        float quatLength = glm::length(blendedRotation);
        if (quatLength > 0.0001f) {
            blendedRotation = glm::normalize(blendedRotation);
        } else {
            blendedRotation = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
        }

        blendedPose.LocalPoses[boneIdx].Translation = blendedTranslation;
        blendedPose.LocalPoses[boneIdx].Rotation = blendedRotation;
        blendedPose.LocalPoses[boneIdx].Scale = blendedScale;
    }

    return blendedPose;
}

//=============================================================================
// Multi-Layer Animation Blending (Step 12.4)
//=============================================================================

SkeletonPose AnimatorSystem::BlendLayers(const AnimatorComponent& animator,
                                          const SkeletalMeshComponent& skeletal,
                                          const SkeletonPose& basePose) {
    if (!skeletal.HasSkeleton() || animator.Layers.empty()) {
        return basePose;
    }

    const auto& skeleton = skeletal.MeshData->GetSkeleton();
    SkeletonPose resultPose = basePose;

    // Process layers in order (they should already be sorted by LayerIndex)
    for (const auto& layer : animator.Layers) {
        // Skip inactive layers
        if (layer.Weight <= 0.0001f) {
            continue;
        }

        // Evaluate the layer's pose
        SkeletonPose layerPose;

        if (layer.HasBlendTree()) {
            // Use the layer's blend tree
            layerPose = EvaluateBlendTree(*layer.LayerBlendTree, animator, skeletal);
        } else if (!layer.StateMachineName.empty()) {
            // TODO: Evaluate separate state machine for this layer
            // For now, we use the current animation state time to sample
            // the layer's current state
            const auto* state = animator.StateMachine.FindState(layer.CurrentStateName);
            if (state) {
                const auto* clip = skeletal.MeshData->FindAnimation(state->AnimationClipName);
                if (clip) {
                    layerPose.Resize(skeleton.GetBoneCount());
                    float time = std::fmod(layer.CurrentTime * state->SpeedMultiplier, 
                                          clip->Duration);
                    SampleAnimationClip(*clip, time, skeleton, layerPose);
                }
            }
        }

        // Skip if we couldn't produce a valid layer pose
        if (!layerPose.IsValid()) {
            continue;
        }

        // Blend the layer pose into the result
        if (layer.IsAdditive) {
            // For additive blending, we need a reference pose
            // Use bind pose as the reference for computing the additive delta
            SkeletonPose additivePose = ComputeAdditivePose(layerPose, skeletal.BindPose);
            ApplyAdditivePose(resultPose, additivePose, layer.Weight);
        } else {
            // Override blending with bone mask
            BlendPoseWithMask(resultPose, layerPose, layer.Weight,
                             layer.AffectedBones, skeleton, false);
        }
    }

    return resultPose;
}

SkeletonPose AnimatorSystem::EvaluateLayer(const AnimationLayer& layer,
                                            const AnimatorComponent& animator,
                                            const SkeletalMeshComponent& skeletal,
                                            float deltaTime) {
    (void)deltaTime;  // Used for layer-specific time updates in future

    if (!skeletal.HasSkeleton()) {
        return SkeletonPose{};
    }

    const auto& skeleton = skeletal.MeshData->GetSkeleton();
    uint32_t boneCount = skeleton.GetBoneCount();

    SkeletonPose layerPose;
    layerPose.Resize(boneCount);

    if (layer.HasBlendTree()) {
        // Evaluate the blend tree
        return EvaluateBlendTree(*layer.LayerBlendTree, animator, skeletal);
    }

    // Fall back to sampling the layer's current animation
    const auto* state = animator.StateMachine.FindState(layer.CurrentStateName);
    if (!state) {
        return skeletal.BindPose;
    }

    const auto* clip = skeletal.MeshData->FindAnimation(state->AnimationClipName);
    if (!clip) {
        return skeletal.BindPose;
    }

    float time = layer.CurrentTime * state->SpeedMultiplier;
    if (state->Loop && clip->Duration > 0.0f) {
        time = std::fmod(time, clip->Duration);
    }

    SampleAnimationClip(*clip, time, skeleton, layerPose);
    return layerPose;
}

void AnimatorSystem::BlendPoseWithMask(SkeletonPose& base,
                                        const SkeletonPose& overlay,
                                        float weight,
                                        const std::vector<std::string>& affectedBones,
                                        const Renderer::Skeleton& skeleton,
                                        bool isAdditive) {
    if (!base.IsValid() || !overlay.IsValid()) {
        return;
    }

    // Clamp weight
    weight = std::max(0.0f, std::min(1.0f, weight));
    if (weight <= 0.0001f) {
        return;
    }

    uint32_t boneCount = static_cast<uint32_t>(
        std::min(base.LocalPoses.size(), overlay.LocalPoses.size()));

    // Build a set of affected bone indices for faster lookup
    std::vector<bool> boneAffected(boneCount, affectedBones.empty());
    
    if (!affectedBones.empty()) {
        for (const auto& boneName : affectedBones) {
            int32_t boneIdx = skeleton.FindBoneIndex(boneName);
            if (boneIdx >= 0 && boneIdx < static_cast<int32_t>(boneCount)) {
                boneAffected[boneIdx] = true;
                
                // Also mark child bones as affected (hierarchical masking)
                // This propagates the mask down the bone hierarchy
                for (uint32_t childIdx = boneIdx + 1; childIdx < boneCount; ++childIdx) {
                    const auto& bone = skeleton.Bones[childIdx];
                    if (bone.ParentIndex >= boneIdx) {
                        // Check if this bone is a descendant
                        int32_t parent = bone.ParentIndex;
                        while (parent >= 0) {
                            if (parent == boneIdx) {
                                boneAffected[childIdx] = true;
                                break;
                            }
                            if (parent < static_cast<int32_t>(skeleton.GetBoneCount())) {
                                parent = skeleton.Bones[parent].ParentIndex;
                            } else {
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    // Blend poses for affected bones
    for (uint32_t i = 0; i < boneCount; ++i) {
        if (!boneAffected[i]) {
            continue;
        }

        if (isAdditive) {
            // Additive blending: add the overlay deltas
            base.LocalPoses[i].Translation += overlay.LocalPoses[i].Translation * weight;
            base.LocalPoses[i].Scale += overlay.LocalPoses[i].Scale * weight;
            
            // For rotation, we multiply quaternions (additive rotation)
            Math::Quat additiveRot = glm::slerp(
                Math::Quat(1.0f, 0.0f, 0.0f, 0.0f),
                overlay.LocalPoses[i].Rotation,
                weight);
            base.LocalPoses[i].Rotation = additiveRot * base.LocalPoses[i].Rotation;
        } else {
            // Override blending: interpolate towards overlay
            base.LocalPoses[i] = BonePose::Lerp(
                base.LocalPoses[i],
                overlay.LocalPoses[i],
                weight);
        }
    }
}

//=============================================================================
// Additive Animation Support (Step 12.4)
//=============================================================================

SkeletonPose AnimatorSystem::ComputeAdditivePose(const SkeletonPose& animation,
                                                  const SkeletonPose& reference) {
    SkeletonPose additivePose;
    
    if (!animation.IsValid() || !reference.IsValid()) {
        return additivePose;
    }

    uint32_t boneCount = static_cast<uint32_t>(
        std::min(animation.LocalPoses.size(), reference.LocalPoses.size()));
    
    additivePose.Resize(boneCount);

    for (uint32_t i = 0; i < boneCount; ++i) {
        const auto& animBone = animation.LocalPoses[i];
        const auto& refBone = reference.LocalPoses[i];

        // Additive translation: delta from reference
        additivePose.LocalPoses[i].Translation = 
            animBone.Translation - refBone.Translation;

        // Additive rotation: relative rotation from reference to animation
        // Qadd = Qanim * inverse(Qref)
        Math::Quat refInverse = glm::inverse(refBone.Rotation);
        additivePose.LocalPoses[i].Rotation = animBone.Rotation * refInverse;

        // Additive scale: ratio relative to reference (multiplicative)
        // For additive, we store the delta as a multiplication factor
        // To apply: result_scale = base_scale * (1 + additive_scale * weight)
        // So we store: additive_scale = anim_scale / ref_scale - 1
        Math::Vec3 scaleDelta(0.0f);
        if (std::abs(refBone.Scale.x) > 0.0001f) {
            scaleDelta.x = animBone.Scale.x / refBone.Scale.x - 1.0f;
        }
        if (std::abs(refBone.Scale.y) > 0.0001f) {
            scaleDelta.y = animBone.Scale.y / refBone.Scale.y - 1.0f;
        }
        if (std::abs(refBone.Scale.z) > 0.0001f) {
            scaleDelta.z = animBone.Scale.z / refBone.Scale.z - 1.0f;
        }
        additivePose.LocalPoses[i].Scale = scaleDelta;
    }

    return additivePose;
}

void AnimatorSystem::ApplyAdditivePose(SkeletonPose& base,
                                        const SkeletonPose& additive,
                                        float weight) {
    if (!base.IsValid() || !additive.IsValid()) {
        return;
    }

    // Clamp weight
    weight = std::max(0.0f, std::min(1.0f, weight));
    if (weight <= 0.0001f) {
        return;
    }

    uint32_t boneCount = static_cast<uint32_t>(
        std::min(base.LocalPoses.size(), additive.LocalPoses.size()));

    for (uint32_t i = 0; i < boneCount; ++i) {
        auto& baseBone = base.LocalPoses[i];
        const auto& addBone = additive.LocalPoses[i];

        // Apply additive translation
        baseBone.Translation += addBone.Translation * weight;

        // Apply additive rotation
        // Interpolate the additive rotation towards identity based on weight
        // Then multiply with base rotation
        Math::Quat identity(1.0f, 0.0f, 0.0f, 0.0f);
        Math::Quat weightedAddRot = glm::slerp(identity, addBone.Rotation, weight);
        baseBone.Rotation = weightedAddRot * baseBone.Rotation;
        baseBone.Rotation = glm::normalize(baseBone.Rotation);

        // Apply additive scale (multiplicative)
        // result = base * (1 + additive_delta * weight)
        baseBone.Scale.x *= (1.0f + addBone.Scale.x * weight);
        baseBone.Scale.y *= (1.0f + addBone.Scale.y * weight);
        baseBone.Scale.z *= (1.0f + addBone.Scale.z * weight);
    }
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

} // namespace ECS
} // namespace Core
