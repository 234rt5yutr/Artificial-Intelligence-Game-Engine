#pragma once

// ============================================================================
// AnimatorComponent.h
// Data-driven Animation State Machine for controlling skeletal animations
// 
// Features:
// - Parameter system (Float, Bool, Int, Trigger)
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
        Int,            ///< Integer value (e.g., enum-like stance/weapon state)
        Trigger         ///< One-shot trigger, auto-resets after consumption
    };

    /**
     * @brief Runtime value storage for animator parameters
     * Uses std::variant for type-safe storage of different parameter types
     */
    struct AnimatorParameterValue {
        std::variant<float, bool, int32_t> Value;
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

        static AnimatorParameterValue CreateInt(int32_t value) {
            AnimatorParameterValue param;
            param.Value = value;
            param.Type = AnimatorParameterType::Int;
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

        int32_t GetInt() const {
            return Type == AnimatorParameterType::Int ? std::get<int32_t>(Value) : 0;
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
        int32_t DefaultInt = 0;

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
                case AnimatorParameterType::Int: {
                    const float value = static_cast<float>(param.GetInt());
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
    // Animation Blending System
    // ========================================================================

    /**
     * @brief Types of nodes in a blend tree
     * 
     * Defines how animations are combined within a blend tree structure.
     * Each type has specific blending behavior and parameter requirements.
     */
    enum class BlendTreeNodeType : uint8_t {
        Clip = 0,       ///< Single animation clip (leaf node)
        Blend1D,        ///< 1D blend space (e.g., walk to run by speed)
        Blend2D,        ///< 2D blend space (e.g., movement direction)
        Additive,       ///< Additive blend on top of base animation
        Override        ///< Override specific bones with this animation
    };

    /**
     * @brief Convert BlendTreeNodeType to string for debugging/serialization
     */
    inline const char* BlendTreeNodeTypeToString(BlendTreeNodeType type) {
        switch (type) {
            case BlendTreeNodeType::Clip:     return "Clip";
            case BlendTreeNodeType::Blend1D:  return "Blend1D";
            case BlendTreeNodeType::Blend2D:  return "Blend2D";
            case BlendTreeNodeType::Additive: return "Additive";
            case BlendTreeNodeType::Override: return "Override";
            default:                          return "Unknown";
        }
    }

    /**
     * @brief A node within a blend tree structure
     * 
     * Blend tree nodes can be either leaf nodes (animation clips) or
     * branch nodes (blend operations that combine child nodes).
     * 
     * For Clip type: AnimationClipName specifies the animation to play
     * For Blend1D/Blend2D: ChildIndices contains indices of child nodes
     * For Additive: Combines with BasePoseClip using AdditiveWeight
     * 
     * @note PositionX/PositionY define the node's position in blend space,
     *       used by parent blend nodes to calculate interpolation weights.
     */
    struct BlendTreeNode {
        BlendTreeNodeType Type = BlendTreeNodeType::Clip;
        std::string AnimationClipName;      ///< Animation clip name (for Clip type)
        
        // Blend position in parameter space
        float PositionX = 0.0f;             ///< Position in blend space X axis
        float PositionY = 0.0f;             ///< Position in blend space Y axis (for 2D blending)
        
        // Child nodes for blend types
        std::vector<int32_t> ChildIndices;  ///< Indices into BlendTree::Nodes array
        
        // Runtime computed weight
        float ComputedWeight = 0.0f;        ///< Weight calculated during blend evaluation
        
        // For additive blending
        std::string BasePoseClip;           ///< Reference pose for additive blending
        float AdditiveWeight = 1.0f;        ///< Blend weight for additive layer (0-1)
        
        BlendTreeNode() = default;
        
        /**
         * @brief Create a clip node at a specific blend space position
         * @param clipName Name of the animation clip
         * @param posX X position in blend space
         * @param posY Y position in blend space (for 2D blending)
         * @return Configured BlendTreeNode
         */
        static BlendTreeNode CreateClip(const std::string& clipName, float posX = 0.0f, float posY = 0.0f) {
            BlendTreeNode node;
            node.Type = BlendTreeNodeType::Clip;
            node.AnimationClipName = clipName;
            node.PositionX = posX;
            node.PositionY = posY;
            return node;
        }
        
        /**
         * @brief Create a 1D blend node (children blended by single parameter)
         * @return Configured BlendTreeNode for 1D blending
         */
        static BlendTreeNode CreateBlend1D() {
            BlendTreeNode node;
            node.Type = BlendTreeNodeType::Blend1D;
            return node;
        }
        
        /**
         * @brief Create a 2D blend node (children blended by two parameters)
         * @return Configured BlendTreeNode for 2D blending
         */
        static BlendTreeNode CreateBlend2D() {
            BlendTreeNode node;
            node.Type = BlendTreeNodeType::Blend2D;
            return node;
        }
        
        /**
         * @brief Create an additive blend node
         * @param clipName Additive animation clip to apply
         * @param basePose Reference pose clip for additive calculation
         * @return Configured BlendTreeNode for additive blending
         */
        static BlendTreeNode CreateAdditive(const std::string& clipName, const std::string& basePose) {
            BlendTreeNode node;
            node.Type = BlendTreeNodeType::Additive;
            node.AnimationClipName = clipName;
            node.BasePoseClip = basePose;
            return node;
        }
        
        /**
         * @brief Create an override node for partial body animation
         * @param clipName Animation clip to use for override
         * @return Configured BlendTreeNode for override blending
         */
        static BlendTreeNode CreateOverride(const std::string& clipName) {
            BlendTreeNode node;
            node.Type = BlendTreeNodeType::Override;
            node.AnimationClipName = clipName;
            return node;
        }
    };

    /**
     * @brief A complete blend tree for complex animation blending
     * 
     * Blend trees allow smooth transitions between multiple animations
     * based on continuous parameters (e.g., speed, direction).
     * 
     * The tree is stored as a flat array of nodes, with RootNodeIndex
     * pointing to the top-level blend node. Each blend node references
     * its children by index in the Nodes array.
     * 
     * @code
     * // Create a locomotion blend tree
     * auto tree = BlendTree::CreateLocomotion1D("idle", "walk", "run");
     * tree.ComputeWeights(currentSpeed);  // Update weights based on parameter
     * @endcode
     */
    struct BlendTree {
        std::string Name;                   ///< Blend tree identifier
        std::vector<BlendTreeNode> Nodes;   ///< All nodes in the tree (flat storage)
        int32_t RootNodeIndex = 0;          ///< Index of the root blend node
        
        // Parameters that drive this blend tree
        std::string ParameterX;             ///< Parameter name for X axis (1D and 2D)
        std::string ParameterY;             ///< Parameter name for Y axis (2D only)
        
        // Blend space bounds (for parameter normalization)
        float MinX = 0.0f;                  ///< Minimum X parameter value
        float MaxX = 1.0f;                  ///< Maximum X parameter value
        float MinY = 0.0f;                  ///< Minimum Y parameter value
        float MaxY = 1.0f;                  ///< Maximum Y parameter value
        
        BlendTree() = default;
        
        /**
         * @brief Add a node to the blend tree
         * @param node The node to add
         * @return Index of the added node
         */
        int32_t AddNode(const BlendTreeNode& node) {
            int32_t index = static_cast<int32_t>(Nodes.size());
            Nodes.push_back(node);
            return index;
        }
        
        /**
         * @brief Set child nodes for a parent blend node
         * @param parentIndex Index of the parent node
         * @param children Vector of child node indices
         */
        void SetChildNodes(int32_t parentIndex, const std::vector<int32_t>& children) {
            if (parentIndex >= 0 && parentIndex < static_cast<int32_t>(Nodes.size())) {
                Nodes[parentIndex].ChildIndices = children;
            }
        }
        
        /**
         * @brief Compute blend weights for all nodes based on parameters
         * 
         * For 1D blending: Uses linear interpolation between adjacent clips
         * For 2D blending: Uses inverse distance weighting (Shepard's method)
         * 
         * @param paramX Current X parameter value
         * @param paramY Current Y parameter value (ignored for 1D blending)
         */
        void ComputeWeights(float paramX, float paramY = 0.0f) {
            // Reset all weights
            for (auto& node : Nodes) {
                node.ComputedWeight = 0.0f;
            }
            
            if (Nodes.empty() || RootNodeIndex < 0 || 
                RootNodeIndex >= static_cast<int32_t>(Nodes.size())) {
                return;
            }
            
            // Normalize parameters to blend space
            float normalizedX = (MaxX > MinX) ? 
                (paramX - MinX) / (MaxX - MinX) : 0.0f;
            float normalizedY = (MaxY > MinY) ? 
                (paramY - MinY) / (MaxY - MinY) : 0.0f;
            
            // Clamp to [0, 1]
            normalizedX = std::max(0.0f, std::min(1.0f, normalizedX));
            normalizedY = std::max(0.0f, std::min(1.0f, normalizedY));
            
            // Start recursive weight computation from root
            ComputeNodeWeights(RootNodeIndex, 1.0f, normalizedX, normalizedY);
        }
        
        /**
         * @brief Check if the blend tree is valid for use
         * @return true if tree has nodes and valid root index
         */
        bool IsValid() const {
            return !Nodes.empty() && 
                   RootNodeIndex >= 0 && 
                   RootNodeIndex < static_cast<int32_t>(Nodes.size());
        }
        
        /**
         * @brief Get all clip nodes with non-zero weights
         * @return Vector of pairs (clip name, weight)
         */
        std::vector<std::pair<std::string, float>> GetActiveClips() const {
            std::vector<std::pair<std::string, float>> result;
            for (const auto& node : Nodes) {
                if (node.Type == BlendTreeNodeType::Clip && 
                    node.ComputedWeight > 0.0f) {
                    result.emplace_back(node.AnimationClipName, node.ComputedWeight);
                }
            }
            return result;
        }
        
        // ====================================================================
        // Factory Methods for Common Presets
        // ====================================================================
        
        /**
         * @brief Create a 1D locomotion blend tree (idle -> walk -> run)
         * 
         * Blends between three animation clips based on a single speed parameter.
         * - Speed 0.0: idle
         * - Speed 0.5: walk
         * - Speed 1.0: run
         * 
         * @param idleClip Idle animation clip name
         * @param walkClip Walk animation clip name
         * @param runClip Run animation clip name
         * @return Configured BlendTree
         */
        static BlendTree CreateLocomotion1D(const std::string& idleClip, 
                                            const std::string& walkClip, 
                                            const std::string& runClip) {
            BlendTree tree;
            tree.Name = "Locomotion1D";
            tree.ParameterX = "Speed";
            tree.MinX = 0.0f;
            tree.MaxX = 1.0f;
            
            // Add clip nodes at their blend positions
            int32_t idleIdx = tree.AddNode(BlendTreeNode::CreateClip(idleClip, 0.0f));
            int32_t walkIdx = tree.AddNode(BlendTreeNode::CreateClip(walkClip, 0.5f));
            int32_t runIdx = tree.AddNode(BlendTreeNode::CreateClip(runClip, 1.0f));
            
            // Add root blend node
            BlendTreeNode rootNode = BlendTreeNode::CreateBlend1D();
            tree.RootNodeIndex = tree.AddNode(rootNode);
            tree.SetChildNodes(tree.RootNodeIndex, {idleIdx, walkIdx, runIdx});
            
            return tree;
        }
        
        /**
         * @brief Create a 2D directional blend tree (forward/backward/left/right)
         * 
         * Blends between four animation clips based on movement direction.
         * - (0, 1): forward
         * - (0, -1): backward
         * - (-1, 0): left
         * - (1, 0): right
         * 
         * @param forwardClip Forward movement animation
         * @param backwardClip Backward movement animation
         * @param leftClip Left strafe animation
         * @param rightClip Right strafe animation
         * @return Configured BlendTree
         */
        static BlendTree CreateDirectional2D(const std::string& forwardClip,
                                             const std::string& backwardClip,
                                             const std::string& leftClip,
                                             const std::string& rightClip) {
            BlendTree tree;
            tree.Name = "Directional2D";
            tree.ParameterX = "DirectionX";
            tree.ParameterY = "DirectionY";
            tree.MinX = -1.0f;
            tree.MaxX = 1.0f;
            tree.MinY = -1.0f;
            tree.MaxY = 1.0f;
            
            // Add clip nodes at directional positions (normalized to [0,1] internally)
            // Note: Positions are in the original parameter space [-1, 1]
            int32_t forwardIdx = tree.AddNode(BlendTreeNode::CreateClip(forwardClip, 0.0f, 1.0f));
            int32_t backwardIdx = tree.AddNode(BlendTreeNode::CreateClip(backwardClip, 0.0f, -1.0f));
            int32_t leftIdx = tree.AddNode(BlendTreeNode::CreateClip(leftClip, -1.0f, 0.0f));
            int32_t rightIdx = tree.AddNode(BlendTreeNode::CreateClip(rightClip, 1.0f, 0.0f));
            
            // Add root blend node
            BlendTreeNode rootNode = BlendTreeNode::CreateBlend2D();
            tree.RootNodeIndex = tree.AddNode(rootNode);
            tree.SetChildNodes(tree.RootNodeIndex, {forwardIdx, backwardIdx, leftIdx, rightIdx});
            
            return tree;
        }
        
        /**
         * @brief Create a 2D directional blend tree with 8 directions + center
         * 
         * Full directional movement including diagonals and idle.
         * 
         * @param idleClip Idle/stationary animation
         * @param forwardClip Forward movement animation
         * @param backwardClip Backward movement animation
         * @param leftClip Left strafe animation
         * @param rightClip Right strafe animation
         * @param forwardLeftClip Forward-left diagonal animation
         * @param forwardRightClip Forward-right diagonal animation
         * @param backwardLeftClip Backward-left diagonal animation
         * @param backwardRightClip Backward-right diagonal animation
         * @return Configured BlendTree
         */
        static BlendTree CreateDirectional2DFull(const std::string& idleClip,
                                                  const std::string& forwardClip,
                                                  const std::string& backwardClip,
                                                  const std::string& leftClip,
                                                  const std::string& rightClip,
                                                  const std::string& forwardLeftClip,
                                                  const std::string& forwardRightClip,
                                                  const std::string& backwardLeftClip,
                                                  const std::string& backwardRightClip) {
            BlendTree tree;
            tree.Name = "Directional2DFull";
            tree.ParameterX = "DirectionX";
            tree.ParameterY = "DirectionY";
            tree.MinX = -1.0f;
            tree.MaxX = 1.0f;
            tree.MinY = -1.0f;
            tree.MaxY = 1.0f;
            
            constexpr float diag = 0.707f;  // sqrt(2)/2 for diagonal positions
            
            // Center
            int32_t idleIdx = tree.AddNode(BlendTreeNode::CreateClip(idleClip, 0.0f, 0.0f));
            
            // Cardinal directions
            int32_t forwardIdx = tree.AddNode(BlendTreeNode::CreateClip(forwardClip, 0.0f, 1.0f));
            int32_t backwardIdx = tree.AddNode(BlendTreeNode::CreateClip(backwardClip, 0.0f, -1.0f));
            int32_t leftIdx = tree.AddNode(BlendTreeNode::CreateClip(leftClip, -1.0f, 0.0f));
            int32_t rightIdx = tree.AddNode(BlendTreeNode::CreateClip(rightClip, 1.0f, 0.0f));
            
            // Diagonals
            int32_t flIdx = tree.AddNode(BlendTreeNode::CreateClip(forwardLeftClip, -diag, diag));
            int32_t frIdx = tree.AddNode(BlendTreeNode::CreateClip(forwardRightClip, diag, diag));
            int32_t blIdx = tree.AddNode(BlendTreeNode::CreateClip(backwardLeftClip, -diag, -diag));
            int32_t brIdx = tree.AddNode(BlendTreeNode::CreateClip(backwardRightClip, diag, -diag));
            
            // Add root blend node
            BlendTreeNode rootNode = BlendTreeNode::CreateBlend2D();
            tree.RootNodeIndex = tree.AddNode(rootNode);
            tree.SetChildNodes(tree.RootNodeIndex, 
                {idleIdx, forwardIdx, backwardIdx, leftIdx, rightIdx, flIdx, frIdx, blIdx, brIdx});
            
            return tree;
        }
        
    private:
        /**
         * @brief Recursively compute weights for a node and its children
         * @param nodeIndex Index of the node to process
         * @param parentWeight Weight inherited from parent
         * @param paramX Normalized X parameter
         * @param paramY Normalized Y parameter
         */
        void ComputeNodeWeights(int32_t nodeIndex, float parentWeight, 
                                float paramX, float paramY) {
            if (nodeIndex < 0 || nodeIndex >= static_cast<int32_t>(Nodes.size())) {
                return;
            }
            
            BlendTreeNode& node = Nodes[nodeIndex];
            
            switch (node.Type) {
                case BlendTreeNodeType::Clip:
                case BlendTreeNodeType::Override:
                    // Leaf node - just set the weight
                    node.ComputedWeight = parentWeight;
                    break;
                    
                case BlendTreeNodeType::Blend1D:
                    Compute1DBlendWeights(node, parentWeight, paramX);
                    break;
                    
                case BlendTreeNodeType::Blend2D:
                    Compute2DBlendWeights(node, parentWeight, paramX, paramY);
                    break;
                    
                case BlendTreeNodeType::Additive:
                    // Additive uses its own weight multiplied by parent
                    node.ComputedWeight = parentWeight * node.AdditiveWeight;
                    break;
            }
        }
        
        /**
         * @brief Compute weights for 1D blend using linear interpolation
         * @param node The blend node
         * @param parentWeight Weight from parent
         * @param param Normalized blend parameter [0, 1]
         */
        void Compute1DBlendWeights(BlendTreeNode& node, float parentWeight, float param) {
            if (node.ChildIndices.empty()) {
                return;
            }
            
            // Sort children by position for interpolation
            std::vector<std::pair<float, int32_t>> sortedChildren;
            for (int32_t childIdx : node.ChildIndices) {
                if (childIdx >= 0 && childIdx < static_cast<int32_t>(Nodes.size())) {
                    sortedChildren.emplace_back(Nodes[childIdx].PositionX, childIdx);
                }
            }
            
            if (sortedChildren.empty()) {
                return;
            }
            
            std::sort(sortedChildren.begin(), sortedChildren.end());
            
            // Find the two children to interpolate between
            if (sortedChildren.size() == 1) {
                // Only one child - full weight
                Nodes[sortedChildren[0].second].ComputedWeight = parentWeight;
                return;
            }
            
            // Clamp parameter to available range
            float minPos = sortedChildren.front().first;
            float maxPos = sortedChildren.back().first;
            float clampedParam = std::max(minPos, std::min(maxPos, param));
            
            // Find adjacent children
            for (size_t i = 0; i < sortedChildren.size() - 1; ++i) {
                float pos0 = sortedChildren[i].first;
                float pos1 = sortedChildren[i + 1].first;
                
                if (clampedParam >= pos0 && clampedParam <= pos1) {
                    // Interpolate between these two
                    float range = pos1 - pos0;
                    float t = (range > 0.0f) ? (clampedParam - pos0) / range : 0.0f;
                    
                    int32_t idx0 = sortedChildren[i].second;
                    int32_t idx1 = sortedChildren[i + 1].second;
                    
                    float weight0 = (1.0f - t) * parentWeight;
                    float weight1 = t * parentWeight;
                    
                    ComputeNodeWeights(idx0, weight0, param, 0.0f);
                    ComputeNodeWeights(idx1, weight1, param, 0.0f);
                    return;
                }
            }
            
            // Edge case: parameter at or beyond the last position
            Nodes[sortedChildren.back().second].ComputedWeight = parentWeight;
        }
        
        /**
         * @brief Compute weights for 2D blend using inverse distance weighting
         * 
         * Uses Shepard's method (inverse distance weighting) to smoothly
         * blend between all nearby sample points.
         * 
         * @param node The blend node
         * @param parentWeight Weight from parent
         * @param paramX Normalized X parameter
         * @param paramY Normalized Y parameter
         */
        void Compute2DBlendWeights(BlendTreeNode& node, float parentWeight, 
                                    float paramX, float paramY) {
            if (node.ChildIndices.empty()) {
                return;
            }
            
            constexpr float epsilon = 0.0001f;
            constexpr float power = 2.0f;  // Inverse distance power
            
            std::vector<float> weights(node.ChildIndices.size(), 0.0f);
            float totalWeight = 0.0f;
            bool exactMatch = false;
            int32_t exactMatchIdx = -1;
            
            // Calculate inverse distances
            for (size_t i = 0; i < node.ChildIndices.size(); ++i) {
                int32_t childIdx = node.ChildIndices[i];
                if (childIdx < 0 || childIdx >= static_cast<int32_t>(Nodes.size())) {
                    continue;
                }
                
                const BlendTreeNode& child = Nodes[childIdx];
                
                // Normalize child position to [0, 1] range for comparison
                float childX = (MaxX > MinX) ? 
                    (child.PositionX - MinX) / (MaxX - MinX) : 0.0f;
                float childY = (MaxY > MinY) ? 
                    (child.PositionY - MinY) / (MaxY - MinY) : 0.0f;
                
                float dx = paramX - childX;
                float dy = paramY - childY;
                float distSq = dx * dx + dy * dy;
                
                if (distSq < epsilon * epsilon) {
                    // Exact match or very close
                    exactMatch = true;
                    exactMatchIdx = static_cast<int32_t>(i);
                    break;
                }
                
                // Inverse distance weight
                float dist = std::sqrt(distSq);
                weights[i] = 1.0f / std::pow(dist, power);
                totalWeight += weights[i];
            }
            
            if (exactMatch && exactMatchIdx >= 0) {
                // Single child gets all weight
                int32_t childIdx = node.ChildIndices[exactMatchIdx];
                ComputeNodeWeights(childIdx, parentWeight, paramX, paramY);
                return;
            }
            
            // Normalize and apply weights
            if (totalWeight > epsilon) {
                for (size_t i = 0; i < node.ChildIndices.size(); ++i) {
                    if (weights[i] > 0.0f) {
                        int32_t childIdx = node.ChildIndices[i];
                        float normalizedWeight = (weights[i] / totalWeight) * parentWeight;
                        ComputeNodeWeights(childIdx, normalizedWeight, paramX, paramY);
                    }
                }
            }
        }
    };

    /**
     * @brief An animation layer for layered animation blending
     * 
     * Layers allow multiple animations to play simultaneously on different
     * parts of the skeleton (e.g., upper body aiming while lower body walks).
     * 
     * Each layer has its own state machine or blend tree and can:
     * - Affect all bones (AffectedBones empty) or specific bones
     * - Blend additively or override the base pose
     * - Have independent weight for smooth transitions
     * 
     * @code
     * AnimationLayer upperBody("UpperBody", 1);
     * upperBody.AffectedBones = {"Spine", "Chest", "Head", "LeftArm", "RightArm"};
     * upperBody.IsAdditive = false;  // Override blending
     * @endcode
     */
    struct AnimationLayer {
        std::string Name;                           ///< Layer identifier
        int32_t LayerIndex = 0;                     ///< Layer order (0 = base layer)
        float Weight = 1.0f;                        ///< Blend weight [0-1]
        
        // Bone mask - which bones this layer affects
        std::vector<std::string> AffectedBones;    ///< Empty = all bones affected
        bool IsAdditive = false;                    ///< true = additive, false = override
        
        // The state machine or blend tree for this layer
        std::string StateMachineName;               ///< Reference to state machine
        
        // Runtime state
        std::string CurrentStateName;               ///< Currently active state
        float CurrentTime = 0.0f;                   ///< Playback time in current state
        
        // Blend tree (optional - alternative to state machine)
        std::optional<BlendTree> LayerBlendTree;    ///< Blend tree for this layer
        
        AnimationLayer() = default;
        
        /**
         * @brief Construct a named layer at a specific index
         * @param name Layer identifier
         * @param index Layer order (higher = processed later)
         */
        AnimationLayer(const std::string& name, int32_t index) 
            : Name(name), LayerIndex(index) {}
        
        /**
         * @brief Set the bones affected by this layer
         * @param bones List of bone names
         * @return Reference to this for chaining
         */
        AnimationLayer& SetAffectedBones(const std::vector<std::string>& bones) {
            AffectedBones = bones;
            return *this;
        }
        
        /**
         * @brief Set the layer as additive
         * @param additive true for additive blending
         * @return Reference to this for chaining
         */
        AnimationLayer& SetAdditive(bool additive) {
            IsAdditive = additive;
            return *this;
        }
        
        /**
         * @brief Set the layer weight
         * @param weight Blend weight [0-1]
         * @return Reference to this for chaining
         */
        AnimationLayer& SetWeight(float weight) {
            Weight = std::max(0.0f, std::min(1.0f, weight));
            return *this;
        }
        
        /**
         * @brief Assign a blend tree to this layer
         * @param tree The blend tree to use
         * @return Reference to this for chaining
         */
        AnimationLayer& SetBlendTree(const BlendTree& tree) {
            LayerBlendTree = tree;
            return *this;
        }
        
        /**
         * @brief Check if a specific bone is affected by this layer
         * @param boneName Name of the bone to check
         * @return true if bone is affected (or if all bones are affected)
         */
        bool AffectsBone(const std::string& boneName) const {
            if (AffectedBones.empty()) {
                return true;  // Empty means all bones
            }
            return std::find(AffectedBones.begin(), AffectedBones.end(), boneName) 
                   != AffectedBones.end();
        }
        
        /**
         * @brief Check if layer has a blend tree assigned
         */
        bool HasBlendTree() const {
            return LayerBlendTree.has_value();
        }
    };

    // ========================================================================
    // Animation State
    // ========================================================================

    /**
     * @brief A single state in the animation state machine
     * 
     * Represents one animation that can be played (e.g., Idle, Walk, Run)
     * Contains playback settings and associated events.
     * 
     * Can optionally contain a BlendTree for complex multi-animation states.
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

        // Blend tree support
        std::optional<BlendTree> BlendTreeData;  ///< Optional blend tree for this state
        std::string BlendParameterX;        ///< Parameter name for 1D/2D blend X axis
        std::string BlendParameterY;        ///< Parameter name for 2D blend Y axis
        
        /**
         * @brief Check if this state uses a blend tree
         * @return true if BlendTreeData is present and valid
         */
        bool IsBlendTree() const { 
            return BlendTreeData.has_value() && BlendTreeData->IsValid(); 
        }

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
        
        /**
         * @brief Assign a blend tree to this state
         * 
         * When a blend tree is assigned, this state will blend multiple
         * animation clips based on the blend tree's parameters.
         * 
         * @param tree The blend tree to use
         * @return Reference to this for method chaining
         */
        AnimationState& SetBlendTree(const BlendTree& tree) {
            BlendTreeData = tree;
            BlendParameterX = tree.ParameterX;
            BlendParameterY = tree.ParameterY;
            return *this;
        }
        
        /**
         * @brief Create a state with a 1D locomotion blend tree
         * @param name State name
         * @param idleClip Idle animation clip
         * @param walkClip Walk animation clip
         * @param runClip Run animation clip
         * @return Configured AnimationState with blend tree
         */
        static AnimationState CreateWithLocomotionBlend(const std::string& name,
                                                        const std::string& idleClip,
                                                        const std::string& walkClip,
                                                        const std::string& runClip) {
            AnimationState state;
            state.Name = name;
            state.SetBlendTree(BlendTree::CreateLocomotion1D(idleClip, walkClip, runClip));
            return state;
        }
        
        /**
         * @brief Create a state with a 2D directional blend tree
         * @param name State name
         * @param forwardClip Forward animation clip
         * @param backwardClip Backward animation clip
         * @param leftClip Left strafe animation clip
         * @param rightClip Right strafe animation clip
         * @return Configured AnimationState with blend tree
         */
        static AnimationState CreateWithDirectionalBlend(const std::string& name,
                                                         const std::string& forwardClip,
                                                         const std::string& backwardClip,
                                                         const std::string& leftClip,
                                                         const std::string& rightClip) {
            AnimationState state;
            state.Name = name;
            state.SetBlendTree(BlendTree::CreateDirectional2D(forwardClip, backwardClip, 
                                                              leftClip, rightClip));
            return state;
        }
    };

    // ========================================================================
    // Animation Transition
    // ========================================================================

    /**
     * @brief Transition curve types for smooth eased transitions
     * 
     * These curves control how the blend weight progresses during a transition,
     * allowing for more natural-looking animation blending than linear interpolation.
     */
    enum class TransitionCurveType : uint8_t {
        Linear = 0,     ///< Linear interpolation (constant velocity)
        EaseIn,         ///< Slow start, fast end (accelerating)
        EaseOut,        ///< Fast start, slow end (decelerating)
        EaseInOut,      ///< Slow start and end, fast middle (S-curve)
        Cubic           ///< Smooth cubic interpolation (Hermite)
    };

    /**
     * @brief Convert TransitionCurveType to string for debugging/serialization
     */
    inline const char* TransitionCurveTypeToString(TransitionCurveType type) {
        switch (type) {
            case TransitionCurveType::Linear:    return "Linear";
            case TransitionCurveType::EaseIn:    return "EaseIn";
            case TransitionCurveType::EaseOut:   return "EaseOut";
            case TransitionCurveType::EaseInOut: return "EaseInOut";
            case TransitionCurveType::Cubic:     return "Cubic";
            default:                             return "Unknown";
        }
    }

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
        
        /// Curve type for transition blending (default: linear)
        TransitionCurveType CurveType = TransitionCurveType::Linear;

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
         * @brief Set the transition curve type for eased blending
         * @param curve The curve type to use
         * @return Reference to this for method chaining
         */
        AnimationTransition& SetCurveType(TransitionCurveType curve) {
            CurveType = curve;
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

        AnimationStateMachine& AddIntParameter(const std::string& name, int32_t defaultValue = 0) {
            AnimatorParameterDefinition param;
            param.Name = name;
            param.Type = AnimatorParameterType::Int;
            param.DefaultInt = defaultValue;
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

    enum class AnimatorRuntimeMode : uint8_t {
        Legacy = 0,
        Hybrid,
        Graph
    };

    /**
     * @brief ECS component for animation state machine control
     * 
     * Provides a data-driven animation state machine that controls
     * animation playback on an associated SkeletalMeshComponent.
     * 
     * Supports:
     * - Multiple animation layers (e.g., base body + upper body override)
     * - Blend trees for smooth multi-animation blending
     * - Parameter-driven state transitions
     * - Root motion extraction
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
     * // Add an upper body layer for aiming
     * auto& aimLayer = animator.AddLayer("UpperBody");
     * aimLayer.SetAffectedBones({"Spine", "Chest", "Head", "LeftArm", "RightArm"});
     * 
     * // In game logic, set parameters to control animation
     * animator.SetFloat("Speed", characterSpeed);
     * animator.SetBool("IsGrounded", physics.IsOnGround());
     * animator.SetTrigger("Jump");  // One-shot transition
     * animator.SetLayerWeight("UpperBody", isAiming ? 1.0f : 0.0f);
     * @endcode
     */
    struct AnimatorComponent {
        // State machine definition (shared blueprint)
        AnimationStateMachine StateMachine;

        // Runtime parameter values
        std::unordered_map<std::string, AnimatorParameterValue> Parameters;

        // Runtime playback state
        AnimatorRuntimeState RuntimeState;

        // Stage 25 runtime controls
        AnimatorRuntimeMode RuntimeMode = AnimatorRuntimeMode::Legacy;
        std::string ActiveGraphId;
        std::string ActiveBlendTreeId;
        std::string MotionDatabaseId;
        bool MotionMatchingEnabled = false;
        uint64_t GraphEvaluationCount = 0;
        uint64_t LegacyFallbackCount = 0;
        uint64_t OrchestrationConflictCount = 0;
        
        // Animation layers for layered blending
        std::vector<AnimationLayer> Layers;

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

        bool SetInt(const std::string& name, int32_t value) {
            auto it = Parameters.find(name);
            if (it != Parameters.end() && it->second.Type == AnimatorParameterType::Int) {
                it->second.Value = value;
                return true;
            }
            return false;
        }

        int32_t GetInt(const std::string& name) const {
            auto it = Parameters.find(name);
            if (it != Parameters.end() && it->second.Type == AnimatorParameterType::Int) {
                return it->second.GetInt();
            }
            return 0;
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

        void AddIntParameter(const std::string& name, int32_t defaultValue = 0) {
            StateMachine.AddIntParameter(name, defaultValue);
            Parameters[name] = AnimatorParameterValue::CreateInt(defaultValue);
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
            
            // Reset all layers
            for (auto& layer : Layers) {
                layer.CurrentTime = 0.0f;
                layer.CurrentStateName.clear();
            }
        }

        // ====================================================================
        // Layer Management
        // ====================================================================

        /**
         * @brief Add a new animation layer
         * 
         * Layers are processed in order of LayerIndex. Layer 0 is typically
         * the base layer, with higher indices for overlay animations.
         * 
         * @param name Unique name for the layer
         * @return Reference to the newly created layer for configuration
         */
        AnimationLayer& AddLayer(const std::string& name) {
            // Check for duplicate
            for (auto& layer : Layers) {
                if (layer.Name == name) {
                    return layer;  // Return existing layer
                }
            }
            
            int32_t newIndex = static_cast<int32_t>(Layers.size());
            Layers.emplace_back(name, newIndex);
            
            // Keep layers sorted by index
            std::sort(Layers.begin(), Layers.end(),
                [](const AnimationLayer& a, const AnimationLayer& b) {
                    return a.LayerIndex < b.LayerIndex;
                });
            
            // Return reference to the added layer (may have moved due to sort)
            for (auto& layer : Layers) {
                if (layer.Name == name) {
                    return layer;
                }
            }
            return Layers.back();  // Shouldn't reach here
        }

        /**
         * @brief Get a layer by name
         * @param name Layer name to find
         * @return Pointer to layer or nullptr if not found
         */
        AnimationLayer* GetLayer(const std::string& name) {
            for (auto& layer : Layers) {
                if (layer.Name == name) {
                    return &layer;
                }
            }
            return nullptr;
        }

        /**
         * @brief Get a layer by name (const version)
         * @param name Layer name to find
         * @return Pointer to layer or nullptr if not found
         */
        const AnimationLayer* GetLayer(const std::string& name) const {
            for (const auto& layer : Layers) {
                if (layer.Name == name) {
                    return &layer;
                }
            }
            return nullptr;
        }

        /**
         * @brief Get a layer by index
         * @param index Layer index
         * @return Pointer to layer or nullptr if index out of range
         */
        AnimationLayer* GetLayerByIndex(int32_t index) {
            for (auto& layer : Layers) {
                if (layer.LayerIndex == index) {
                    return &layer;
                }
            }
            return nullptr;
        }

        /**
         * @brief Set the blend weight of a layer
         * 
         * @param name Layer name
         * @param weight Blend weight (0-1), clamped automatically
         * @return true if layer was found and weight set
         */
        void SetLayerWeight(const std::string& name, float weight) {
            AnimationLayer* layer = GetLayer(name);
            if (layer) {
                layer->Weight = std::max(0.0f, std::min(1.0f, weight));
            }
        }

        /**
         * @brief Get the blend weight of a layer
         * @param name Layer name
         * @return Layer weight or 0.0f if not found
         */
        float GetLayerWeight(const std::string& name) const {
            const AnimationLayer* layer = GetLayer(name);
            return layer ? layer->Weight : 0.0f;
        }

        /**
         * @brief Remove a layer by name
         * @param name Layer name to remove
         * @return true if layer was found and removed
         */
        bool RemoveLayer(const std::string& name) {
            auto it = std::find_if(Layers.begin(), Layers.end(),
                [&name](const AnimationLayer& layer) { return layer.Name == name; });
            if (it != Layers.end()) {
                Layers.erase(it);
                return true;
            }
            return false;
        }

        /**
         * @brief Get the number of animation layers
         */
        size_t GetLayerCount() const {
            return Layers.size();
        }

        /**
         * @brief Assign a blend tree to a layer
         * @param layerName Layer to assign the blend tree to
         * @param tree The blend tree
         * @return true if layer was found and blend tree assigned
         */
        bool SetLayerBlendTree(const std::string& layerName, const BlendTree& tree) {
            AnimationLayer* layer = GetLayer(layerName);
            if (layer) {
                layer->SetBlendTree(tree);
                return true;
            }
            return false;
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
                    case AnimatorParameterType::Int:
                        Parameters[def.Name] = AnimatorParameterValue::CreateInt(def.DefaultInt);
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
            case AnimatorParameterType::Int:     return "Int";
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
