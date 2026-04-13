#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Core::Animation {

enum class DiagnosticSeverity : uint8_t {
    Info = 0,
    Warning,
    Error
};

struct AnimationDiagnostic {
    std::string Code;
    std::string Message;
    DiagnosticSeverity Severity = DiagnosticSeverity::Info;
    std::string Context;
};

enum class AnimatorParameterChannelType : uint8_t {
    Float = 0,
    Bool,
    Int,
    Trigger
};

enum class EventSyncMode : uint8_t {
    Immediate = 0,
    NextUpdate,
    TransitionBoundary
};

struct AnimationGraphLayerDefinition {
    std::string Name;
    int32_t LayerIndex = 0;
    bool Additive = false;
    float DefaultWeight = 1.0f;
};

struct AnimationGraphStateDefinition {
    std::string Id;
    std::string Clip;
    float Speed = 1.0f;
    bool Loop = true;
    std::string BlendTreeId;
    std::string LayerName;
};

struct AnimationGraphTransitionCondition {
    std::string ParameterName;
    std::string Operator;
    AnimatorParameterChannelType ValueType = AnimatorParameterChannelType::Float;
    float FloatValue = 0.0f;
    bool BoolValue = false;
    int32_t IntValue = 0;
    std::string StringValue;
};

struct AnimationGraphTransitionDefinition {
    std::string Id;
    std::string SourceState;
    std::string TargetState;
    float Duration = 0.25f;
    std::string CurveType = "linear";
    int32_t Priority = 0;
    std::vector<AnimationGraphTransitionCondition> Conditions;
};

struct AnimationGraphBuildRequest {
    std::string GraphName;
    std::string DefaultState;
    std::vector<AnimationGraphLayerDefinition> Layers;
    std::vector<AnimationGraphStateDefinition> States;
    std::vector<AnimationGraphTransitionDefinition> Transitions;
};

struct AnimationGraphCompiledLayer {
    std::string Name;
    int32_t LayerIndex = 0;
    bool Additive = false;
    float DefaultWeight = 1.0f;
};

struct AnimationGraphCompiledState {
    std::string Id;
    std::string Clip;
    int32_t LayerIndex = 0;
    float Speed = 1.0f;
    bool Loop = true;
    std::string BlendTreeId;
};

struct AnimationGraphCompiledTransition {
    std::string Id;
    uint32_t SourceStateIndex = 0;
    uint32_t TargetStateIndex = 0;
    float Duration = 0.25f;
    std::string CurveType = "linear";
    int32_t Priority = 0;
    std::vector<AnimationGraphTransitionCondition> Conditions;
};

struct AnimationStateMachineGraph {
    std::string GraphName;
    std::string DefaultStateId;
    uint32_t DefaultStateIndex = 0;
    std::vector<AnimationGraphCompiledLayer> Layers;
    std::vector<AnimationGraphCompiledState> States;
    std::vector<AnimationGraphCompiledTransition> Transitions;
    std::unordered_map<uint32_t, std::vector<uint32_t>> OutgoingTransitions;
};

struct AnimationGraphBuildResult {
    bool Success = false;
    AnimationStateMachineGraph Graph;
    std::vector<AnimationDiagnostic> Diagnostics;
};

enum class AnimationBlendTreeNodeType : uint8_t {
    Clip = 0,
    Blend1D,
    Blend2D,
    Additive,
    Override
};

struct AnimationBlendTreeNodeDefinition {
    std::string Id;
    AnimationBlendTreeNodeType Type = AnimationBlendTreeNodeType::Clip;
    std::string Clip;
    float PositionX = 0.0f;
    float PositionY = 0.0f;
    std::vector<std::string> Children;
    float AdditiveWeight = 1.0f;
};

struct AnimationBlendTreeBuildRequest {
    std::string TreeName;
    std::string ParameterX;
    std::string ParameterY;
    float MinX = 0.0f;
    float MaxX = 1.0f;
    float MinY = 0.0f;
    float MaxY = 1.0f;
    std::vector<AnimationBlendTreeNodeDefinition> Nodes;
    std::string RootNodeId;
};

struct AnimationBlendTreeRuntimeNode {
    std::string Id;
    AnimationBlendTreeNodeType Type = AnimationBlendTreeNodeType::Clip;
    std::string Clip;
    float PositionX = 0.0f;
    float PositionY = 0.0f;
    std::vector<uint32_t> ChildIndices;
    float AdditiveWeight = 1.0f;
};

struct AnimationBlendTree {
    std::string TreeName;
    std::string ParameterX;
    std::string ParameterY;
    float MinX = 0.0f;
    float MaxX = 1.0f;
    float MinY = 0.0f;
    float MaxY = 1.0f;
    uint32_t RootNodeIndex = 0;
    std::vector<AnimationBlendTreeRuntimeNode> Nodes;
    std::unordered_map<std::string, uint32_t> NodeIdToIndex;
};

struct AnimationBlendTreeBuildResult {
    bool Success = false;
    AnimationBlendTree BlendTree;
    std::vector<AnimationDiagnostic> Diagnostics;
};

struct AnimatorParameterSetRequest {
    std::string ParameterName;
    AnimatorParameterChannelType ParameterType = AnimatorParameterChannelType::Float;
    EventSyncMode EventMode = EventSyncMode::NextUpdate;
    std::optional<float> FloatValue;
    std::optional<bool> BoolValue;
    std::optional<int32_t> IntValue;
};

struct AnimatorParameterSetResult {
    bool Success = false;
    bool TriggerConsumed = false;
    std::string ErrorCode;
    std::string Message;
    EventSyncMode AppliedEventMode = EventSyncMode::NextUpdate;
    std::vector<AnimationDiagnostic> Diagnostics;
};

enum class RetargetTransformPolicy : uint8_t {
    Copy = 0,
    Relative,
    Scaled
};

enum class RetargetUnmappedBonePolicy : uint8_t {
    KeepBind = 0,
    InheritParent,
    Skip
};

struct RetargetBoneMapRule {
    std::string SourceBone;
    std::string TargetBone;
    RetargetTransformPolicy RotationPolicy = RetargetTransformPolicy::Relative;
    RetargetTransformPolicy TranslationPolicy = RetargetTransformPolicy::Relative;
};

struct RetargetAnimationRequest {
    std::string SourceClip;
    std::string SourceSkeleton;
    std::string TargetSkeleton;
    std::string ProfileId;
    bool PreserveRootMotion = true;
    bool NormalizeScale = false;
    std::string OutputClipName;
    std::vector<RetargetBoneMapRule> BoneMapRules;
    RetargetUnmappedBonePolicy UnmappedBonePolicy = RetargetUnmappedBonePolicy::Skip;
};

struct RetargetAnimationResult {
    bool Success = false;
    std::string OutputClipName;
    uint32_t MappedBoneCount = 0;
    bool PreservedRootMotion = false;
    bool NormalizedScale = false;
    std::vector<AnimationDiagnostic> Diagnostics;
};

struct BakeControlRigRequest {
    std::string RigId;
    std::string TargetSkeleton;
    float SampleRateHz = 30.0f;
    float StartTimeSec = 0.0f;
    float EndTimeSec = 0.0f;
    float KeyReductionTolerance = 0.0f;
    std::string OutputClipName;
    bool PreserveEventMarkers = true;
};

struct BakeControlRigResult {
    bool Success = false;
    std::string OutputClipName;
    uint32_t SampleCount = 0;
    uint32_t GeneratedKeyCount = 0;
    std::vector<AnimationDiagnostic> Diagnostics;
};

struct MotionTrajectorySample {
    float Time = 0.0f;
    float PosX = 0.0f;
    float PosZ = 0.0f;
    float FacingYaw = 0.0f;
};

struct MotionMatchingQuery {
    std::string DatabaseId;
    std::string CurrentPoseId;
    std::vector<MotionTrajectorySample> Trajectory;
    std::array<float, 3> DesiredVelocity{0.0f, 0.0f, 0.0f};
    uint32_t MaxCandidates = 32;
    float ContinuityWeight = 1.0f;
    float QueryBudgetMs = 0.25f;
};

struct MotionMatchingCandidate {
    std::string PoseId;
    float Score = 0.0f;
    float Distance = 0.0f;
    float ContinuityPenalty = 0.0f;
};

struct MotionMatchingResult {
    bool Success = false;
    bool UsedFallback = false;
    std::string FallbackReason;
    std::string SelectedPoseId;
    float SelectedScore = 0.0f;
    float QueryTimeMs = 0.0f;
    std::vector<MotionMatchingCandidate> Candidates;
    std::vector<AnimationDiagnostic> Diagnostics;
};

struct AnimationRuntimeEvent {
    std::string EventName;
    std::string Severity;
    std::string Details;
    uint64_t FrameIndex = 0;
};

struct AnimationRuntimeDiagnosticsState {
    std::string ActiveGraphId;
    std::string ActiveStateName;
    std::string ActiveMotionDatabaseId;
    float LastMotionQueryTimeMs = 0.0f;
    float LastMotionBestScore = 0.0f;
    bool LastMotionUsedFallback = false;
    std::vector<AnimationRuntimeEvent> RecentEvents;
};

} // namespace Core::Animation
