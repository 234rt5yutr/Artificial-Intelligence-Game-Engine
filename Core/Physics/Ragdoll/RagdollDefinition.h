#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Core::Physics
{
    // =============================================================================
    // Math Type Aliases
    // =============================================================================
    
    namespace Math
    {
        using Vec3 = glm::vec3;
        using Quat = glm::quat;
    } // namespace Math

    // =============================================================================
    // Enumerations
    // =============================================================================

    /**
     * @brief Defines the type of physics collider used for a ragdoll bone.
     */
    enum class ColliderType : uint8_t
    {
        Capsule,    ///< Capsule collider - ideal for limbs
        Box,        ///< Box collider - suitable for torso/chest
        Sphere      ///< Sphere collider - good for joints/head
    };

    /**
     * @brief Defines the constraint type between parent and child bones.
     */
    enum class ConstraintType : uint8_t
    {
        Cone,       ///< Cone constraint - allows rotation within a cone
        Hinge,      ///< Hinge constraint - single axis rotation (elbows, knees)
        Fixed       ///< Fixed constraint - no relative movement allowed
    };

    /**
     * @brief Predefined ragdoll skeleton profiles for common creature types.
     */
    enum class RagdollProfile : uint8_t
    {
        Humanoid,   ///< Bipedal humanoid (head, torso, 2 arms, 2 legs)
        Quadruped,  ///< Four-legged creature (dogs, cats, horses)
        Biped,      ///< Generic two-legged creature
        Serpentine, ///< Snake-like chain of connected segments
        Custom      ///< User-defined bone configuration
    };

    // =============================================================================
    // RagdollBoneDefinition
    // =============================================================================

    /**
     * @brief Defines the physical properties and constraints of a single ragdoll bone.
     * 
     * Each bone definition maps to a skeleton bone and specifies its physics collider,
     * mass properties, and joint constraints relative to its parent bone.
     */
    struct RagdollBoneDefinition
    {
        // -------------------------------------------------------------------------
        // Skeleton Binding
        // -------------------------------------------------------------------------
        
        /** Index into the skeleton's bone array. -1 indicates unbound. */
        int32_t SkeletonBoneIndex = -1;
        
        /** Human-readable bone name for identification and debugging. */
        std::string BoneName;

        // -------------------------------------------------------------------------
        // Collider Configuration
        // -------------------------------------------------------------------------
        
        /** The geometric shape used for collision detection. */
        ColliderType ColliderType = ColliderType::Capsule;
        
        /** 
         * Dimensions of the collider:
         * - Capsule: (radius, height, unused)
         * - Box: (halfWidth, halfHeight, halfDepth)
         * - Sphere: (radius, unused, unused)
         */
        Math::Vec3 ColliderDimensions = Math::Vec3(0.05f, 0.2f, 0.05f);
        
        /** Local position offset from the bone's origin. */
        Math::Vec3 LocalOffset = Math::Vec3(0.0f);
        
        /** Local rotation offset from the bone's orientation. */
        Math::Quat LocalRotation = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f); // Identity quaternion (w, x, y, z)

        // -------------------------------------------------------------------------
        // Mass Properties
        // -------------------------------------------------------------------------
        
        /** Mass of the bone in kilograms. */
        float Mass = 1.0f;
        
        /** Surface friction coefficient [0, 1]. */
        float Friction = 0.5f;
        
        /** Bounciness/elasticity coefficient [0, 1]. */
        float Restitution = 0.1f;
        
        /** Linear velocity damping factor [0, 1]. Higher values slow movement faster. */
        float LinearDamping = 0.05f;
        
        /** Angular velocity damping factor [0, 1]. Higher values slow rotation faster. */
        float AngularDamping = 0.05f;

        // -------------------------------------------------------------------------
        // Joint Constraints (relative to parent bone)
        // -------------------------------------------------------------------------
        
        /** Type of joint constraint connecting this bone to its parent. */
        ConstraintType ParentConstraintType = ConstraintType::Cone;
        
        /** Primary axis for the constraint (normalized direction vector). */
        Math::Vec3 ConstraintAxis = Math::Vec3(0.0f, 1.0f, 0.0f);
        
        /** Maximum cone angle limit in radians (for Cone constraints). */
        float ConeAngleLimit = 0.5236f; // ~30 degrees
        
        /** Minimum twist rotation limit in radians (around constraint axis). */
        float TwistMinLimit = -0.7854f; // ~-45 degrees
        
        /** Maximum twist rotation limit in radians (around constraint axis). */
        float TwistMaxLimit = 0.7854f;  // ~45 degrees

        // -------------------------------------------------------------------------
        // State
        // -------------------------------------------------------------------------
        
        /** Whether this bone participates in ragdoll simulation. */
        bool Enabled = true;

        // -------------------------------------------------------------------------
        // Constructors
        // -------------------------------------------------------------------------
        
        RagdollBoneDefinition() = default;
        
        RagdollBoneDefinition(const std::string& name, int32_t boneIndex = -1)
            : SkeletonBoneIndex(boneIndex)
            , BoneName(name)
        {
        }

        // -------------------------------------------------------------------------
        // Utility Methods
        // -------------------------------------------------------------------------
        
        /**
         * @brief Checks if this bone definition is properly bound to a skeleton bone.
         * @return True if SkeletonBoneIndex is valid (>= 0).
         */
        [[nodiscard]] bool IsBound() const noexcept
        {
            return SkeletonBoneIndex >= 0;
        }

        /**
         * @brief Checks if this bone is a root bone (no parent constraint).
         * @return True if this is a root bone with Fixed constraint type.
         */
        [[nodiscard]] bool IsRoot() const noexcept
        {
            return ParentConstraintType == ConstraintType::Fixed && SkeletonBoneIndex == 0;
        }
    };

    // =============================================================================
    // RagdollDefinition
    // =============================================================================

    /**
     * @brief Complete ragdoll skeleton definition containing all bones and global settings.
     * 
     * This structure defines the complete physical representation of a ragdoll,
     * including all bone definitions, constraint settings, and collision behavior.
     * Use static factory methods to create common ragdoll configurations.
     */
    struct RagdollDefinition
    {
        // -------------------------------------------------------------------------
        // Identification
        // -------------------------------------------------------------------------
        
        /** Unique name identifier for this ragdoll definition. */
        std::string Name;
        
        /** The skeletal profile this ragdoll is based on. */
        RagdollProfile Profile = RagdollProfile::Custom;

        // -------------------------------------------------------------------------
        // Bone Configuration
        // -------------------------------------------------------------------------
        
        /** Collection of all bone definitions in this ragdoll. */
        std::vector<RagdollBoneDefinition> Bones;

        // -------------------------------------------------------------------------
        // Global Physics Properties
        // -------------------------------------------------------------------------
        
        /** Global multiplier applied to all bone masses. */
        float GlobalMassScale = 1.0f;
        
        /** Global multiplier applied to all bone damping values. */
        float GlobalDampingScale = 1.0f;
        
        /** Whether ragdoll bones can collide with each other. */
        bool SelfCollision = false;
        
        /** Whether ragdoll collides with world geometry. */
        bool CollideWithWorld = true;
        
        /** Global joint stiffness factor [0, 1]. Higher values resist movement more. */
        float JointStiffness = 0.3f;

        // -------------------------------------------------------------------------
        // Constructors
        // -------------------------------------------------------------------------
        
        RagdollDefinition() = default;
        
        explicit RagdollDefinition(const std::string& name, RagdollProfile profile = RagdollProfile::Custom)
            : Name(name)
            , Profile(profile)
        {
        }

        // -------------------------------------------------------------------------
        // Bone Lookup Methods
        // -------------------------------------------------------------------------
        
        /**
         * @brief Finds a bone definition by its skeleton bone index.
         * @param skeletonBoneIndex The skeleton bone index to search for.
         * @return Pointer to the bone definition, or nullptr if not found.
         */
        [[nodiscard]] RagdollBoneDefinition* FindBone(int32_t skeletonBoneIndex)
        {
            for (auto& bone : Bones)
            {
                if (bone.SkeletonBoneIndex == skeletonBoneIndex)
                {
                    return &bone;
                }
            }
            return nullptr;
        }

        /**
         * @brief Finds a bone definition by its skeleton bone index (const version).
         * @param skeletonBoneIndex The skeleton bone index to search for.
         * @return Const pointer to the bone definition, or nullptr if not found.
         */
        [[nodiscard]] const RagdollBoneDefinition* FindBone(int32_t skeletonBoneIndex) const
        {
            for (const auto& bone : Bones)
            {
                if (bone.SkeletonBoneIndex == skeletonBoneIndex)
                {
                    return &bone;
                }
            }
            return nullptr;
        }

        /**
         * @brief Finds a bone definition by its name.
         * @param boneName The bone name to search for (case-sensitive).
         * @return Pointer to the bone definition, or nullptr if not found.
         */
        [[nodiscard]] RagdollBoneDefinition* FindBone(const std::string& boneName)
        {
            for (auto& bone : Bones)
            {
                if (bone.BoneName == boneName)
                {
                    return &bone;
                }
            }
            return nullptr;
        }

        /**
         * @brief Finds a bone definition by its name (const version).
         * @param boneName The bone name to search for (case-sensitive).
         * @return Const pointer to the bone definition, or nullptr if not found.
         */
        [[nodiscard]] const RagdollBoneDefinition* FindBone(const std::string& boneName) const
        {
            for (const auto& bone : Bones)
            {
                if (bone.BoneName == boneName)
                {
                    return &bone;
                }
            }
            return nullptr;
        }

        // -------------------------------------------------------------------------
        // Utility Methods
        // -------------------------------------------------------------------------
        
        /**
         * @brief Gets the total number of enabled bones in the ragdoll.
         * @return Count of bones where Enabled == true.
         */
        [[nodiscard]] size_t GetEnabledBoneCount() const noexcept
        {
            size_t count = 0;
            for (const auto& bone : Bones)
            {
                if (bone.Enabled)
                {
                    ++count;
                }
            }
            return count;
        }

        /**
         * @brief Calculates the total mass of the ragdoll (with global scale applied).
         * @return Sum of all bone masses multiplied by GlobalMassScale.
         */
        [[nodiscard]] float GetTotalMass() const noexcept
        {
            float totalMass = 0.0f;
            for (const auto& bone : Bones)
            {
                if (bone.Enabled)
                {
                    totalMass += bone.Mass;
                }
            }
            return totalMass * GlobalMassScale;
        }

        // -------------------------------------------------------------------------
        // Static Factory Methods
        // -------------------------------------------------------------------------
        
        /**
         * @brief Creates a standard humanoid ragdoll definition.
         * 
         * Generates a ragdoll with the following bones:
         * - Pelvis (root), Spine, Chest, Head
         * - Left/Right Upper Arm, Lower Arm, Hand
         * - Left/Right Upper Leg, Lower Leg, Foot
         * 
         * @param name Name for the ragdoll definition.
         * @return A fully configured humanoid ragdoll definition.
         */
        [[nodiscard]] static RagdollDefinition CreateHumanoid(const std::string& name = "Humanoid")
        {
            RagdollDefinition def(name, RagdollProfile::Humanoid);
            def.SelfCollision = false;
            def.CollideWithWorld = true;
            def.JointStiffness = 0.3f;

            // Pelvis (Root)
            {
                RagdollBoneDefinition bone("Pelvis", 0);
                bone.ColliderType = ColliderType::Box;
                bone.ColliderDimensions = Math::Vec3(0.15f, 0.1f, 0.1f);
                bone.Mass = 15.0f;
                bone.ParentConstraintType = ConstraintType::Fixed;
                def.Bones.push_back(bone);
            }

            // Spine
            {
                RagdollBoneDefinition bone("Spine", 1);
                bone.ColliderType = ColliderType::Box;
                bone.ColliderDimensions = Math::Vec3(0.12f, 0.15f, 0.08f);
                bone.Mass = 10.0f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 0.3491f; // ~20 degrees
                bone.TwistMinLimit = -0.2618f;
                bone.TwistMaxLimit = 0.2618f;
                def.Bones.push_back(bone);
            }

            // Chest
            {
                RagdollBoneDefinition bone("Chest", 2);
                bone.ColliderType = ColliderType::Box;
                bone.ColliderDimensions = Math::Vec3(0.15f, 0.2f, 0.1f);
                bone.Mass = 12.0f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 0.2618f; // ~15 degrees
                def.Bones.push_back(bone);
            }

            // Head
            {
                RagdollBoneDefinition bone("Head", 3);
                bone.ColliderType = ColliderType::Sphere;
                bone.ColliderDimensions = Math::Vec3(0.1f, 0.0f, 0.0f);
                bone.Mass = 5.0f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 0.5236f; // ~30 degrees
                bone.TwistMinLimit = -1.0472f; // ~-60 degrees
                bone.TwistMaxLimit = 1.0472f;  // ~60 degrees
                def.Bones.push_back(bone);
            }

            // Left Upper Arm
            {
                RagdollBoneDefinition bone("LeftUpperArm", 4);
                bone.ColliderType = ColliderType::Capsule;
                bone.ColliderDimensions = Math::Vec3(0.04f, 0.25f, 0.0f);
                bone.Mass = 3.0f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 1.5708f; // ~90 degrees
                bone.ConstraintAxis = Math::Vec3(1.0f, 0.0f, 0.0f);
                def.Bones.push_back(bone);
            }

            // Left Lower Arm
            {
                RagdollBoneDefinition bone("LeftLowerArm", 5);
                bone.ColliderType = ColliderType::Capsule;
                bone.ColliderDimensions = Math::Vec3(0.035f, 0.22f, 0.0f);
                bone.Mass = 2.0f;
                bone.ParentConstraintType = ConstraintType::Hinge;
                bone.ConstraintAxis = Math::Vec3(0.0f, 0.0f, 1.0f);
                bone.TwistMinLimit = 0.0f;
                bone.TwistMaxLimit = 2.6180f; // ~150 degrees
                def.Bones.push_back(bone);
            }

            // Left Hand
            {
                RagdollBoneDefinition bone("LeftHand", 6);
                bone.ColliderType = ColliderType::Box;
                bone.ColliderDimensions = Math::Vec3(0.04f, 0.08f, 0.02f);
                bone.Mass = 0.5f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 1.0472f; // ~60 degrees
                def.Bones.push_back(bone);
            }

            // Right Upper Arm
            {
                RagdollBoneDefinition bone("RightUpperArm", 7);
                bone.ColliderType = ColliderType::Capsule;
                bone.ColliderDimensions = Math::Vec3(0.04f, 0.25f, 0.0f);
                bone.Mass = 3.0f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 1.5708f; // ~90 degrees
                bone.ConstraintAxis = Math::Vec3(-1.0f, 0.0f, 0.0f);
                def.Bones.push_back(bone);
            }

            // Right Lower Arm
            {
                RagdollBoneDefinition bone("RightLowerArm", 8);
                bone.ColliderType = ColliderType::Capsule;
                bone.ColliderDimensions = Math::Vec3(0.035f, 0.22f, 0.0f);
                bone.Mass = 2.0f;
                bone.ParentConstraintType = ConstraintType::Hinge;
                bone.ConstraintAxis = Math::Vec3(0.0f, 0.0f, 1.0f);
                bone.TwistMinLimit = 0.0f;
                bone.TwistMaxLimit = 2.6180f; // ~150 degrees
                def.Bones.push_back(bone);
            }

            // Right Hand
            {
                RagdollBoneDefinition bone("RightHand", 9);
                bone.ColliderType = ColliderType::Box;
                bone.ColliderDimensions = Math::Vec3(0.04f, 0.08f, 0.02f);
                bone.Mass = 0.5f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 1.0472f; // ~60 degrees
                def.Bones.push_back(bone);
            }

            // Left Upper Leg
            {
                RagdollBoneDefinition bone("LeftUpperLeg", 10);
                bone.ColliderType = ColliderType::Capsule;
                bone.ColliderDimensions = Math::Vec3(0.06f, 0.4f, 0.0f);
                bone.Mass = 8.0f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 1.2217f; // ~70 degrees
                bone.ConstraintAxis = Math::Vec3(0.0f, -1.0f, 0.0f);
                def.Bones.push_back(bone);
            }

            // Left Lower Leg
            {
                RagdollBoneDefinition bone("LeftLowerLeg", 11);
                bone.ColliderType = ColliderType::Capsule;
                bone.ColliderDimensions = Math::Vec3(0.05f, 0.38f, 0.0f);
                bone.Mass = 5.0f;
                bone.ParentConstraintType = ConstraintType::Hinge;
                bone.ConstraintAxis = Math::Vec3(1.0f, 0.0f, 0.0f);
                bone.TwistMinLimit = -2.4435f; // ~-140 degrees
                bone.TwistMaxLimit = 0.0f;
                def.Bones.push_back(bone);
            }

            // Left Foot
            {
                RagdollBoneDefinition bone("LeftFoot", 12);
                bone.ColliderType = ColliderType::Box;
                bone.ColliderDimensions = Math::Vec3(0.05f, 0.03f, 0.12f);
                bone.Mass = 1.0f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 0.5236f; // ~30 degrees
                def.Bones.push_back(bone);
            }

            // Right Upper Leg
            {
                RagdollBoneDefinition bone("RightUpperLeg", 13);
                bone.ColliderType = ColliderType::Capsule;
                bone.ColliderDimensions = Math::Vec3(0.06f, 0.4f, 0.0f);
                bone.Mass = 8.0f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 1.2217f; // ~70 degrees
                bone.ConstraintAxis = Math::Vec3(0.0f, -1.0f, 0.0f);
                def.Bones.push_back(bone);
            }

            // Right Lower Leg
            {
                RagdollBoneDefinition bone("RightLowerLeg", 14);
                bone.ColliderType = ColliderType::Capsule;
                bone.ColliderDimensions = Math::Vec3(0.05f, 0.38f, 0.0f);
                bone.Mass = 5.0f;
                bone.ParentConstraintType = ConstraintType::Hinge;
                bone.ConstraintAxis = Math::Vec3(1.0f, 0.0f, 0.0f);
                bone.TwistMinLimit = -2.4435f; // ~-140 degrees
                bone.TwistMaxLimit = 0.0f;
                def.Bones.push_back(bone);
            }

            // Right Foot
            {
                RagdollBoneDefinition bone("RightFoot", 15);
                bone.ColliderType = ColliderType::Box;
                bone.ColliderDimensions = Math::Vec3(0.05f, 0.03f, 0.12f);
                bone.Mass = 1.0f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 0.5236f; // ~30 degrees
                def.Bones.push_back(bone);
            }

            return def;
        }

        /**
         * @brief Creates a standard quadruped ragdoll definition.
         * 
         * Generates a ragdoll suitable for four-legged creatures:
         * - Pelvis (root), Spine, Chest, Neck, Head
         * - Front Left/Right: Upper Leg, Lower Leg, Paw
         * - Back Left/Right: Upper Leg, Lower Leg, Paw
         * - Tail (optional chain)
         * 
         * @param name Name for the ragdoll definition.
         * @return A fully configured quadruped ragdoll definition.
         */
        [[nodiscard]] static RagdollDefinition CreateQuadruped(const std::string& name = "Quadruped")
        {
            RagdollDefinition def(name, RagdollProfile::Quadruped);
            def.SelfCollision = false;
            def.CollideWithWorld = true;
            def.JointStiffness = 0.4f;

            // Pelvis (Root)
            {
                RagdollBoneDefinition bone("Pelvis", 0);
                bone.ColliderType = ColliderType::Box;
                bone.ColliderDimensions = Math::Vec3(0.12f, 0.1f, 0.15f);
                bone.Mass = 12.0f;
                bone.ParentConstraintType = ConstraintType::Fixed;
                def.Bones.push_back(bone);
            }

            // Spine
            {
                RagdollBoneDefinition bone("Spine", 1);
                bone.ColliderType = ColliderType::Capsule;
                bone.ColliderDimensions = Math::Vec3(0.08f, 0.25f, 0.0f);
                bone.LocalRotation = Math::Quat(0.7071f, 0.0f, 0.0f, 0.7071f); // 90 deg X
                bone.Mass = 8.0f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 0.2618f; // ~15 degrees
                bone.ConstraintAxis = Math::Vec3(0.0f, 0.0f, 1.0f);
                def.Bones.push_back(bone);
            }

            // Chest
            {
                RagdollBoneDefinition bone("Chest", 2);
                bone.ColliderType = ColliderType::Box;
                bone.ColliderDimensions = Math::Vec3(0.12f, 0.1f, 0.12f);
                bone.Mass = 10.0f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 0.1745f; // ~10 degrees
                def.Bones.push_back(bone);
            }

            // Neck
            {
                RagdollBoneDefinition bone("Neck", 3);
                bone.ColliderType = ColliderType::Capsule;
                bone.ColliderDimensions = Math::Vec3(0.05f, 0.15f, 0.0f);
                bone.Mass = 3.0f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 0.5236f; // ~30 degrees
                def.Bones.push_back(bone);
            }

            // Head
            {
                RagdollBoneDefinition bone("Head", 4);
                bone.ColliderType = ColliderType::Box;
                bone.ColliderDimensions = Math::Vec3(0.08f, 0.1f, 0.12f);
                bone.Mass = 4.0f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 0.6981f; // ~40 degrees
                def.Bones.push_back(bone);
            }

            // Front Left Upper Leg
            {
                RagdollBoneDefinition bone("FrontLeftUpperLeg", 5);
                bone.ColliderType = ColliderType::Capsule;
                bone.ColliderDimensions = Math::Vec3(0.04f, 0.18f, 0.0f);
                bone.Mass = 3.0f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 0.7854f; // ~45 degrees
                bone.ConstraintAxis = Math::Vec3(1.0f, 0.0f, 0.0f);
                def.Bones.push_back(bone);
            }

            // Front Left Lower Leg
            {
                RagdollBoneDefinition bone("FrontLeftLowerLeg", 6);
                bone.ColliderType = ColliderType::Capsule;
                bone.ColliderDimensions = Math::Vec3(0.03f, 0.15f, 0.0f);
                bone.Mass = 1.5f;
                bone.ParentConstraintType = ConstraintType::Hinge;
                bone.ConstraintAxis = Math::Vec3(1.0f, 0.0f, 0.0f);
                bone.TwistMinLimit = -1.5708f;
                bone.TwistMaxLimit = 0.0f;
                def.Bones.push_back(bone);
            }

            // Front Left Paw
            {
                RagdollBoneDefinition bone("FrontLeftPaw", 7);
                bone.ColliderType = ColliderType::Sphere;
                bone.ColliderDimensions = Math::Vec3(0.03f, 0.0f, 0.0f);
                bone.Mass = 0.3f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 0.3491f; // ~20 degrees
                def.Bones.push_back(bone);
            }

            // Front Right Upper Leg
            {
                RagdollBoneDefinition bone("FrontRightUpperLeg", 8);
                bone.ColliderType = ColliderType::Capsule;
                bone.ColliderDimensions = Math::Vec3(0.04f, 0.18f, 0.0f);
                bone.Mass = 3.0f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 0.7854f; // ~45 degrees
                bone.ConstraintAxis = Math::Vec3(-1.0f, 0.0f, 0.0f);
                def.Bones.push_back(bone);
            }

            // Front Right Lower Leg
            {
                RagdollBoneDefinition bone("FrontRightLowerLeg", 9);
                bone.ColliderType = ColliderType::Capsule;
                bone.ColliderDimensions = Math::Vec3(0.03f, 0.15f, 0.0f);
                bone.Mass = 1.5f;
                bone.ParentConstraintType = ConstraintType::Hinge;
                bone.ConstraintAxis = Math::Vec3(1.0f, 0.0f, 0.0f);
                bone.TwistMinLimit = -1.5708f;
                bone.TwistMaxLimit = 0.0f;
                def.Bones.push_back(bone);
            }

            // Front Right Paw
            {
                RagdollBoneDefinition bone("FrontRightPaw", 10);
                bone.ColliderType = ColliderType::Sphere;
                bone.ColliderDimensions = Math::Vec3(0.03f, 0.0f, 0.0f);
                bone.Mass = 0.3f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 0.3491f; // ~20 degrees
                def.Bones.push_back(bone);
            }

            // Back Left Upper Leg
            {
                RagdollBoneDefinition bone("BackLeftUpperLeg", 11);
                bone.ColliderType = ColliderType::Capsule;
                bone.ColliderDimensions = Math::Vec3(0.045f, 0.2f, 0.0f);
                bone.Mass = 4.0f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 0.8727f; // ~50 degrees
                bone.ConstraintAxis = Math::Vec3(1.0f, 0.0f, 0.0f);
                def.Bones.push_back(bone);
            }

            // Back Left Lower Leg
            {
                RagdollBoneDefinition bone("BackLeftLowerLeg", 12);
                bone.ColliderType = ColliderType::Capsule;
                bone.ColliderDimensions = Math::Vec3(0.035f, 0.18f, 0.0f);
                bone.Mass = 2.0f;
                bone.ParentConstraintType = ConstraintType::Hinge;
                bone.ConstraintAxis = Math::Vec3(1.0f, 0.0f, 0.0f);
                bone.TwistMinLimit = 0.0f;
                bone.TwistMaxLimit = 1.5708f;
                def.Bones.push_back(bone);
            }

            // Back Left Paw
            {
                RagdollBoneDefinition bone("BackLeftPaw", 13);
                bone.ColliderType = ColliderType::Sphere;
                bone.ColliderDimensions = Math::Vec3(0.035f, 0.0f, 0.0f);
                bone.Mass = 0.4f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 0.3491f; // ~20 degrees
                def.Bones.push_back(bone);
            }

            // Back Right Upper Leg
            {
                RagdollBoneDefinition bone("BackRightUpperLeg", 14);
                bone.ColliderType = ColliderType::Capsule;
                bone.ColliderDimensions = Math::Vec3(0.045f, 0.2f, 0.0f);
                bone.Mass = 4.0f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 0.8727f; // ~50 degrees
                bone.ConstraintAxis = Math::Vec3(-1.0f, 0.0f, 0.0f);
                def.Bones.push_back(bone);
            }

            // Back Right Lower Leg
            {
                RagdollBoneDefinition bone("BackRightLowerLeg", 15);
                bone.ColliderType = ColliderType::Capsule;
                bone.ColliderDimensions = Math::Vec3(0.035f, 0.18f, 0.0f);
                bone.Mass = 2.0f;
                bone.ParentConstraintType = ConstraintType::Hinge;
                bone.ConstraintAxis = Math::Vec3(1.0f, 0.0f, 0.0f);
                bone.TwistMinLimit = 0.0f;
                bone.TwistMaxLimit = 1.5708f;
                def.Bones.push_back(bone);
            }

            // Back Right Paw
            {
                RagdollBoneDefinition bone("BackRightPaw", 16);
                bone.ColliderType = ColliderType::Sphere;
                bone.ColliderDimensions = Math::Vec3(0.035f, 0.0f, 0.0f);
                bone.Mass = 0.4f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 0.3491f; // ~20 degrees
                def.Bones.push_back(bone);
            }

            // Tail Base
            {
                RagdollBoneDefinition bone("TailBase", 17);
                bone.ColliderType = ColliderType::Capsule;
                bone.ColliderDimensions = Math::Vec3(0.025f, 0.1f, 0.0f);
                bone.Mass = 0.5f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 0.7854f; // ~45 degrees
                bone.ConstraintAxis = Math::Vec3(0.0f, 0.0f, -1.0f);
                def.Bones.push_back(bone);
            }

            // Tail Mid
            {
                RagdollBoneDefinition bone("TailMid", 18);
                bone.ColliderType = ColliderType::Capsule;
                bone.ColliderDimensions = Math::Vec3(0.02f, 0.08f, 0.0f);
                bone.Mass = 0.3f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 0.5236f; // ~30 degrees
                def.Bones.push_back(bone);
            }

            // Tail Tip
            {
                RagdollBoneDefinition bone("TailTip", 19);
                bone.ColliderType = ColliderType::Capsule;
                bone.ColliderDimensions = Math::Vec3(0.015f, 0.06f, 0.0f);
                bone.Mass = 0.2f;
                bone.ParentConstraintType = ConstraintType::Cone;
                bone.ConeAngleLimit = 0.5236f; // ~30 degrees
                def.Bones.push_back(bone);
            }

            return def;
        }
    };

} // namespace Core::Physics
