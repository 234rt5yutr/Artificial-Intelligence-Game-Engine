#pragma once

// ============================================================================
// AnimatorComponent.h
// Data-driven Animation State Machine for controlling skeletal animations
// 
// Features:
// - Parameter system (Float, Bool, Trigger)
// - Animation states with speed/loop/events
// - Conditional transitions with blending
// - Factory methods for common presets (Locomotion)
// 
// Designed to work with SkeletalMeshComponent for skeletal animation playback
// ============================================================================

#include "Core/Math/Math.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <functional>
#include <optional>
#include <cstdint>
#include <algorithm>
#include <limits>

namespace Core {
namespace ECS {

    // ========================================================================
    // Forward Declarations
    // ========================================================================

    struct AnimationState;
    struct AnimationTransition;
    struct AnimationStateMachine;

    // ========================================================================
    // Animation Parameter Types
    // ========================================================================

    /**
     * @brief Types of parameters that can control animation state transitions
     */
    enum class AnimatorParameterType : uint8_t {
        Float = 0,      ///< Floating-point value (e.g., Speed, Direction)
        Bool,           ///< Boolean flag (e.g., IsGrounded, IsJumping)
        Trigger         ///< One-shot trigger, auto-resets after consumption
    };

    /**
     * @brief Runtime value storage for animator parameters
     * Uses std::variant for type-safe storage of different parameter types
     */
    struct AnimatorParameterValue {
        std::variant<float, bool> Value;
        AnimatorParameterType Type = AnimatorParameterType::Float;
        bool TriggerConsumed = false;  ///< For trigger type: marks if consumed this frame

        // Factory methods for creating parameter values
        static AnimatorParameterValue CreateFloat(float value) {
            AnimatorParameterValue param;
            param.Value = value;
            param.Type = AnimatorParameterType::Float;
            return param;
        }

        static AnimatorParameterValue CreateBool(bool value) {
            AnimatorParameterValue param;
            param.Value = value;
            param.Type = AnimatorParameterType::Bool;
            return param;
        }

        static AnimatorParameterValue CreateTrigger() {
            AnimatorParameterValue param;
            param.Value = false;
            param.Type = AnimatorParameterType::Trigger;
            param.TriggerConsumed = false;
            return param;
        }

        // Value accessors with type safety
        float GetFloat() const {
            return Type == AnimatorParameterType::Float ? std::get<float>(Value) : 0.0f;
        }

        bool GetBool() const {
            return Type == AnimatorParameterType::Bool ? std::get<bool>(Value) : false;
        }

        bool IsTriggerSet() const {
            return Type == AnimatorParameterType::Trigger && 
                   std::get<bool>(Value) && 
                   !TriggerConsumed;
        }

        void ConsumeTrigger() {
            if (Type == AnimatorParameterType::Trigger) {
                TriggerConsumed = true;
            }
        }

        void ResetTrigger() {
            if (Type == AnimatorParameterType::Trigger) {
                Value = false;
                TriggerConsumed = false;
            }
        }
    };

    /**
     * @brief Definition of an animator parameter for serialization
     */
    struct AnimatorParameterDefinition {
        std::string Name;
        AnimatorParameterType Type = AnimatorParameterType::Float;
        float DefaultFloat = 0.0f;
        bool DefaultBool = false;

        AnimatorParameterDefinition() = default;
        AnimatorParameterDefinition(const std::string& name, AnimatorParameterType type)
            : Name(name), Type(type) {}
    };

    // ========================================================================
    // Transition Condition System
    // ========================================================================

    /**
     * @brief Comparison operators for transition conditions
     */
    enum class ComparisonOp : uint8_t {
        Greater = 0,    ///< >
        Less,           ///< <
        Equal,          ///< ==
        NotEqual,       ///< !=
        GreaterEqual,   ///< >=
        LessEqual       ///< <=
    };

    /**
     * @brief A single condition that must be met for a transition to occur
     * 
     * For Float parameters: Compares against threshold using ComparisonOp
     * For Bool parameters: Compares against expected value (Equal/NotEqual only)
     * For Trigger parameters: Checks if trigger is set (threshold ignored)
     */
    struct TransitionCondition {
        std::string ParameterName;
        ComparisonOp Operator = ComparisonOp::Greater;
        float Threshold = 0.0f;         ///< For float comparisons
        bool BoolValue = true;          ///< For bool comparisons

        TransitionCondition() = default;

        // Factory for float conditions
        static TransitionCondition Float(const std::string& param, ComparisonOp op, float threshold) {
            TransitionCondition cond;
            cond.ParameterName = param;
            cond.Operator = op;
            cond.Threshold = threshold;
            return cond;
        }

        // Factory for bool conditions
        static TransitionCondition Bool(const std::string& param, bool expectedValue) {
            TransitionCondition cond;
            cond.ParameterName = param;
            cond.Operator = expectedValue ? ComparisonOp::Equal : ComparisonOp::NotEqual;
            cond.BoolValue = expectedValue;
            return cond;
        }

        // Factory for trigger conditions
        static TransitionCondition Trigger(const std::string& param) {
            TransitionCondition cond;
            cond.ParameterName = param;
            cond.Operator = ComparisonOp::Equal;  // Trigger just needs to be set
            cond.BoolValue = true;
            return cond;
        }

        /**
         * @brief Evaluate this condition against the current parameter values
         * @param parameters Map of parameter name to value
         * @return true if condition is satisfied
         */
        bool Evaluate(const std::unordered_map<std::string, AnimatorParameterValue>& parameters) const {
            auto it = parameters.find(ParameterName);
            if (it == parameters.end()) {
                return false;  // Parameter not found, condition fails
            }

            const auto& param = it->second;

            switch (param.Type) {
                case AnimatorParameterType::Float: {
                    float value = param.GetFloat();
                    switch (Operator) {
                        case ComparisonOp::Greater:      return value > Threshold;
                        case ComparisonOp::Less:         return value < Threshold;
                        case ComparisonOp::Equal:        return std::abs(value - Threshold) < 0.0001f;
                        case ComparisonOp::NotEqual:     return std::abs(value - Threshold) >= 0.0001f;
                        case ComparisonOp::GreaterEqual: return value >= Threshold;
                        case ComparisonOp::LessEqual:    return value <= Threshold;
                    }
                    break;
                }
                case AnimatorParameterType::Bool: {
                    bool value = param.GetBool();
                    switch (Operator) {
                        case ComparisonOp::Equal:    return value == BoolValue;
                        case ComparisonOp::NotEqual: return value != BoolValue;
                        default: return value == BoolValue;  // Default to equality check
                    }
                    break;
                }
                case AnimatorParameterType::Trigger: {
                    return param.IsTriggerSet();
                }
            }

            return false;
        }
    };

    // ========================================================================
    // Animation Events
    // ========================================================================

    /**
     * @brief Types of animation events that can be triggered
     */
    enum class AnimationEventType : uint8_t {
        Custom = 0,     ///< User-defined event with string identifier
        Sound,          ///< Play a sound effect
        Particle,       ///< Spawn particle effect
        Callback        ///< Invoke a registered callback (runtime only, not serialized)
    };

    /**
     * @brief An event that can be triggered during animation playback
     * Stored as data for serialization, callbacks registered at runtime
     */
    struct AnimationEvent {
        std::string Name;                           ///< Event identifier
        AnimationEventType Type = AnimationEventType::Custom;
        float NormalizedTime = 0.0f;                ///< 0-1 position in animation
        std::string StringData;                     ///< Associated data (sound name, etc.)
        Math::Vec3 VectorData{0.0f};               ///< Optional position offset

        AnimationEvent() = default;
        AnimationEvent(const std::string& name, float time, AnimationEventType type = AnimationEventType::Custom)
            : Name(name), Type(type), NormalizedTime(time) {}
    };

    /**
     * @brief Entry/exit event identifiers for state transitions
     * Actual callbacks are registered separately at runtime
     */
    struct StateEvents {
        std::string OnEnterEvent;   ///< Event name triggered when entering state
        std::string OnExitEvent;    ///< Event name triggered when exiting state

        bool HasEntryEvent() const { return !OnEnterEvent.empty(); }
        bool HasExitEvent() const { return !OnExitEvent.empty(); }
    };

    // ========================================================================
    // Animation State
    // ========================================================================

    /**
     * @brief A single state in the animation state machine
     * 
     * Represents one animation that can be played (e.g., Idle, Walk, Run)
     * Contains playback settings and associated events
     */
    struct AnimationState {
        std::string Name;                   ///< Unique identifier for this state
        std::string AnimationClipName;      ///< Name of the animation clip to play
        
        float SpeedMultiplier = 1.0f;       ///< Playback speed (1.0 = normal)
        bool Loop = true;                   ///< Whether animation loops
        
        StateEvents Events;                 ///< Entry/exit events
        std::vector<AnimationEvent> TimelineEvents;  ///< Events at specific times

        // Motion settings
        bool ApplyRootMotion = false;       ///< Extract and apply root bone motion
        float FootIKWeight = 0.0f;          ///< Weight for foot IK (0 = disabled)

        // Blend tree support (for future expansion)
        bool IsBlendTree = false;
        std::string BlendParameterX;
        std::string BlendParameterY;

        AnimationState() = default;
        
        AnimationState(const std::string& name, const std::string& clipName, bool loop = true)
            : Name(name), AnimationClipName(clipName), Loop(loop) {}

        /**
         * @brief Create a state with common settings
         */
        static AnimationState Create(const std::string& name, 
                                     const std::string& clipName,
                                     bool loop = true,
                                     float speed = 1.0f) {
            AnimationState state;
            state.Name = name;
            state.AnimationClipName = clipName;
            state.Loop = loop;
            state.SpeedMultiplier = speed;
            return state;
        }

        /**
         * @brief Add a timeline event at a specific normalized time
         */
        AnimationState& AddTimelineEvent(const std::string& eventName, 
                                         float normalizedTime,
                                         AnimationEventType type = AnimationEventType::Custom) {
            TimelineEvents.emplace_back(eventName, normalizedTime, type);
            return *this;
        }

        /**
         * @brief Set entry event
         */
        AnimationState& SetOnEnter(const std::string& eventName) {
            Events.OnEnterEvent = eventName;
            return *this;
        }

        /**
         * @brief Set exit event
         */
        AnimationState& SetOnExit(const std::string& eventName) {
            Events.OnExitEvent = eventName;
            return *this;
        }
    };

    // ========================================================================
    // Animation Transition
    // ========================================================================

    /**
     * @brief Settings for how a transition can be interrupted
     */
    enum class InterruptionSource : uint8_t {
        None = 0,           ///< Cannot be interrupted
        CurrentState,       ///< Can be interrupted by transitions from current state
        NextState,          ///< Can be interrupted by transitions from next state
        CurrentThenNext,    ///< Check current state first, then next
        NextThenCurrent     ///< Check next state first, then current
    };

    /**
     * @brief Special source state identifier for "Any State" transitions
     */
    constexpr const char* ANY_STATE = "Any";

    /**
     * @brief A transition between two animation states
     * 
     * Defines when and how to move from one state to another,
     * including conditions, timing, and blending settings
     */
    struct AnimationTransition {
        std::string SourceState;            ///< Source state name (or "Any" for any-state)
        std::string TargetState;            ///< Destination state name
        
        float TransitionDuration = 0.25f;   ///< Blend duration in seconds
        float ExitTime = 0.0f;              ///< Normalized time (0-1) when transition can start
        bool HasExitTime = false;           ///< If true, must wait until ExitTime to transition
        
        std::vector<TransitionCondition> Conditions;  ///< All must be true (AND logic)
        
        InterruptionSource Interruption = InterruptionSource::None;
        bool CanTransitionToSelf = false;   ///< Allow transition back to same state
        
        int32_t Priority = 0;               ///< Higher priority transitions checked first

        AnimationTransition() = default;

        AnimationTransition(const std::string& from, const std::string& to, float duration = 0.25f)
            : SourceState(from), TargetState(to), TransitionDuration(duration) {}

        /**
         * @brief Create an "any state" transition
         */
        static AnimationTransition FromAnyState(const std::string& targetState, float duration = 0.25f) {
            return AnimationTransition(ANY_STATE, targetState, duration);
        }

        /**
         * @brief Add a float condition
         */
        AnimationTransition& AddFloatCondition(const std::string& param, ComparisonOp op, float threshold) {
            Conditions.push_back(TransitionCondition::Float(param, op, threshold));
            return *this;
        }

        /**
         * @brief Add a bool condition
         */
        AnimationTransition& AddBoolCondition(const std::string& param, bool expectedValue) {
            Conditions.push_back(TransitionCondition::Bool(param, expectedValue));
            return *this;
        }

        /**
         * @brief Add a trigger condition
         */
        AnimationTransition& AddTriggerCondition(const std::string& param) {
            Conditions.push_back(TransitionCondition::Trigger(param));
            return *this;
        }

        /**
         * @brief Set exit time requirement
         */
        AnimationTransition& SetExitTime(float normalizedTime) {
            ExitTime = normalizedTime;
            HasExitTime = true;
            return *this;
        }

        /**
         * @brief Set transition priority
         */
        AnimationTransition& SetPriority(int32_t priority) {
            Priority = priority;
            return *this;
        }

        /**
         * @brief Set interruption behavior
         */
        AnimationTransition& SetInterruption(InterruptionSource source) {
            Interruption = source;
            return *this;
        }

        /**
         * @brief Check if this transition is from "Any State"
         */
        bool IsAnyStateTransition() const {
            return SourceState == ANY_STATE;
        }

        /**
         * @brief Evaluate all conditions
         * @param parameters Current parameter values
         * @param currentNormalizedTime Current animation time (0-1)
         * @return true if all conditions are met
         */
        bool EvaluateConditions(const std::unordered_map<std::string, AnimatorParameterValue>& parameters,
                                float currentNormalizedTime) const {
            // Check exit time first
            if (HasExitTime && currentNormalizedTime < ExitTime) {
                return false;
            }

            // All conditions must pass (AND logic)
            for (const auto& condition : Conditions) {
                if (!condition.Evaluate(parameters)) {
                    return false;
                }
            }

            return true;
        }
    };

    // ========================================================================
    // Animation State Machine Definition
    // ========================================================================

    /**
     * @brief Complete definition of an animation state machine
     * 
     * Contains all states, transitions, and parameters that define
     * the animation behavior. This is the "blueprint" that can be
     * shared across multiple entities.
     */
    struct AnimationStateMachine {
        std::string Name;                       ///< State machine name
        
        std::vector<AnimationState> States;
        std::vector<AnimationTransition> Transitions;
        std::vector<AnimatorParameterDefinition> ParameterDefinitions;
        
        std::string DefaultStateName;           ///< Entry state name
        
        // Layer settings (for layered animation systems)
        int32_t LayerIndex = 0;
        float DefaultWeight = 1.0f;
        bool IsAdditive = false;                ///< Additive vs override blending
        std::string AvatarMask;                 ///< Bone mask for partial body animation

        AnimationStateMachine() = default;
        AnimationStateMachine(const std::string& name) : Name(name) {}

        // ====================================================================
        // State Management
        // ====================================================================

        /**
         * @brief Add a state to the state machine
         * @return Reference to this for method chaining
         */
        AnimationStateMachine& AddState(const AnimationState& state) {
            // Check for duplicate
            for (const auto& existing : States) {
                if (existing.Name == state.Name) {
                    return *this;  // Already exists
                }
            }
            States.push_back(state);
            
            // Set as default if first state
            if (States.size() == 1) {
                DefaultStateName = state.Name;
            }
            return *this;
        }

        /**
         * @brief Add a state with inline parameters
         */
        AnimationStateMachine& AddState(const std::string& name, 
                                        const std::string& clipName,
                                        bool loop = true,
                                        float speed = 1.0f) {
            return AddState(AnimationState::Create(name, clipName, loop, speed));
        }

        /**
         * @brief Find a state by name
         * @return Pointer to state or nullptr if not found
         */
        const AnimationState* FindState(const std::string& name) const {
            for (const auto& state : States) {
                if (state.Name == name) {
                    return &state;
                }
            }
            return nullptr;
        }

        AnimationState* FindState(const std::string& name) {
            for (auto& state : States) {
                if (state.Name == name) {
                    return &state;
                }
            }
            return nullptr;
        }

        /**
         * @brief Get state index by name
         * @return Index or -1 if not found
         */
        int32_t GetStateIndex(const std::string& name) const {
            for (size_t i = 0; i < States.size(); ++i) {
                if (States[i].Name == name) {
                    return static_cast<int32_t>(i);
                }
            }
            return -1;
        }

        /**
         * @brief Set the default/entry state
         */
        AnimationStateMachine& SetDefaultState(const std::string& stateName) {
            DefaultStateName = stateName;
            return *this;
        }

        // ====================================================================
        // Transition Management
        // ====================================================================

        /**
         * @brief Add a transition
         */
        AnimationStateMachine& AddTransition(const AnimationTransition& transition) {
            Transitions.push_back(transition);
            // Sort by priority (higher first)
            std::sort(Transitions.begin(), Transitions.end(),
                [](const auto& a, const auto& b) { return a.Priority > b.Priority; });
            return *this;
        }

        /**
         * @brief Add a transition with inline parameters
         */
        AnimationStateMachine& AddTransition(const std::string& from,
                                             const std::string& to,
                                             float duration = 0.25f) {
            return AddTransition(AnimationTransition(from, to, duration));
        }

        /**
         * @brief Get all transitions from a given state (including "Any" state)
         */
        std::vector<const AnimationTransition*> GetTransitionsFrom(const std::string& stateName) const {
            std::vector<const AnimationTransition*> result;
            for (const auto& transition : Transitions) {
                if (transition.SourceState == stateName || transition.IsAnyStateTransition()) {
                    // Skip self-transitions if not allowed
                    if (transition.SourceState == transition.TargetState && !transition.CanTransitionToSelf) {
                        continue;
                    }
                    result.push_back(&transition);
                }
            }
            return result;
        }

        // ====================================================================
        // Parameter Management
        // ====================================================================

        /**
         * @brief Add a float parameter definition
         */
        AnimationStateMachine& AddFloatParameter(const std::string& name, float defaultValue = 0.0f) {
            AnimatorParameterDefinition param;
            param.Name = name;
            param.Type = AnimatorParameterType::Float;
            param.DefaultFloat = defaultValue;
            ParameterDefinitions.push_back(param);
            return *this;
        }

        /**
         * @brief Add a bool parameter definition
         */
        AnimationStateMachine& AddBoolParameter(const std::string& name, bool defaultValue = false) {
            AnimatorParameterDefinition param;
            param.Name = name;
            param.Type = AnimatorParameterType::Bool;
            param.DefaultBool = defaultValue;
            ParameterDefinitions.push_back(param);
            return *this;
        }

        /**
         * @brief Add a trigger parameter definition
         */
        AnimationStateMachine& AddTriggerParameter(const std::string& name) {
            AnimatorParameterDefinition param;
            param.Name = name;
            param.Type = AnimatorParameterType::Trigger;
            ParameterDefinitions.push_back(param);
            return *this;
        }

        /**
         * @brief Check if state machine is valid for use
         */
        bool IsValid() const {
            return !States.empty() && !DefaultStateName.empty() && FindState(DefaultStateName) != nullptr;
        }
    };

    // ========================================================================
    // Runtime State
    // ========================================================================

    /**
     * @brief Runtime state of an active transition
     */
    struct ActiveTransition {
        const AnimationTransition* Transition = nullptr;
        float Progress = 0.0f;          ///< 0-1 blend progress
        float SourceTime = 0.0f;        ///< Time in source animation when transition started
        bool IsComplete = false;

        bool IsActive() const { return Transition != nullptr && !IsComplete; }

        void Reset() {
            Transition = nullptr;
            Progress = 0.0f;
            SourceTime = 0.0f;
            IsComplete = false;
        }
    };

    /**
     * @brief Runtime state tracking for animation playback
     */
    struct AnimatorRuntimeState {
        std::string CurrentStateName;       ///< Currently active state
        float CurrentStateTime = 0.0f;      ///< Playback time in current state (seconds)
        float NormalizedTime = 0.0f;        ///< Normalized time (0-1, can exceed 1 for loops)
        
        ActiveTransition CurrentTransition;
        
        std::string PreviousStateName;      ///< For transition blending
        float PreviousStateTime = 0.0f;
        
        bool IsPlaying = true;
        bool HasInitialized = false;

        /**
         * @brief Get the blend weight for the current state
         */
        float GetCurrentStateWeight() const {
            if (CurrentTransition.IsActive()) {
                return CurrentTransition.Progress;
            }
            return 1.0f;
        }

        /**
         * @brief Get the blend weight for the previous state (during transition)
         */
        float GetPreviousStateWeight() const {
            if (CurrentTransition.IsActive()) {
                return 1.0f - CurrentTransition.Progress;
            }
            return 0.0f;
        }

        void Reset() {
            CurrentStateName.clear();
            CurrentStateTime = 0.0f;
            NormalizedTime = 0.0f;
            CurrentTransition.Reset();
            PreviousStateName.clear();
            PreviousStateTime = 0.0f;
            IsPlaying = true;
            HasInitialized = false;
        }
    };

    // ========================================================================
    // AnimatorComponent
    // ========================================================================

    /**
     * @brief ECS component for animation state machine control
     * 
     * Provides a data-driven animation state machine that controls
     * animation playback on an associated SkeletalMeshComponent.
     * 
     * Usage:
     * 1. Create or load an AnimationStateMachine definition
     * 2. Attach AnimatorComponent to entity with SkeletalMeshComponent
     * 3. Set parameters to drive transitions
     * 4. AnimationSystem updates state and blending each frame
     * 
     * @code
     * // Create animator with locomotion preset
     * auto animator = AnimatorComponent::CreateWithLocomotion();
     * 
     * // In game logic, set parameters to control animation
     * animator.SetFloat("Speed", characterSpeed);
     * animator.SetBool("IsGrounded", physics.IsOnGround());
     * animator.SetTrigger("Jump");  // One-shot transition
     * @endcode
     */
    struct AnimatorComponent {
        // State machine definition (shared blueprint)
        AnimationStateMachine StateMachine;

        // Runtime parameter values
        std::unordered_map<std::string, AnimatorParameterValue> Parameters;

        // Runtime playback state
        AnimatorRuntimeState RuntimeState;

        // Update settings
        float UpdateRate = 0.0f;            ///< 0 = every frame, >0 = fixed timestep
        float TimeSinceLastUpdate = 0.0f;
        bool Enabled = true;

        // Root motion output (consumed by physics/transform system)
        Math::Vec3 RootMotionDelta{0.0f};
        Math::Quat RootMotionRotation{1.0f, 0.0f, 0.0f, 0.0f};

        // Event callback storage (runtime only, not serialized)
        // Actual callbacks registered via AnimationSystem

        // ====================================================================
        // Constructors
        // ====================================================================

        AnimatorComponent() = default;

        explicit AnimatorComponent(const AnimationStateMachine& stateMachine)
            : StateMachine(stateMachine) {
            InitializeParameters();
        }

        // ====================================================================
        // Parameter Accessors
        // ====================================================================

        /**
         * @brief Set a float parameter value
         * @param name Parameter name
         * @param value New value
         * @return true if parameter exists and was set
         */
        bool SetFloat(const std::string& name, float value) {
            auto it = Parameters.find(name);
            if (it != Parameters.end() && it->second.Type == AnimatorParameterType::Float) {
                it->second.Value = value;
                return true;
            }
            return false;
        }

        /**
         * @brief Get a float parameter value
         * @param name Parameter name
         * @return Parameter value or 0 if not found
         */
        float GetFloat(const std::string& name) const {
            auto it = Parameters.find(name);
            if (it != Parameters.end() && it->second.Type == AnimatorParameterType::Float) {
                return it->second.GetFloat();
            }
            return 0.0f;
        }

        /**
         * @brief Set a bool parameter value
         * @param name Parameter name
         * @param value New value
         * @return true if parameter exists and was set
         */
        bool SetBool(const std::string& name, bool value) {
            auto it = Parameters.find(name);
            if (it != Parameters.end() && it->second.Type == AnimatorParameterType::Bool) {
                it->second.Value = value;
                return true;
            }
            return false;
        }

        /**
         * @brief Get a bool parameter value
         * @param name Parameter name
         * @return Parameter value or false if not found
         */
        bool GetBool(const std::string& name) const {
            auto it = Parameters.find(name);
            if (it != Parameters.end() && it->second.Type == AnimatorParameterType::Bool) {
                return it->second.GetBool();
            }
            return false;
        }

        /**
         * @brief Set a trigger parameter (will be consumed on next valid transition)
         * @param name Parameter name
         * @return true if parameter exists and was set
         */
        bool SetTrigger(const std::string& name) {
            auto it = Parameters.find(name);
            if (it != Parameters.end() && it->second.Type == AnimatorParameterType::Trigger) {
                it->second.Value = true;
                it->second.TriggerConsumed = false;
                return true;
            }
            return false;
        }

        /**
         * @brief Reset a trigger parameter
         * @param name Parameter name
         */
        void ResetTrigger(const std::string& name) {
            auto it = Parameters.find(name);
            if (it != Parameters.end() && it->second.Type == AnimatorParameterType::Trigger) {
                it->second.ResetTrigger();
            }
        }

        /**
         * @brief Check if a trigger is set and not consumed
         */
        bool IsTriggerSet(const std::string& name) const {
            auto it = Parameters.find(name);
            if (it != Parameters.end() && it->second.Type == AnimatorParameterType::Trigger) {
                return it->second.IsTriggerSet();
            }
            return false;
        }

        // ====================================================================
        // State Machine Modification
        // ====================================================================

        /**
         * @brief Add a state to the state machine
         */
        void AddState(const AnimationState& state) {
            StateMachine.AddState(state);
        }

        /**
         * @brief Add a transition to the state machine
         */
        void AddTransition(const AnimationTransition& transition) {
            StateMachine.AddTransition(transition);
        }

        /**
         * @brief Add a float parameter
         */
        void AddFloatParameter(const std::string& name, float defaultValue = 0.0f) {
            StateMachine.AddFloatParameter(name, defaultValue);
            Parameters[name] = AnimatorParameterValue::CreateFloat(defaultValue);
        }

        /**
         * @brief Add a bool parameter
         */
        void AddBoolParameter(const std::string& name, bool defaultValue = false) {
            StateMachine.AddBoolParameter(name, defaultValue);
            Parameters[name] = AnimatorParameterValue::CreateBool(defaultValue);
        }

        /**
         * @brief Add a trigger parameter
         */
        void AddTriggerParameter(const std::string& name) {
            StateMachine.AddTriggerParameter(name);
            Parameters[name] = AnimatorParameterValue::CreateTrigger();
        }

        // ====================================================================
        // Playback Control
        // ====================================================================

        /**
         * @brief Force transition to a specific state
         * @param stateName Target state name
         * @param transitionDuration Blend time (0 for instant)
         * @return true if state exists and transition started
         */
        bool ForceState(const std::string& stateName, float transitionDuration = 0.0f) {
            const auto* state = StateMachine.FindState(stateName);
            if (!state) return false;

            if (transitionDuration > 0.0f && !RuntimeState.CurrentStateName.empty()) {
                // Start transition
                RuntimeState.PreviousStateName = RuntimeState.CurrentStateName;
                RuntimeState.PreviousStateTime = RuntimeState.CurrentStateTime;
                RuntimeState.CurrentTransition.Progress = 0.0f;
                RuntimeState.CurrentTransition.IsComplete = false;
                // Create temporary transition data
                static AnimationTransition tempTransition;
                tempTransition.TransitionDuration = transitionDuration;
                RuntimeState.CurrentTransition.Transition = &tempTransition;
            }

            RuntimeState.CurrentStateName = stateName;
            RuntimeState.CurrentStateTime = 0.0f;
            RuntimeState.NormalizedTime = 0.0f;
            return true;
        }

        /**
         * @brief Get the current state name
         */
        const std::string& GetCurrentStateName() const {
            return RuntimeState.CurrentStateName;
        }

        /**
         * @brief Get the current state (or nullptr if not set)
         */
        const AnimationState* GetCurrentState() const {
            return StateMachine.FindState(RuntimeState.CurrentStateName);
        }

        /**
         * @brief Check if currently transitioning between states
         */
        bool IsTransitioning() const {
            return RuntimeState.CurrentTransition.IsActive();
        }

        /**
         * @brief Get current transition progress (0-1)
         */
        float GetTransitionProgress() const {
            return RuntimeState.CurrentTransition.Progress;
        }

        /**
         * @brief Pause animation playback
         */
        void Pause() {
            RuntimeState.IsPlaying = false;
        }

        /**
         * @brief Resume animation playback
         */
        void Resume() {
            RuntimeState.IsPlaying = true;
        }

        /**
         * @brief Check if playing
         */
        bool IsPlaying() const {
            return RuntimeState.IsPlaying;
        }

        /**
         * @brief Reset to initial state
         */
        void Reset() {
            RuntimeState.Reset();
            InitializeParameters();
            RuntimeState.CurrentStateName = StateMachine.DefaultStateName;
            RuntimeState.HasInitialized = true;
        }

        // ====================================================================
        // Validation
        // ====================================================================

        /**
         * @brief Check if the animator is properly configured
         */
        bool IsValid() const {
            return StateMachine.IsValid();
        }

        /**
         * @brief Initialize the animator if not already done
         */
        void EnsureInitialized() {
            if (!RuntimeState.HasInitialized && StateMachine.IsValid()) {
                InitializeParameters();
                RuntimeState.CurrentStateName = StateMachine.DefaultStateName;
                RuntimeState.HasInitialized = true;
            }
        }

        // ====================================================================
        // Factory Methods
        // ====================================================================

        /**
         * @brief Create a basic locomotion state machine
         * 
         * Creates states: Idle, Walk, Run, Jump
         * Parameters: Speed (float), Jump (trigger), IsGrounded (bool)
         * 
         * Transitions:
         * - Idle -> Walk: Speed > 0.1
         * - Walk -> Idle: Speed <= 0.1
         * - Walk -> Run: Speed > 0.5
         * - Run -> Walk: Speed <= 0.5
         * - Any -> Jump: Jump trigger + IsGrounded
         * - Jump -> Idle: Animation complete (exit time 0.9)
         * 
         * @return Configured AnimatorComponent
         */
        static AnimatorComponent CreateWithLocomotion() {
            AnimationStateMachine sm("Locomotion");

            // Add parameters
            sm.AddFloatParameter("Speed", 0.0f);
            sm.AddBoolParameter("IsGrounded", true);
            sm.AddTriggerParameter("Jump");

            // Add states
            sm.AddState(AnimationState::Create("Idle", "idle", true, 1.0f)
                .SetOnEnter("OnIdleEnter")
                .SetOnExit("OnIdleExit"));

            sm.AddState(AnimationState::Create("Walk", "walk", true, 1.0f)
                .AddTimelineEvent("Footstep", 0.25f, AnimationEventType::Sound)
                .AddTimelineEvent("Footstep", 0.75f, AnimationEventType::Sound));

            sm.AddState(AnimationState::Create("Run", "run", true, 1.0f)
                .AddTimelineEvent("Footstep", 0.15f, AnimationEventType::Sound)
                .AddTimelineEvent("Footstep", 0.65f, AnimationEventType::Sound));

            sm.AddState(AnimationState::Create("Jump", "jump", false, 1.0f)
                .SetOnEnter("OnJumpStart")
                .SetOnExit("OnJumpEnd"));

            sm.SetDefaultState("Idle");

            // Add transitions

            // Idle <-> Walk based on Speed
            sm.AddTransition(
                AnimationTransition("Idle", "Walk", 0.2f)
                    .AddFloatCondition("Speed", ComparisonOp::Greater, 0.1f)
            );

            sm.AddTransition(
                AnimationTransition("Walk", "Idle", 0.2f)
                    .AddFloatCondition("Speed", ComparisonOp::LessEqual, 0.1f)
            );

            // Walk <-> Run based on Speed threshold
            sm.AddTransition(
                AnimationTransition("Walk", "Run", 0.15f)
                    .AddFloatCondition("Speed", ComparisonOp::Greater, 0.5f)
            );

            sm.AddTransition(
                AnimationTransition("Run", "Walk", 0.15f)
                    .AddFloatCondition("Speed", ComparisonOp::LessEqual, 0.5f)
            );

            // Idle <-> Run (skip walk if speed is high enough from standstill)
            sm.AddTransition(
                AnimationTransition("Idle", "Run", 0.2f)
                    .AddFloatCondition("Speed", ComparisonOp::Greater, 0.5f)
            );

            sm.AddTransition(
                AnimationTransition("Run", "Idle", 0.25f)
                    .AddFloatCondition("Speed", ComparisonOp::LessEqual, 0.1f)
            );

            // Jump from any grounded state
            sm.AddTransition(
                AnimationTransition::FromAnyState("Jump", 0.1f)
                    .AddTriggerCondition("Jump")
                    .AddBoolCondition("IsGrounded", true)
                    .SetPriority(10)  // High priority to override other transitions
            );

            // Jump -> Idle when animation completes
            sm.AddTransition(
                AnimationTransition("Jump", "Idle", 0.15f)
                    .SetExitTime(0.9f)
                    .AddBoolCondition("IsGrounded", true)
            );

            // Create component with this state machine
            return AnimatorComponent(sm);
        }

        /**
         * @brief Create an empty animator ready for configuration
         */
        static AnimatorComponent Create(const std::string& name = "Animator") {
            AnimatorComponent comp;
            comp.StateMachine.Name = name;
            return comp;
        }

        /**
         * @brief Create from an existing state machine definition
         */
        static AnimatorComponent CreateFromStateMachine(const AnimationStateMachine& sm) {
            return AnimatorComponent(sm);
        }

    private:
        /**
         * @brief Initialize parameters from state machine definition
         */
        void InitializeParameters() {
            Parameters.clear();
            for (const auto& def : StateMachine.ParameterDefinitions) {
                switch (def.Type) {
                    case AnimatorParameterType::Float:
                        Parameters[def.Name] = AnimatorParameterValue::CreateFloat(def.DefaultFloat);
                        break;
                    case AnimatorParameterType::Bool:
                        Parameters[def.Name] = AnimatorParameterValue::CreateBool(def.DefaultBool);
                        break;
                    case AnimatorParameterType::Trigger:
                        Parameters[def.Name] = AnimatorParameterValue::CreateTrigger();
                        break;
                }
            }
        }
    };

    // ========================================================================
    // Utility Functions
    // ========================================================================

    /**
     * @brief Convert ComparisonOp to string for debugging/serialization
     */
    inline const char* ComparisonOpToString(ComparisonOp op) {
        switch (op) {
            case ComparisonOp::Greater:      return ">";
            case ComparisonOp::Less:         return "<";
            case ComparisonOp::Equal:        return "==";
            case ComparisonOp::NotEqual:     return "!=";
            case ComparisonOp::GreaterEqual: return ">=";
            case ComparisonOp::LessEqual:    return "<=";
            default:                         return "?";
        }
    }

    /**
     * @brief Convert AnimatorParameterType to string
     */
    inline const char* ParameterTypeToString(AnimatorParameterType type) {
        switch (type) {
            case AnimatorParameterType::Float:   return "Float";
            case AnimatorParameterType::Bool:    return "Bool";
            case AnimatorParameterType::Trigger: return "Trigger";
            default:                             return "Unknown";
        }
    }

    /**
     * @brief Convert AnimationEventType to string
     */
    inline const char* EventTypeToString(AnimationEventType type) {
        switch (type) {
            case AnimationEventType::Custom:   return "Custom";
            case AnimationEventType::Sound:    return "Sound";
            case AnimationEventType::Particle: return "Particle";
            case AnimationEventType::Callback: return "Callback";
            default:                           return "Unknown";
        }
    }

    /**
     * @brief Convert InterruptionSource to string
     */
    inline const char* InterruptionSourceToString(InterruptionSource source) {
        switch (source) {
            case InterruptionSource::None:            return "None";
            case InterruptionSource::CurrentState:    return "CurrentState";
            case InterruptionSource::NextState:       return "NextState";
            case InterruptionSource::CurrentThenNext: return "CurrentThenNext";
            case InterruptionSource::NextThenCurrent: return "NextThenCurrent";
            default:                                  return "Unknown";
        }
    }

} // namespace ECS
} // namespace Core
