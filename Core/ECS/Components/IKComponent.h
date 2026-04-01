#pragma once

// ============================================================================
// IKComponent.h
// Inverse Kinematics component for procedural animation adjustment
// 
// Features:
// - Two-bone IK chains (legs, arms)
// - Foot placement IK with terrain queries
// - Look-at/aim IK constraints
// - Blendable IK weights
// ============================================================================

#include "Core/Math/Math.h"
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>

namespace Core {
namespace ECS {

    // ========================================================================
    // IK Chain Types
    // ========================================================================

    /**
     * @brief Types of IK chains supported
     */
    enum class IKChainType : uint8_t {
        TwoBone = 0,    ///< Standard two-bone IK (arm, leg)
        LookAt,         ///< Single bone look-at constraint
        FABRIK,         ///< Forward And Backward Reaching IK (multi-bone)
        SplineIK        ///< Spline-based IK for tails/tentacles
    };

    /**
     * @brief Hint direction for IK solver (pole vector)
     */
    enum class IKHintType : uint8_t {
        None = 0,       ///< No hint, solver chooses direction
        Forward,        ///< Hint towards forward direction
        Backward,       ///< Hint towards backward direction
        World,          ///< Use world-space hint vector
        Local           ///< Use local-space hint vector
    };

    // ========================================================================
    // IK Target - Where an IK chain should reach
    // ========================================================================

    /**
     * @brief Target specification for an IK chain
     */
    struct IKTarget {
        Math::Vec3 Position{0.0f};          ///< Target position in world space
        Math::Quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};  ///< Target rotation (for end effector)
        
        bool UsePosition = true;             ///< Apply position IK
        bool UseRotation = false;            ///< Apply rotation to end effector
        
        float Weight = 1.0f;                 ///< Blend weight (0 = animation, 1 = IK)
        float PositionWeight = 1.0f;         ///< Position blend weight
        float RotationWeight = 1.0f;         ///< Rotation blend weight

        // Hint/pole vector for controlling bend direction
        IKHintType HintType = IKHintType::None;
        Math::Vec3 HintVector{0.0f, 0.0f, 1.0f};  ///< Pole vector direction
        float HintWeight = 1.0f;

        // Target limits
        float MaxReach = -1.0f;              ///< Maximum reach distance (-1 = unlimited)

        IKTarget() = default;

        static IKTarget FromPosition(const Math::Vec3& pos, float weight = 1.0f) {
            IKTarget target;
            target.Position = pos;
            target.Weight = weight;
            return target;
        }

        static IKTarget FromPositionRotation(const Math::Vec3& pos, 
                                              const Math::Quat& rot,
                                              float weight = 1.0f) {
            IKTarget target;
            target.Position = pos;
            target.Rotation = rot;
            target.UseRotation = true;
            target.Weight = weight;
            return target;
        }

        bool IsActive() const { return Weight > 0.001f; }
    };

    // ========================================================================
    // IK Chain Definition - Bones in the chain
    // ========================================================================

    /**
     * @brief Definition of an IK chain
     */
    struct IKChainDefinition {
        std::string Name;                    ///< Chain identifier (e.g., "LeftLeg", "RightArm")
        IKChainType Type = IKChainType::TwoBone;
        
        // Bone names in chain (from root to tip)
        // For TwoBone: [upper, lower, end] e.g., ["LeftUpperLeg", "LeftLowerLeg", "LeftFoot"]
        // For LookAt: [bone] e.g., ["Head"]
        std::vector<std::string> BoneNames;
        
        // Cached bone indices (populated at runtime)
        std::vector<int32_t> BoneIndices;
        
        // Chain constraints
        float MinAngle = 0.0f;               ///< Minimum bend angle (radians)
        float MaxAngle = 3.14159f;           ///< Maximum bend angle (radians)
        bool AllowStretching = false;        ///< Allow bones to stretch beyond natural length
        float StretchLimit = 1.0f;           ///< Maximum stretch factor (1 = no stretch)
        
        // Twist axis for proper rotation
        Math::Vec3 TwistAxis{0.0f, 1.0f, 0.0f};  ///< Local twist axis
        
        IKChainDefinition() = default;
        
        IKChainDefinition(const std::string& name, IKChainType type)
            : Name(name), Type(type) {}

        /**
         * @brief Create a two-bone IK chain for leg
         */
        static IKChainDefinition CreateLegChain(const std::string& name,
                                                 const std::string& upperLeg,
                                                 const std::string& lowerLeg,
                                                 const std::string& foot) {
            IKChainDefinition chain(name, IKChainType::TwoBone);
            chain.BoneNames = {upperLeg, lowerLeg, foot};
            chain.MinAngle = 0.01f;  // Slight bend minimum to avoid singularity
            chain.MaxAngle = 2.5f;   // ~143 degrees max knee bend
            return chain;
        }

        /**
         * @brief Create a two-bone IK chain for arm
         */
        static IKChainDefinition CreateArmChain(const std::string& name,
                                                 const std::string& upperArm,
                                                 const std::string& lowerArm,
                                                 const std::string& hand) {
            IKChainDefinition chain(name, IKChainType::TwoBone);
            chain.BoneNames = {upperArm, lowerArm, hand};
            chain.MinAngle = 0.01f;
            chain.MaxAngle = 2.7f;   // ~155 degrees max elbow bend
            return chain;
        }

        /**
         * @brief Create a look-at constraint for head
         */
        static IKChainDefinition CreateLookAtChain(const std::string& name,
                                                    const std::string& bone) {
            IKChainDefinition chain(name, IKChainType::LookAt);
            chain.BoneNames = {bone};
            chain.MinAngle = -1.0f;  // About -57 degrees
            chain.MaxAngle = 1.0f;   // About 57 degrees
            return chain;
        }

        bool IsValid() const {
            switch (Type) {
                case IKChainType::TwoBone:
                    return BoneNames.size() >= 3;
                case IKChainType::LookAt:
                    return BoneNames.size() >= 1;
                case IKChainType::FABRIK:
                    return BoneNames.size() >= 2;
                default:
                    return false;
            }
        }
    };

    // ========================================================================
    // Foot IK Settings - For ground alignment
    // ========================================================================

    /**
     * @brief Settings for procedural foot placement
     */
    struct FootIKSettings {
        bool Enabled = true;
        
        // Raycast settings
        float RaycastHeight = 0.5f;          ///< Height above foot to start raycast
        float RaycastDepth = 0.8f;           ///< Distance below foot to cast ray
        float FootHeight = 0.1f;             ///< Height of foot off ground
        
        // Blend settings
        float BlendSpeed = 10.0f;            ///< IK blend smoothing speed
        float MaxFootAdjustment = 0.4f;      ///< Maximum vertical foot adjustment
        
        // Hip adjustment
        bool AdjustHips = true;              ///< Lower hips when feet on uneven terrain
        float MaxHipAdjustment = 0.3f;       ///< Maximum hip lowering amount
        float HipBlendSpeed = 8.0f;          ///< Hip adjustment smoothing speed
        
        // Ground normal alignment
        bool AlignToSlope = true;            ///< Rotate foot to match ground normal
        float SlopeAlignmentWeight = 0.8f;   ///< How much to align to ground (0-1)
        float MaxSlopeAngle = 0.7f;          ///< Max slope angle in radians (~40 degrees)

        FootIKSettings() = default;
    };

    // ========================================================================
    // IK Runtime State - Per-chain runtime data
    // ========================================================================

    /**
     * @brief Runtime state for a single IK chain
     */
    struct IKChainState {
        IKTarget CurrentTarget;              ///< Current active target
        IKTarget PreviousTarget;             ///< For interpolation
        
        // Blending state
        float CurrentWeight = 0.0f;          ///< Current blend weight
        float TargetWeight = 0.0f;           ///< Weight we're blending towards
        
        // Foot IK specific state
        Math::Vec3 GroundPosition{0.0f};     ///< Last ground hit position
        Math::Vec3 GroundNormal{0.0f, 1.0f, 0.0f};  ///< Ground surface normal
        bool IsGrounded = false;             ///< Whether foot raycast hit ground
        float GroundDistance = 0.0f;         ///< Distance to ground
        
        // Smoothed values
        Math::Vec3 SmoothedPosition{0.0f};
        Math::Quat SmoothedRotation{1.0f, 0.0f, 0.0f, 0.0f};
        
        void Reset() {
            CurrentWeight = 0.0f;
            TargetWeight = 0.0f;
            IsGrounded = false;
            GroundDistance = 0.0f;
        }
    };

    // ========================================================================
    // IK Component - Main component for entity IK
    // ========================================================================

    /**
     * @brief ECS component for Inverse Kinematics
     * 
     * Attach to entities with SkeletalMeshComponent to enable procedural
     * IK adjustments on top of animation.
     * 
     * Usage:
     * @code
     * // Create IK component with foot placement
     * auto ik = IKComponent::CreateBipedFootIK();
     * 
     * // Add custom arm IK
     * ik.AddChain(IKChainDefinition::CreateArmChain("RightArm", 
     *     "RightUpperArm", "RightForearm", "RightHand"));
     * 
     * // Set IK target
     * ik.SetTarget("RightArm", IKTarget::FromPosition(targetPos, 0.8f));
     * @endcode
     */
    struct IKComponent {
        // IK chain definitions
        std::vector<IKChainDefinition> Chains;
        
        // Runtime state per chain
        std::vector<IKChainState> ChainStates;
        
        // Foot IK settings
        FootIKSettings FootSettings;
        
        // Global IK settings
        bool Enabled = true;
        float GlobalWeight = 1.0f;           ///< Master blend weight for all IK
        
        // Hip adjustment state (for foot IK)
        Math::Vec3 HipOffset{0.0f};          ///< Current hip position offset
        Math::Vec3 TargetHipOffset{0.0f};    ///< Target hip offset
        std::string HipBoneName = "Hips";    ///< Name of hip/pelvis bone
        int32_t HipBoneIndex = -1;           ///< Cached hip bone index

        // Initialization flag
        bool IsInitialized = false;

        // ====================================================================
        // Constructors
        // ====================================================================

        IKComponent() = default;

        // ====================================================================
        // Chain Management
        // ====================================================================

        /**
         * @brief Add an IK chain
         */
        void AddChain(const IKChainDefinition& chain) {
            Chains.push_back(chain);
            ChainStates.emplace_back();
        }

        /**
         * @brief Get chain by name
         * @return Pointer to chain or nullptr
         */
        IKChainDefinition* GetChain(const std::string& name) {
            for (auto& chain : Chains) {
                if (chain.Name == name) return &chain;
            }
            return nullptr;
        }

        const IKChainDefinition* GetChain(const std::string& name) const {
            for (const auto& chain : Chains) {
                if (chain.Name == name) return &chain;
            }
            return nullptr;
        }

        /**
         * @brief Get chain index by name
         * @return Index or -1 if not found
         */
        int32_t GetChainIndex(const std::string& name) const {
            for (size_t i = 0; i < Chains.size(); ++i) {
                if (Chains[i].Name == name) return static_cast<int32_t>(i);
            }
            return -1;
        }

        /**
         * @brief Get runtime state for a chain
         */
        IKChainState* GetChainState(const std::string& name) {
            int32_t index = GetChainIndex(name);
            if (index >= 0 && index < static_cast<int32_t>(ChainStates.size())) {
                return &ChainStates[index];
            }
            return nullptr;
        }

        // ====================================================================
        // Target Setting
        // ====================================================================

        /**
         * @brief Set IK target for a chain
         * @param chainName Name of the chain
         * @param target Target position/rotation
         * @return true if chain was found
         */
        bool SetTarget(const std::string& chainName, const IKTarget& target) {
            IKChainState* state = GetChainState(chainName);
            if (state) {
                state->PreviousTarget = state->CurrentTarget;
                state->CurrentTarget = target;
                state->TargetWeight = target.Weight;
                return true;
            }
            return false;
        }

        /**
         * @brief Set IK target position for a chain
         */
        bool SetTargetPosition(const std::string& chainName, 
                                const Math::Vec3& position,
                                float weight = 1.0f) {
            return SetTarget(chainName, IKTarget::FromPosition(position, weight));
        }

        /**
         * @brief Set chain weight (for blending IK on/off)
         */
        bool SetChainWeight(const std::string& chainName, float weight) {
            IKChainState* state = GetChainState(chainName);
            if (state) {
                state->TargetWeight = std::clamp(weight, 0.0f, 1.0f);
                return true;
            }
            return false;
        }

        /**
         * @brief Clear IK target (return to animation)
         */
        bool ClearTarget(const std::string& chainName) {
            IKChainState* state = GetChainState(chainName);
            if (state) {
                state->TargetWeight = 0.0f;
                return true;
            }
            return false;
        }

        // ====================================================================
        // Factory Methods
        // ====================================================================

        /**
         * @brief Create IK component with standard biped foot IK
         */
        static IKComponent CreateBipedFootIK(
            const std::string& leftUpperLeg = "LeftUpLeg",
            const std::string& leftLowerLeg = "LeftLeg",
            const std::string& leftFoot = "LeftFoot",
            const std::string& rightUpperLeg = "RightUpLeg",
            const std::string& rightLowerLeg = "RightLeg",
            const std::string& rightFoot = "RightFoot") 
        {
            IKComponent ik;
            
            // Left leg
            ik.AddChain(IKChainDefinition::CreateLegChain("LeftFoot",
                leftUpperLeg, leftLowerLeg, leftFoot));
            
            // Right leg
            ik.AddChain(IKChainDefinition::CreateLegChain("RightFoot",
                rightUpperLeg, rightLowerLeg, rightFoot));
            
            ik.FootSettings.Enabled = true;
            return ik;
        }

        /**
         * @brief Create IK component with full humanoid chains
         */
        static IKComponent CreateHumanoidIK(
            const std::string& leftUpperLeg = "LeftUpLeg",
            const std::string& leftLowerLeg = "LeftLeg",
            const std::string& leftFoot = "LeftFoot",
            const std::string& rightUpperLeg = "RightUpLeg",
            const std::string& rightLowerLeg = "RightLeg",
            const std::string& rightFoot = "RightFoot",
            const std::string& leftUpperArm = "LeftArm",
            const std::string& leftLowerArm = "LeftForeArm",
            const std::string& leftHand = "LeftHand",
            const std::string& rightUpperArm = "RightArm",
            const std::string& rightLowerArm = "RightForeArm",
            const std::string& rightHand = "RightHand",
            const std::string& head = "Head")
        {
            IKComponent ik;
            
            // Legs
            ik.AddChain(IKChainDefinition::CreateLegChain("LeftFoot",
                leftUpperLeg, leftLowerLeg, leftFoot));
            ik.AddChain(IKChainDefinition::CreateLegChain("RightFoot",
                rightUpperLeg, rightLowerLeg, rightFoot));
            
            // Arms
            ik.AddChain(IKChainDefinition::CreateArmChain("LeftHand",
                leftUpperArm, leftLowerArm, leftHand));
            ik.AddChain(IKChainDefinition::CreateArmChain("RightHand",
                rightUpperArm, rightLowerArm, rightHand));
            
            // Head
            ik.AddChain(IKChainDefinition::CreateLookAtChain("Head", head));
            
            ik.FootSettings.Enabled = true;
            return ik;
        }

        // ====================================================================
        // Utility
        // ====================================================================

        /**
         * @brief Check if component has any active IK
         */
        bool HasActiveIK() const {
            if (!Enabled || GlobalWeight <= 0.001f) return false;
            
            for (const auto& state : ChainStates) {
                if (state.CurrentWeight > 0.001f || state.TargetWeight > 0.001f) {
                    return true;
                }
            }
            return false;
        }

        /**
         * @brief Reset all IK state
         */
        void Reset() {
            for (auto& state : ChainStates) {
                state.Reset();
            }
            HipOffset = Math::Vec3(0.0f);
            TargetHipOffset = Math::Vec3(0.0f);
            IsInitialized = false;
        }

        /**
         * @brief Get total number of chains
         */
        size_t GetChainCount() const { return Chains.size(); }
    };

} // namespace ECS
} // namespace Core
