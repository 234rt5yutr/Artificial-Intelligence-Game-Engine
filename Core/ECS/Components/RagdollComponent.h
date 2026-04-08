#pragma once

#include "Core/Physics/Ragdoll/RagdollDefinition.h"
#include "Core/Math/Math.h"

#include <memory>
#include <vector>
#include <unordered_map>
#include <cstdint>

// Forward declarations for Jolt Physics types
namespace JPH
{
    class Constraint;
    class BodyID;
} // namespace JPH

// Include BodyID for actual usage (forward declaration alone isn't sufficient for value types)
#include <Jolt/Physics/Body/BodyID.h>

namespace Core {
namespace ECS {

    // =============================================================================
    // RagdollState Enumeration
    // =============================================================================

    /**
     * @brief Represents the current state of a ragdoll simulation.
     * 
     * The ragdoll lifecycle transitions through these states:
     *   Inactive -> BlendingIn -> Active -> BlendingOut -> Inactive/Frozen
     */
    enum class RagdollState : uint8_t
    {
        Inactive,       ///< Ragdoll is not active, animation drives the skeleton
        BlendingIn,     ///< Transitioning from animation to physics simulation
        Active,         ///< Fully physics-driven ragdoll
        BlendingOut,    ///< Transitioning from physics back to animation
        Frozen          ///< Ragdoll is frozen in place (no physics updates)
    };

    // =============================================================================
    // RagdollComponent
    // =============================================================================

    /**
     * @brief ECS component for ragdoll physics simulation.
     * 
     * This component manages the state and runtime data for ragdoll physics,
     * including body references, constraints, bone mappings, and blend states.
     * It supports both full-body and partial ragdoll configurations.
     * 
     * @note The actual physics bodies are created and managed by the RagdollSystem.
     *       This component holds references and state information.
     * 
     * Usage:
     * @code
     *     auto& ragdoll = entity.AddComponent<RagdollComponent>();
     *     ragdoll.Definition = ragdollAsset;
     *     ragdoll.Activate(impulse, hitPoint);
     * @endcode
     */
    struct RagdollComponent
    {
        // -------------------------------------------------------------------------
        // Definition & Configuration
        // -------------------------------------------------------------------------

        /** @brief Shared pointer to the ragdoll definition asset. */
        std::shared_ptr<Physics::RagdollDefinition> Definition = nullptr;

        /** @brief Current state of the ragdoll simulation. */
        RagdollState State = RagdollState::Inactive;

        // -------------------------------------------------------------------------
        // Blend State
        // -------------------------------------------------------------------------

        /** @brief Current blend timer (0 to BlendDuration). */
        float BlendTimer = 0.0f;

        /** @brief Duration of blend transitions in seconds. */
        float BlendDuration = 0.3f;

        // -------------------------------------------------------------------------
        // Physics Runtime Data
        // -------------------------------------------------------------------------

        /** @brief Body IDs for each ragdoll bone in the physics world. */
        std::vector<JPH::BodyID> BodyIDs;

        /** @brief Constraint pointers connecting ragdoll bodies. */
        std::vector<JPH::Constraint*> Constraints;

        /** @brief Maps skeleton bone indices to BodyIDs vector indices. */
        std::unordered_map<int32_t, size_t> BoneToBodyMap;

        // -------------------------------------------------------------------------
        // Partial Ragdoll Configuration
        // -------------------------------------------------------------------------

        /** 
         * @brief Mask indicating which bones are affected by ragdoll physics.
         * 
         * When empty, all bones are ragdolled. When populated, only bones
         * with corresponding true values are physics-driven (partial ragdoll).
         */
        std::vector<bool> BoneMask;

        // -------------------------------------------------------------------------
        // Activation Parameters
        // -------------------------------------------------------------------------

        /** @brief Initial impulse to apply when ragdoll activates. */
        Math::Vec3 InitialImpulse{ 0.0f, 0.0f, 0.0f };

        /** @brief World-space point where the impact occurred. */
        Math::Vec3 ImpactPoint{ 0.0f, 0.0f, 0.0f };

        // -------------------------------------------------------------------------
        // Lifetime Management
        // -------------------------------------------------------------------------

        /** 
         * @brief Remaining lifetime in seconds. 
         * 
         * A value of -1.0f indicates infinite lifetime (no automatic timeout).
         */
        float LifetimeRemaining = -1.0f;

        /** @brief If true, entity is destroyed when lifetime expires. */
        bool DestroyOnTimeout = false;

        // -------------------------------------------------------------------------
        // Internal State
        // -------------------------------------------------------------------------

        /** @brief Indicates physics bodies have been created. */
        bool BodiesCreated = false;

        // -------------------------------------------------------------------------
        // Constructors
        // -------------------------------------------------------------------------

        RagdollComponent() = default;

        explicit RagdollComponent(std::shared_ptr<Physics::RagdollDefinition> definition)
            : Definition(std::move(definition))
        {
        }

        // -------------------------------------------------------------------------
        // State Query Methods
        // -------------------------------------------------------------------------

        /**
         * @brief Checks if the ragdoll is in any active state.
         * @return true if state is Active, BlendingIn, or BlendingOut.
         */
        [[nodiscard]] bool IsActive() const noexcept
        {
            return State == RagdollState::Active ||
                   State == RagdollState::BlendingIn ||
                   State == RagdollState::BlendingOut;
        }

        /**
         * @brief Checks if the ragdoll is fully physics-driven.
         * @return true only if state is exactly Active.
         */
        [[nodiscard]] bool IsFullyActive() const noexcept
        {
            return State == RagdollState::Active;
        }

        /**
         * @brief Checks if the ragdoll is currently blending.
         * @return true if state is BlendingIn or BlendingOut.
         */
        [[nodiscard]] bool IsBlending() const noexcept
        {
            return State == RagdollState::BlendingIn ||
                   State == RagdollState::BlendingOut;
        }

        /**
         * @brief Checks if the ragdoll is frozen in place.
         * @return true if state is Frozen.
         */
        [[nodiscard]] bool IsFrozen() const noexcept
        {
            return State == RagdollState::Frozen;
        }

        /**
         * @brief Gets the current blend weight (0 = animation, 1 = physics).
         * @return Normalized blend weight based on current state and timer.
         */
        [[nodiscard]] float GetBlendWeight() const noexcept
        {
            if (BlendDuration <= 0.0f) return (State == RagdollState::Active) ? 1.0f : 0.0f;

            switch (State)
            {
                case RagdollState::BlendingIn:
                    return BlendTimer / BlendDuration;
                case RagdollState::BlendingOut:
                    return 1.0f - (BlendTimer / BlendDuration);
                case RagdollState::Active:
                case RagdollState::Frozen:
                    return 1.0f;
                default:
                    return 0.0f;
            }
        }

        // -------------------------------------------------------------------------
        // Activation Methods
        // -------------------------------------------------------------------------

        /**
         * @brief Activates the ragdoll with an initial impulse.
         * 
         * Transitions the ragdoll from Inactive/Frozen to BlendingIn state.
         * The RagdollSystem will create physics bodies and apply the impulse.
         * 
         * @param impulse Initial linear impulse to apply at the impact point.
         * @param impactPoint World-space position of the impact.
         */
        void Activate(const Math::Vec3& impulse, const Math::Vec3& impactPoint)
        {
            if (State == RagdollState::Active || State == RagdollState::BlendingIn)
            {
                return; // Already activating or active
            }

            InitialImpulse = impulse;
            ImpactPoint = impactPoint;
            BlendTimer = 0.0f;
            State = RagdollState::BlendingIn;
        }

        /**
         * @brief Activates the ragdoll without an initial impulse.
         * 
         * Useful for scenarios like character death without external force.
         */
        void Activate()
        {
            Activate(Math::Vec3{ 0.0f, 0.0f, 0.0f }, Math::Vec3{ 0.0f, 0.0f, 0.0f });
        }

        /**
         * @brief Begins deactivating the ragdoll.
         * 
         * Transitions from Active to BlendingOut state. After the blend
         * completes, the state will transition to Inactive.
         */
        void Deactivate()
        {
            if (State != RagdollState::Active && State != RagdollState::BlendingIn)
            {
                return; // Not in an active state
            }

            BlendTimer = 0.0f;
            State = RagdollState::BlendingOut;
        }

        /**
         * @brief Immediately freezes the ragdoll in its current pose.
         * 
         * Physics bodies become kinematic and stop simulating, but
         * maintain their current positions. Useful for death poses.
         */
        void Freeze()
        {
            if (State == RagdollState::Active || State == RagdollState::BlendingIn)
            {
                State = RagdollState::Frozen;
            }
        }

        // -------------------------------------------------------------------------
        // Partial Ragdoll Configuration
        // -------------------------------------------------------------------------

        /**
         * @brief Configures partial ragdoll for specific bones.
         * 
         * Only the specified bones will be physics-driven while other
         * bones continue to follow animation. Useful for hit reactions
         * affecting only specific body parts.
         * 
         * @param boneIndices Vector of skeleton bone indices to ragdoll.
         */
        void SetPartialRagdoll(const std::vector<int32_t>& boneIndices)
        {
            if (!Definition) return;

            // Resize mask if necessary - assumes we know the bone count
            // The RagdollSystem will validate against actual skeleton
            const size_t boneCount = Definition->GetBoneCount();
            BoneMask.assign(boneCount, false);

            for (int32_t index : boneIndices)
            {
                if (index >= 0 && static_cast<size_t>(index) < boneCount)
                {
                    BoneMask[static_cast<size_t>(index)] = true;
                }
            }
        }

        /**
         * @brief Clears partial ragdoll configuration.
         * 
         * After calling this, all bones will be affected by ragdoll
         * physics when the ragdoll is active.
         */
        void ClearPartialRagdoll()
        {
            BoneMask.clear();
        }

        /**
         * @brief Checks if a specific bone is ragdolled.
         * 
         * @param boneIndex The skeleton bone index to check.
         * @return true if the bone is affected by ragdoll physics.
         */
        [[nodiscard]] bool IsBoneRagdolled(int32_t boneIndex) const noexcept
        {
            // If no mask set, all bones are ragdolled
            if (BoneMask.empty()) return true;

            if (boneIndex < 0 || static_cast<size_t>(boneIndex) >= BoneMask.size())
            {
                return false;
            }

            return BoneMask[static_cast<size_t>(boneIndex)];
        }

        // -------------------------------------------------------------------------
        // Utility Methods
        // -------------------------------------------------------------------------

        /**
         * @brief Resets the component to its initial state.
         * 
         * Clears all runtime data but preserves the definition.
         * Physics bodies should be cleaned up by the RagdollSystem first.
         */
        void Reset()
        {
            State = RagdollState::Inactive;
            BlendTimer = 0.0f;
            BodyIDs.clear();
            Constraints.clear();
            BoneToBodyMap.clear();
            BoneMask.clear();
            InitialImpulse = Math::Vec3{ 0.0f, 0.0f, 0.0f };
            ImpactPoint = Math::Vec3{ 0.0f, 0.0f, 0.0f };
            LifetimeRemaining = -1.0f;
            BodiesCreated = false;
        }

        /**
         * @brief Checks if the ragdoll has a valid definition.
         * @return true if a definition is assigned.
         */
        [[nodiscard]] bool HasDefinition() const noexcept
        {
            return Definition != nullptr;
        }

        /**
         * @brief Gets the body ID for a specific bone.
         * 
         * @param boneIndex The skeleton bone index.
         * @return Pointer to the BodyID, or nullptr if not mapped.
         */
        [[nodiscard]] const JPH::BodyID* GetBodyForBone(int32_t boneIndex) const
        {
            auto it = BoneToBodyMap.find(boneIndex);
            if (it != BoneToBodyMap.end() && it->second < BodyIDs.size())
            {
                return &BodyIDs[it->second];
            }
            return nullptr;
        }
    };

} // namespace ECS
} // namespace Core
