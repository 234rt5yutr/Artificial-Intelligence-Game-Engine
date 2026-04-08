#pragma once

#include "Core/Physics/Ragdoll/RagdollGenerator.h"
#include "Core/Math/Math.h"

#include <cstdint>

namespace Core
{
    namespace ECS
    {
        struct RagdollComponent;
    }
}

namespace Core::Physics
{
    // Forward declarations
    class PhysicsWorld;

    // =============================================================================
    // BlendMode Enumeration
    // =============================================================================

    /**
     * @brief Defines the interpolation curve used for pose blending transitions.
     * 
     * Different blend modes provide varying acceleration/deceleration profiles
     * for smoother, more natural-looking transitions between animation and
     * ragdoll physics states.
     */
    enum class BlendMode : uint8_t
    {
        Linear,     ///< Constant rate interpolation (t)
        EaseIn,     ///< Starts slow, accelerates (t^2)
        EaseOut,    ///< Starts fast, decelerates (1 - (1-t)^2)
        EaseInOut,  ///< Smooth acceleration and deceleration (smoothstep)
        Cubic       ///< Cubic interpolation for extra-smooth transitions (t^3)
    };

    // =============================================================================
    // AnimationBlender
    // =============================================================================

    /**
     * @brief Static utility class for blending between animation and ragdoll poses.
     * 
     * Provides functionality for:
     * - Blending skeleton poses with various interpolation curves
     * - Capturing ragdoll poses from physics simulation
     * - Applying poses back to ragdoll bodies
     * - Smooth damping and transform interpolation utilities
     * 
     * This class is essential for seamless transitions between animated
     * (kinematic) and physics-driven (dynamic) character states.
     * 
     * @note This class follows the static utility pattern - all methods are static
     *       and no instance state is maintained.
     * 
     * @example
     * @code
     * // Blend from animation to ragdoll over time
     * float t = blendTimer / blendDuration;
     * float weight = AnimationBlender::CalculateBlendWeight(t, BlendMode::EaseInOut);
     * SkeletonPose blendedPose = AnimationBlender::BlendPoses(
     *     animPose, ragdollPose, weight, BlendMode::EaseInOut);
     * 
     * // Capture current ragdoll state
     * SkeletonPose currentPose = AnimationBlender::CaptureRagdollPose(
     *     physicsWorld, ragdollComponent);
     * @endcode
     */
    class AnimationBlender
    {
    public:
        // -------------------------------------------------------------------------
        // Deleted Constructors (Static Utility Class)
        // -------------------------------------------------------------------------

        AnimationBlender() = delete;
        ~AnimationBlender() = delete;
        AnimationBlender(const AnimationBlender&) = delete;
        AnimationBlender& operator=(const AnimationBlender&) = delete;
        AnimationBlender(AnimationBlender&&) = delete;
        AnimationBlender& operator=(AnimationBlender&&) = delete;

        // -------------------------------------------------------------------------
        // Pose Blending
        // -------------------------------------------------------------------------

        /**
         * @brief Blends between an animation pose and a ragdoll pose.
         * 
         * Interpolates each bone transform from the animation pose to the
         * ragdoll pose based on the blend weight and mode. Uses per-bone
         * matrix decomposition for proper rotation interpolation.
         * 
         * @param animPose The source animation pose (typically from skeletal animation).
         * @param ragdollPose The target ragdoll pose (captured from physics simulation).
         * @param t Normalized blend time [0, 1] where 0 = full animation, 1 = full ragdoll.
         * @param mode The interpolation curve to apply to the blend weight.
         * @return The resulting blended skeleton pose.
         * 
         * @note If the poses have different bone counts, the smaller count is used.
         */
        [[nodiscard]] static SkeletonPose BlendPoses(
            const SkeletonPose& animPose,
            const SkeletonPose& ragdollPose,
            float t,
            BlendMode mode);

        /**
         * @brief Calculates the blend weight for a given normalized time and mode.
         * 
         * Applies the specified easing function to transform linear time
         * into the appropriate blend curve.
         * 
         * @param normalizedTime Time value in range [0, 1].
         * @param mode The blend curve to apply.
         * @return The eased blend weight in range [0, 1].
         */
        [[nodiscard]] static float CalculateBlendWeight(float normalizedTime, BlendMode mode);

        // -------------------------------------------------------------------------
        // Ragdoll Pose Operations
        // -------------------------------------------------------------------------

        /**
         * @brief Captures the current pose from ragdoll physics bodies.
         * 
         * Reads the world-space transforms of all ragdoll bodies and
         * constructs a SkeletonPose suitable for blending or recording.
         * 
         * @param physicsWorld The physics world containing the ragdoll bodies.
         * @param ragdoll The ragdoll component with body references.
         * @return SkeletonPose containing world transforms of all ragdoll bones.
         * 
         * @note Returns an empty pose if the ragdoll has no bodies.
         */
        [[nodiscard]] static SkeletonPose CaptureRagdollPose(
            PhysicsWorld& physicsWorld,
            ECS::RagdollComponent& ragdoll);

        /**
         * @brief Applies a skeleton pose to ragdoll physics bodies.
         * 
         * Sets the transforms of all ragdoll bodies to match the given pose.
         * Typically used when bodies are in kinematic mode during blending.
         * 
         * @param physicsWorld The physics world containing the ragdoll bodies.
         * @param ragdoll The ragdoll component with body references.
         * @param pose The pose to apply to the ragdoll bodies.
         * 
         * @warning Bodies should be set to kinematic mode before calling this
         *          to avoid physics conflicts.
         */
        static void ApplyPoseToRagdoll(
            PhysicsWorld& physicsWorld,
            ECS::RagdollComponent& ragdoll,
            const SkeletonPose& pose);

        /**
         * @brief Sets kinematic mode for all ragdoll physics bodies.
         * 
         * In kinematic mode, bodies are controlled directly by code rather
         * than physics simulation. This is essential during blend transitions.
         * 
         * @param physicsWorld The physics world containing the ragdoll bodies.
         * @param ragdoll The ragdoll component with body references.
         * @param isKinematic True to enable kinematic mode, false for dynamic physics.
         */
        static void SetBodiesKinematic(
            PhysicsWorld& physicsWorld,
            ECS::RagdollComponent& ragdoll,
            bool isKinematic);

        // -------------------------------------------------------------------------
        // Transform Utilities
        // -------------------------------------------------------------------------

        /**
         * @brief Smoothly damps a transform towards a target using velocity tracking.
         * 
         * Provides frame-rate independent smooth interpolation that naturally
         * decelerates as it approaches the target. Useful for smooth camera
         * following or secondary motion effects.
         * 
         * @param current The current transformation matrix.
         * @param target The target transformation matrix.
         * @param velocity Reference to velocity state (modified by this function).
         * @param smoothTime Approximate time to reach the target.
         * @param deltaTime Time elapsed since last frame.
         * @return The smoothly interpolated transformation matrix.
         */
        [[nodiscard]] static Math::Mat4 SmoothDampTransform(
            const Math::Mat4& current,
            const Math::Mat4& target,
            Math::Vec3& velocity,
            float smoothTime,
            float deltaTime);

        /**
         * @brief Linearly interpolates between two transformation matrices.
         * 
         * Performs proper interpolation by decomposing matrices into
         * translation, rotation, and scale components, interpolating each
         * separately (using slerp for rotation), then recomposing.
         * 
         * @param a The starting transformation matrix.
         * @param b The ending transformation matrix.
         * @param t Interpolation factor [0, 1].
         * @return The interpolated transformation matrix.
         */
        [[nodiscard]] static Math::Mat4 InterpolateMat4(
            const Math::Mat4& a,
            const Math::Mat4& b,
            float t);

        // -------------------------------------------------------------------------
        // Easing Functions
        // -------------------------------------------------------------------------

        /**
         * @brief Quadratic ease-in function.
         * 
         * Starts slow and accelerates. Follows the curve t^2.
         * 
         * @param t Input value in range [0, 1].
         * @return Eased value in range [0, 1].
         */
        [[nodiscard]] static float EaseInFunc(float t) noexcept
        {
            return t * t;
        }

        /**
         * @brief Quadratic ease-out function.
         * 
         * Starts fast and decelerates. Follows the curve 1 - (1-t)^2.
         * 
         * @param t Input value in range [0, 1].
         * @return Eased value in range [0, 1].
         */
        [[nodiscard]] static float EaseOutFunc(float t) noexcept
        {
            const float oneMinusT = 1.0f - t;
            return 1.0f - (oneMinusT * oneMinusT);
        }

        /**
         * @brief Smooth ease-in-out function using smoothstep.
         * 
         * Provides smooth acceleration and deceleration.
         * Follows the curve: 3t^2 - 2t^3 (Hermite smoothstep).
         * 
         * @param t Input value in range [0, 1].
         * @return Eased value in range [0, 1].
         */
        [[nodiscard]] static float EaseInOutFunc(float t) noexcept
        {
            return t * t * (3.0f - 2.0f * t);
        }

        /**
         * @brief Cubic easing function.
         * 
         * Provides extra-smooth transitions with gradual acceleration.
         * Follows the curve t^3.
         * 
         * @param t Input value in range [0, 1].
         * @return Eased value in range [0, 1].
         */
        [[nodiscard]] static float CubicFunc(float t) noexcept
        {
            return t * t * t;
        }
    };

} // namespace Core::Physics
