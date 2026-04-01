#include "Core/ECS/Systems/IKSystem.h"
#include "Core/Physics/PhysicsWorld.h"
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <cmath>
#include <algorithm>

namespace Core {
namespace ECS {

//=============================================================================
// Constructor/Destructor
//=============================================================================

IKSystem::IKSystem() = default;
IKSystem::~IKSystem() = default;

//=============================================================================
// Initialization
//=============================================================================

void IKSystem::Initialize(Physics::PhysicsWorld* physicsWorld) {
    if (m_Initialized) {
        ENGINE_CORE_WARN("IKSystem already initialized");
        return;
    }

    m_PhysicsWorld = physicsWorld;
    m_Statistics.Reset();
    m_Initialized = true;

    ENGINE_CORE_INFO("IKSystem initialized with physics world: {}", 
                     physicsWorld ? "yes" : "no");
}

void IKSystem::Shutdown() {
    if (!m_Initialized) return;

    m_PhysicsWorld = nullptr;
    m_Statistics.Reset();
    m_Initialized = false;

    ENGINE_CORE_INFO("IKSystem shutdown");
}

//=============================================================================
// Update Methods
//=============================================================================

void IKSystem::Update(Scene& scene, float deltaTime) {
    PROFILE_SCOPE("IKSystem::Update");

    m_Statistics.Reset();

    auto view = scene.View<IKComponent, SkeletalMeshComponent, TransformComponent>();

    for (auto entity : view) {
        auto& ik = view.get<IKComponent>(entity);
        auto& skeletal = view.get<SkeletalMeshComponent>(entity);
        const auto& transform = view.get<TransformComponent>(entity);

        if (!ik.Enabled || ik.GlobalWeight <= 0.001f) continue;
        if (!skeletal.IsValid()) continue;

        ProcessEntity(entity, ik, skeletal, transform, deltaTime);
    }

    ENGINE_CORE_TRACE("IKSystem: {} entities, {} chains, {} raycasts",
                      m_Statistics.EntitiesProcessed,
                      m_Statistics.ChainsEvaluated,
                      m_Statistics.RaycastsPerformed);
}

void IKSystem::UpdateParallel(Scene& scene, float deltaTime) {
    PROFILE_SCOPE("IKSystem::UpdateParallel");

    // Reset atomics
    m_AtomicEntities.store(0);
    m_AtomicChains.store(0);
    m_AtomicRaycasts.store(0);

    auto view = scene.View<IKComponent, SkeletalMeshComponent, TransformComponent>();

    // Collect entities
    std::vector<entt::entity> entities;
    for (auto entity : view) {
        entities.push_back(entity);
    }

    if (entities.empty()) {
        m_Statistics.Reset();
        return;
    }

    // For IK, prefer sequential processing due to physics raycast thread safety
    // Fall back to sequential for now - can be optimized later with batched raycasts
    Update(scene, deltaTime);
}

//=============================================================================
// Entity Processing
//=============================================================================

void IKSystem::ProcessEntity(entt::entity entity,
                              IKComponent& ik,
                              SkeletalMeshComponent& skeletal,
                              const TransformComponent& transform,
                              float deltaTime) {
    (void)entity;  // Available for debugging

    // Initialize bone indices if needed
    if (!ik.IsInitialized) {
        InitializeChainIndices(ik, skeletal);
        ik.IsInitialized = true;
    }

    m_Statistics.EntitiesProcessed++;

    // Process foot IK first (adjusts hip)
    if (ik.FootSettings.Enabled) {
        ProcessFootIK(ik, skeletal, transform, deltaTime);
    }

    // Process each IK chain
    for (size_t i = 0; i < ik.Chains.size(); ++i) {
        auto& chain = ik.Chains[i];
        auto& state = ik.ChainStates[i];

        // Skip if chain weight is zero
        if (state.CurrentWeight <= 0.001f && state.TargetWeight <= 0.001f) {
            continue;
        }

        // Blend weight towards target
        float blendSpeed = 10.0f;
        state.CurrentWeight = glm::mix(state.CurrentWeight, state.TargetWeight, 
                                        1.0f - std::exp(-blendSpeed * deltaTime));

        ProcessChain(chain, state, skeletal, transform, deltaTime);
        m_Statistics.ChainsEvaluated++;
    }
}

void IKSystem::InitializeChainIndices(IKComponent& ik,
                                       const SkeletalMeshComponent& skeletal) {
    if (!skeletal.HasSkeleton()) return;

    const auto& skeleton = skeletal.MeshData->GetSkeleton();

    // Initialize hip bone index
    ik.HipBoneIndex = skeleton.FindBoneIndex(ik.HipBoneName);

    // Initialize each chain's bone indices
    for (auto& chain : ik.Chains) {
        chain.BoneIndices.clear();
        chain.BoneIndices.reserve(chain.BoneNames.size());

        for (const auto& boneName : chain.BoneNames) {
            int32_t index = skeleton.FindBoneIndex(boneName);
            chain.BoneIndices.push_back(index);

            if (index < 0) {
                ENGINE_CORE_WARN("IKSystem: Bone '{}' not found for chain '{}'",
                                boneName, chain.Name);
            }
        }
    }
}

//=============================================================================
// Two-Bone IK Solver
//=============================================================================

TwoBoneIKResult IKSystem::SolveTwoBoneIK(
    const Math::Vec3& upperPos,
    const Math::Vec3& midPos,
    const Math::Vec3& endPos,
    const Math::Vec3& targetPos,
    const Math::Vec3& poleVector,
    float upperLength,
    float lowerLength,
    float minAngle,
    float maxAngle) 
{
    TwoBoneIKResult result;
    result.Solved = false;

    // Vector from root to target
    Math::Vec3 toTarget = targetPos - upperPos;
    float targetDist = glm::length(toTarget);

    if (targetDist < 0.001f) {
        // Target is at root - no valid solution
        result.UpperRotation = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
        result.LowerRotation = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
        result.EndRotation = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
        return result;
    }

    Math::Vec3 targetDir = toTarget / targetDist;

    // Calculate reach (total chain length)
    float totalLength = upperLength + lowerLength;
    float minLength = std::abs(upperLength - lowerLength);

    // Clamp target distance to valid range
    float clampedDist = std::clamp(targetDist, minLength + 0.001f, totalLength - 0.001f);
    result.ReachFraction = std::min(targetDist / totalLength, 1.0f);

    // Law of cosines to find knee angle
    // a^2 = b^2 + c^2 - 2bc*cos(A)
    // cos(A) = (b^2 + c^2 - a^2) / (2bc)
    float cosKneeAngle = (upperLength * upperLength + lowerLength * lowerLength - 
                          clampedDist * clampedDist) / 
                         (2.0f * upperLength * lowerLength);
    cosKneeAngle = std::clamp(cosKneeAngle, -1.0f, 1.0f);
    float kneeAngle = std::acos(cosKneeAngle);

    // Clamp knee angle to constraints
    float bentAngle = glm::pi<float>() - kneeAngle;
    bentAngle = std::clamp(bentAngle, minAngle, maxAngle);
    kneeAngle = glm::pi<float>() - bentAngle;

    // Find angle at upper bone (hip/shoulder)
    float cosUpperAngle = (clampedDist * clampedDist + upperLength * upperLength - 
                           lowerLength * lowerLength) / 
                          (2.0f * clampedDist * upperLength);
    cosUpperAngle = std::clamp(cosUpperAngle, -1.0f, 1.0f);
    float upperAngle = std::acos(cosUpperAngle);

    // Calculate the rotation plane using pole vector
    Math::Vec3 currentDir = glm::normalize(midPos - upperPos);
    
    // Project pole vector onto the plane perpendicular to target direction
    Math::Vec3 poleProjected = poleVector - targetDir * glm::dot(poleVector, targetDir);
    if (glm::length(poleProjected) < 0.001f) {
        // Pole is along target - use arbitrary perpendicular
        poleProjected = glm::cross(targetDir, Math::Vec3(0.0f, 1.0f, 0.0f));
        if (glm::length(poleProjected) < 0.001f) {
            poleProjected = glm::cross(targetDir, Math::Vec3(1.0f, 0.0f, 0.0f));
        }
    }
    poleProjected = glm::normalize(poleProjected);

    // Calculate upper bone rotation
    // First, rotate to point at target
    Math::Quat targetRot = glm::rotation(currentDir, targetDir);
    
    // Then, rotate within the plane to account for bend
    Math::Vec3 rotatedDir = targetRot * currentDir;
    Math::Vec3 bendAxis = glm::normalize(glm::cross(rotatedDir, poleProjected));
    if (glm::length(bendAxis) < 0.001f) {
        bendAxis = glm::normalize(glm::cross(targetDir, poleProjected));
    }
    Math::Quat bendRot = glm::angleAxis(upperAngle, bendAxis);
    
    result.UpperRotation = bendRot * targetRot;

    // Calculate lower bone rotation (just the knee bend)
    Math::Vec3 lowerAxis = glm::normalize(glm::cross(
        glm::normalize(midPos - upperPos),
        glm::normalize(endPos - midPos)));
    if (glm::length(lowerAxis) < 0.001f) {
        lowerAxis = Math::Vec3(1.0f, 0.0f, 0.0f);
    }
    
    // The lower bone needs to bend by (pi - kneeAngle) relative to upper
    float currentKneeAngle = glm::acos(std::clamp(
        glm::dot(glm::normalize(midPos - upperPos), 
                 glm::normalize(endPos - midPos)), -1.0f, 1.0f));
    float deltaAngle = kneeAngle - currentKneeAngle;
    result.LowerRotation = glm::angleAxis(-deltaAngle, lowerAxis);

    // End effector rotation (maintain current or match target)
    result.EndRotation = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);

    result.Solved = result.ReachFraction >= 0.99f;
    return result;
}

//=============================================================================
// Look-At Solver
//=============================================================================

Math::Quat IKSystem::SolveLookAt(
    const Math::Vec3& bonePos,
    const Math::Vec3& boneForward,
    const Math::Vec3& targetPos,
    float minAngle,
    float maxAngle) 
{
    Math::Vec3 toTarget = targetPos - bonePos;
    float dist = glm::length(toTarget);
    
    if (dist < 0.001f) {
        return Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    Math::Vec3 targetDir = toTarget / dist;
    
    // Calculate angle between current and target
    float dotProduct = std::clamp(glm::dot(boneForward, targetDir), -1.0f, 1.0f);
    float angle = std::acos(dotProduct);
    
    // Clamp to constraints
    angle = std::clamp(angle, minAngle, maxAngle);
    
    // Calculate rotation axis
    Math::Vec3 axis = glm::cross(boneForward, targetDir);
    if (glm::length(axis) < 0.001f) {
        // Vectors are parallel
        return Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    axis = glm::normalize(axis);
    
    return glm::angleAxis(angle, axis);
}

//=============================================================================
// Foot IK Processing
//=============================================================================

void IKSystem::ProcessFootIK(IKComponent& ik,
                              SkeletalMeshComponent& skeletal,
                              const TransformComponent& transform,
                              float deltaTime) {
    if (!m_PhysicsWorld) return;

    const auto& settings = ik.FootSettings;
    
    // Get world transform matrix
    Math::Mat4 worldMatrix = transform.GetWorldMatrix();
    
    // Process each leg chain
    float lowestFootDrop = 0.0f;
    
    for (size_t i = 0; i < ik.Chains.size(); ++i) {
        auto& chain = ik.Chains[i];
        auto& state = ik.ChainStates[i];
        
        // Only process foot chains (leg IK)
        if (chain.Type != IKChainType::TwoBone) continue;
        if (chain.BoneIndices.size() < 3) continue;
        
        // Check if this is a foot chain (has "foot" in name, case insensitive)
        std::string lowerName = chain.Name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
        if (lowerName.find("foot") == std::string::npos && 
            lowerName.find("leg") == std::string::npos) {
            continue;
        }

        int32_t footBoneIndex = chain.BoneIndices[2];  // End effector
        if (footBoneIndex < 0) continue;
        
        // Get foot position in world space
        Math::Vec3 footLocalPos = skeletal.CurrentPose.LocalPoses[footBoneIndex].Translation;
        Math::Vec4 footWorld4 = worldMatrix * Math::Vec4(footLocalPos, 1.0f);
        Math::Vec3 footPos = Math::Vec3(footWorld4);
        
        // Raycast from above foot to below
        Math::Vec3 hitPos, hitNormal;
        bool hit = RaycastGround(footPos, settings.RaycastHeight, settings.RaycastDepth,
                                  hitPos, hitNormal);
        m_Statistics.RaycastsPerformed++;
        
        state.IsGrounded = hit;
        
        if (hit) {
            state.GroundPosition = hitPos;
            state.GroundNormal = hitNormal;
            
            // Calculate foot adjustment
            float targetHeight = hitPos.y + settings.FootHeight;
            float footDrop = footPos.y - targetHeight;
            
            // Clamp adjustment
            footDrop = std::clamp(footDrop, -settings.MaxFootAdjustment, 
                                   settings.MaxFootAdjustment);
            
            // Track lowest foot drop for hip adjustment
            lowestFootDrop = std::min(lowestFootDrop, footDrop);
            
            // Smooth target position
            Math::Vec3 targetFootPos = footPos;
            targetFootPos.y = targetHeight;
            
            // Smoothly interpolate
            static Math::Vec3 footVelocity{0.0f};
            state.SmoothedPosition = SmoothDampVec3(state.SmoothedPosition, targetFootPos,
                                                      footVelocity, 1.0f / settings.BlendSpeed,
                                                      deltaTime);
            
            // Set IK target
            state.CurrentTarget.Position = state.SmoothedPosition;
            state.CurrentTarget.UsePosition = true;
            state.TargetWeight = 1.0f;
            
            // Ground slope alignment
            if (settings.AlignToSlope) {
                float slopeAngle = std::acos(std::clamp(
                    glm::dot(hitNormal, Math::Vec3(0.0f, 1.0f, 0.0f)), 
                    -1.0f, 1.0f));
                
                if (slopeAngle < settings.MaxSlopeAngle) {
                    // Calculate rotation to align foot with slope
                    Math::Vec3 forward = glm::normalize(
                        glm::cross(Math::Vec3(1.0f, 0.0f, 0.0f), hitNormal));
                    Math::Vec3 right = glm::cross(hitNormal, forward);
                    
                    Math::Mat3 slopeRot(right, hitNormal, forward);
                    Math::Quat slopeQuat = glm::quat_cast(slopeRot);
                    
                    state.CurrentTarget.Rotation = glm::slerp(
                        Math::Quat(1.0f, 0.0f, 0.0f, 0.0f),
                        slopeQuat,
                        settings.SlopeAlignmentWeight);
                    state.CurrentTarget.UseRotation = true;
                }
            }
        } else {
            // No ground - blend IK out
            state.TargetWeight = 0.0f;
        }
    }
    
    // Hip adjustment
    if (settings.AdjustHips && ik.HipBoneIndex >= 0) {
        ik.TargetHipOffset.y = std::min(0.0f, lowestFootDrop);
        ik.TargetHipOffset.y = std::max(ik.TargetHipOffset.y, -settings.MaxHipAdjustment);
        
        // Smooth hip offset
        static Math::Vec3 hipVelocity{0.0f};
        ik.HipOffset = SmoothDampVec3(ik.HipOffset, ik.TargetHipOffset,
                                       hipVelocity, 1.0f / settings.HipBlendSpeed, deltaTime);
        
        // Apply to hip bone
        auto& hipPose = skeletal.CurrentPose.LocalPoses[ik.HipBoneIndex];
        hipPose.Translation += ik.HipOffset;
    }
}

bool IKSystem::RaycastGround(const Math::Vec3& footPos,
                              float rayHeight,
                              float rayDepth,
                              Math::Vec3& outHitPos,
                              Math::Vec3& outHitNormal) {
    if (!m_PhysicsWorld || !m_PhysicsWorld->IsInitialized()) {
        return false;
    }

    JPH::PhysicsSystem& system = m_PhysicsWorld->GetPhysicsSystem();
    const JPH::NarrowPhaseQuery& query = system.GetNarrowPhaseQuery();

    // Ray from above foot pointing down
    Math::Vec3 rayStart = footPos + Math::Vec3(0.0f, rayHeight, 0.0f);
    Math::Vec3 rayDir = Math::Vec3(0.0f, -1.0f, 0.0f);
    float rayLength = rayHeight + rayDepth;

    JPH::RRayCast ray(
        JPH::RVec3(rayStart.x, rayStart.y, rayStart.z),
        JPH::Vec3(rayDir.x, rayDir.y, rayDir.z) * rayLength
    );

    JPH::RayCastSettings settings;
    settings.mBackFaceMode = JPH::EBackFaceMode::IgnoreBackFaces;

    JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;
    
    // Use default filters - hit static and dynamic objects
    query.CastRay(ray, settings, collector);

    if (collector.HadHit()) {
        // Calculate hit position
        float hitFraction = collector.mHit.mFraction;
        Math::Vec3 hitPoint = rayStart + rayDir * rayLength * hitFraction;
        
        outHitPos = hitPoint;
        
        // Get surface normal
        // Note: For more accurate normals, you'd need to query the body/shape
        outHitNormal = Math::Vec3(0.0f, 1.0f, 0.0f);  // Default to up
        
        return true;
    }

    return false;
}

//=============================================================================
// Chain Processing
//=============================================================================

void IKSystem::ProcessChain(const IKChainDefinition& chain,
                             IKChainState& state,
                             SkeletalMeshComponent& skeletal,
                             const TransformComponent& transform,
                             float deltaTime) {
    if (!chain.IsValid() || !state.CurrentTarget.IsActive()) return;
    
    (void)transform;  // Used for world space conversion
    (void)deltaTime;  // Used for smoothing

    switch (chain.Type) {
        case IKChainType::TwoBone: {
            if (chain.BoneIndices.size() < 3) return;
            
            int32_t upperIdx = chain.BoneIndices[0];
            int32_t lowerIdx = chain.BoneIndices[1];
            int32_t endIdx = chain.BoneIndices[2];
            
            if (upperIdx < 0 || lowerIdx < 0 || endIdx < 0) return;
            
            auto& pose = skeletal.CurrentPose;
            if (static_cast<size_t>(endIdx) >= pose.LocalPoses.size()) return;
            
            // Get current bone positions (from global poses)
            Math::Vec3 upperPos = Math::Vec3(pose.GlobalPoses[upperIdx][3]);
            Math::Vec3 lowerPos = Math::Vec3(pose.GlobalPoses[lowerIdx][3]);
            Math::Vec3 endPos = Math::Vec3(pose.GlobalPoses[endIdx][3]);
            
            // Calculate bone lengths
            float upperLength = glm::length(lowerPos - upperPos);
            float lowerLength = glm::length(endPos - lowerPos);
            
            // Get pole vector
            Math::Vec3 poleVector = state.CurrentTarget.HintVector;
            if (state.CurrentTarget.HintType == IKHintType::Forward) {
                poleVector = Math::Vec3(0.0f, 0.0f, 1.0f);
            } else if (state.CurrentTarget.HintType == IKHintType::Backward) {
                poleVector = Math::Vec3(0.0f, 0.0f, -1.0f);
            }
            
            // Solve IK
            TwoBoneIKResult result = SolveTwoBoneIK(
                upperPos, lowerPos, endPos,
                state.CurrentTarget.Position,
                poleVector,
                upperLength, lowerLength,
                chain.MinAngle, chain.MaxAngle);
            
            // Apply result with blending
            float weight = state.CurrentWeight * state.CurrentTarget.Weight;
            ApplyIKToPose(result, chain, pose, weight);
            
            if (result.Solved) {
                m_Statistics.SuccessfulSolves++;
            }
            break;
        }
        
        case IKChainType::LookAt: {
            if (chain.BoneIndices.empty()) return;
            
            int32_t boneIdx = chain.BoneIndices[0];
            if (boneIdx < 0) return;
            
            auto& pose = skeletal.CurrentPose;
            if (static_cast<size_t>(boneIdx) >= pose.LocalPoses.size()) return;
            
            Math::Vec3 bonePos = Math::Vec3(pose.GlobalPoses[boneIdx][3]);
            Math::Vec3 boneForward = Math::Vec3(pose.GlobalPoses[boneIdx][2]);
            
            Math::Quat lookAtRot = SolveLookAt(
                bonePos, boneForward,
                state.CurrentTarget.Position,
                chain.MinAngle, chain.MaxAngle);
            
            // Blend with current rotation
            float weight = state.CurrentWeight * state.CurrentTarget.Weight;
            pose.LocalPoses[boneIdx].Rotation = glm::slerp(
                pose.LocalPoses[boneIdx].Rotation,
                lookAtRot * pose.LocalPoses[boneIdx].Rotation,
                weight);
            break;
        }
        
        default:
            // FABRIK and SplineIK not implemented yet
            break;
    }
}

void IKSystem::ApplyIKToPose(const TwoBoneIKResult& result,
                              const IKChainDefinition& chain,
                              SkeletonPose& pose,
                              float weight) {
    if (weight <= 0.001f) return;
    if (chain.BoneIndices.size() < 3) return;
    
    int32_t upperIdx = chain.BoneIndices[0];
    int32_t lowerIdx = chain.BoneIndices[1];
    int32_t endIdx = chain.BoneIndices[2];
    
    // Blend rotations
    auto& upperPose = pose.LocalPoses[upperIdx];
    auto& lowerPose = pose.LocalPoses[lowerIdx];
    auto& endPose = pose.LocalPoses[endIdx];
    
    upperPose.Rotation = glm::slerp(upperPose.Rotation, 
                                     result.UpperRotation * upperPose.Rotation, 
                                     weight);
    lowerPose.Rotation = glm::slerp(lowerPose.Rotation,
                                     result.LowerRotation * lowerPose.Rotation,
                                     weight);
    
    // End effector rotation (optional)
    endPose.Rotation = glm::slerp(endPose.Rotation,
                                   result.EndRotation * endPose.Rotation,
                                   weight);
}

//=============================================================================
// Utility Functions
//=============================================================================

float IKSystem::SmoothDamp(float current, float target, float& velocity,
                            float smoothTime, float deltaTime) {
    // Based on Game Programming Gems 4 smooth damping
    smoothTime = std::max(0.0001f, smoothTime);
    float omega = 2.0f / smoothTime;
    float x = omega * deltaTime;
    float exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float change = current - target;
    float temp = (velocity + omega * change) * deltaTime;
    velocity = (velocity - omega * temp) * exp;
    return target + (change + temp) * exp;
}

Math::Vec3 IKSystem::SmoothDampVec3(const Math::Vec3& current,
                                     const Math::Vec3& target,
                                     Math::Vec3& velocity,
                                     float smoothTime,
                                     float deltaTime) {
    return Math::Vec3(
        SmoothDamp(current.x, target.x, velocity.x, smoothTime, deltaTime),
        SmoothDamp(current.y, target.y, velocity.y, smoothTime, deltaTime),
        SmoothDamp(current.z, target.z, velocity.z, smoothTime, deltaTime)
    );
}

} // namespace ECS
} // namespace Core
