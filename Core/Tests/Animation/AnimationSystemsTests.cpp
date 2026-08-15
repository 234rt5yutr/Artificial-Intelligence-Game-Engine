// AnimationSystemsTests: Unit tests for Animation State Machines, Blend Trees,
// Motion Matching Database / Runtime, and Animator Parameter Channels.

#include "Core/Animation/AnimationRuntimeTypes.h"
#include "Core/Animation/Graph/AnimationStateMachineGraph.h"
#include "Core/Animation/Graph/AnimationGraphValidator.h"
#include "Core/Animation/Blend/AnimationBlendTreeBuilder.h"
#include "Core/Animation/MotionMatching/MotionMatchingDatabase.h"
#include "Core/Animation/MotionMatching/MotionMatchingRuntime.h"
#include "Core/Animation/MotionMatching/TrajectoryPredictor.h"
#include "Core/Animation/Parameters/AnimatorParameterChannel.h"
#include "Core/ECS/Components/AnimatorComponent.h"
#include "Core/Log.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n",                 \
                         #expr, __FILE__, __LINE__);                           \
            std::abort();                                                      \
        }                                                                      \
    } while (false)

int main() {
    using namespace Core::Animation;

    Engine::Log::Init();

    // 1. Animation State Machine Graph Construction & Validation
    {
        AnimationGraphBuildRequest request;
        request.GraphName = "LocomotionStateMachine";
        request.DefaultState = "Idle";

        // Layers
        AnimationGraphLayerDefinition baseLayer;
        baseLayer.Name = "BaseLayer";
        baseLayer.LayerIndex = 0;
        baseLayer.Additive = false;
        baseLayer.DefaultWeight = 1.0f;
        request.Layers.push_back(baseLayer);

        AnimationGraphLayerDefinition upperBodyLayer;
        upperBodyLayer.Name = "UpperBodyLayer";
        upperBodyLayer.LayerIndex = 1;
        upperBodyLayer.Additive = true;
        upperBodyLayer.DefaultWeight = 0.8f;
        request.Layers.push_back(upperBodyLayer);

        // States
        AnimationGraphStateDefinition idleState;
        idleState.Id = "Idle";
        idleState.Clip = "idle.anim";
        idleState.LayerName = "BaseLayer";
        request.States.push_back(idleState);

        AnimationGraphStateDefinition walkState;
        walkState.Id = "Walk";
        walkState.Clip = "walk.anim";
        walkState.LayerName = "BaseLayer";
        request.States.push_back(walkState);

        AnimationGraphStateDefinition runState;
        runState.Id = "Run";
        runState.Clip = "run.anim";
        runState.LayerName = "BaseLayer";
        request.States.push_back(runState);

        // Transitions
        AnimationGraphTransitionDefinition idleToWalk;
        idleToWalk.Id = "Idle->Walk";
        idleToWalk.SourceState = "Idle";
        idleToWalk.TargetState = "Walk";
        idleToWalk.Duration = 0.2f;

        AnimationGraphTransitionCondition condSpeed;
        condSpeed.ParameterName = "Speed";
        condSpeed.Operator = ">";
        condSpeed.ValueType = AnimatorParameterChannelType::Float;
        condSpeed.FloatValue = 0.1f;
        idleToWalk.Conditions.push_back(condSpeed);
        request.Transitions.push_back(idleToWalk);

        AnimationGraphTransitionDefinition walkToRun;
        walkToRun.Id = "Walk->Run";
        walkToRun.SourceState = "Walk";
        walkToRun.TargetState = "Run";
        walkToRun.Duration = 0.25f;

        AnimationGraphTransitionCondition condRunSpeed;
        condRunSpeed.ParameterName = "Speed";
        condRunSpeed.Operator = ">";
        condRunSpeed.ValueType = AnimatorParameterChannelType::Float;
        condRunSpeed.FloatValue = 4.0f;
        walkToRun.Conditions.push_back(condRunSpeed);
        request.Transitions.push_back(walkToRun);

        // Build State Machine Graph
        AnimationGraphBuildResult buildResult = CreateAnimationStateMachineGraph(request);
        CHECK(buildResult.Success);
        CHECK(buildResult.Graph.GraphName == "LocomotionStateMachine");
        CHECK(buildResult.Graph.DefaultStateId == "Idle");
        CHECK(buildResult.Graph.States.size() == 3);
        CHECK(buildResult.Graph.Layers.size() == 2);
        CHECK(buildResult.Graph.Transitions.size() == 2);

        // Verify outgoing transition indexing
        uint32_t idleIndex = buildResult.Graph.DefaultStateIndex;
        CHECK(buildResult.Graph.OutgoingTransitions.contains(idleIndex));
        CHECK(buildResult.Graph.OutgoingTransitions[idleIndex].size() == 1);
    }

    // 2. Animation Blend Tree Construction
    {
        AnimationBlendTreeBuildRequest request;
        request.TreeName = "Locomotion2DBlendSpace";
        request.ParameterX = "MoveX";
        request.ParameterY = "MoveY";
        request.MinX = -1.0f;
        request.MaxX = 1.0f;
        request.MinY = -1.0f;
        request.MaxY = 1.0f;
        request.RootNodeId = "RootBlend2D";

        AnimationBlendTreeNodeDefinition rootNode;
        rootNode.Id = "RootBlend2D";
        rootNode.Type = AnimationBlendTreeNodeType::Blend2D;
        rootNode.Children = {"IdleClip", "WalkFwdClip", "WalkBackClip", "StrafeLeftClip", "StrafeRightClip"};
        request.Nodes.push_back(rootNode);

        AnimationBlendTreeNodeDefinition idleNode;
        idleNode.Id = "IdleClip";
        idleNode.Type = AnimationBlendTreeNodeType::Clip;
        idleNode.Clip = "idle.anim";
        idleNode.PositionX = 0.0f;
        idleNode.PositionY = 0.0f;
        request.Nodes.push_back(idleNode);

        AnimationBlendTreeNodeDefinition walkFwdNode;
        walkFwdNode.Id = "WalkFwdClip";
        walkFwdNode.Type = AnimationBlendTreeNodeType::Clip;
        walkFwdNode.Clip = "walk_fwd.anim";
        walkFwdNode.PositionX = 0.0f;
        walkFwdNode.PositionY = 1.0f;
        request.Nodes.push_back(walkFwdNode);

        AnimationBlendTreeNodeDefinition walkBackNode;
        walkBackNode.Id = "WalkBackClip";
        walkBackNode.Type = AnimationBlendTreeNodeType::Clip;
        walkBackNode.Clip = "walk_bwd.anim";
        walkBackNode.PositionX = 0.0f;
        walkBackNode.PositionY = -1.0f;
        request.Nodes.push_back(walkBackNode);

        AnimationBlendTreeNodeDefinition strafeLeftNode;
        strafeLeftNode.Id = "StrafeLeftClip";
        strafeLeftNode.Type = AnimationBlendTreeNodeType::Clip;
        strafeLeftNode.Clip = "strafe_left.anim";
        strafeLeftNode.PositionX = -1.0f;
        strafeLeftNode.PositionY = 0.0f;
        request.Nodes.push_back(strafeLeftNode);

        AnimationBlendTreeNodeDefinition strafeRightNode;
        strafeRightNode.Id = "StrafeRightClip";
        strafeRightNode.Type = AnimationBlendTreeNodeType::Clip;
        strafeRightNode.Clip = "strafe_right.anim";
        strafeRightNode.PositionX = 1.0f;
        strafeRightNode.PositionY = 0.0f;
        request.Nodes.push_back(strafeRightNode);

        AnimationBlendTreeBuildResult blendResult = CreateAnimationBlendTree(request);
        CHECK(blendResult.Success);
        CHECK(blendResult.BlendTree.TreeName == "Locomotion2DBlendSpace");
        CHECK(blendResult.BlendTree.Nodes.size() == 6);
        CHECK(blendResult.BlendTree.NodeIdToIndex.contains("RootBlend2D"));
        CHECK(blendResult.BlendTree.NodeIdToIndex.contains("WalkFwdClip"));
    }

    // 3. Motion Matching Database Construction, Serialization & Runtime Query
    {
        MotionMatchingDatabaseAsset db;
        db.DatabaseId = "HeroCombatLocomotion";
        db.FeatureDimension = 4;

        MotionPoseRecord pose1;
        pose1.PoseId = "pose_idle_0";
        pose1.ClipName = "hero_idle.anim";
        pose1.FrameIndex = 0;
        pose1.SampleTimeSec = 0.0f;
        pose1.Features = {0.0f, 0.0f, 0.0f, 0.0f};
        pose1.LeftFootContact = true;
        pose1.RightFootContact = true;
        db.Poses.push_back(pose1);

        MotionPoseRecord pose2;
        pose2.PoseId = "pose_walk_fwd_15";
        pose2.ClipName = "hero_walk_fwd.anim";
        pose2.FrameIndex = 15;
        pose2.SampleTimeSec = 0.5f;
        pose2.Features = {0.0f, 1.5f, 0.8f, 0.0f};
        pose2.LeftFootContact = false;
        pose2.RightFootContact = true;
        db.Poses.push_back(pose2);

        MotionPoseRecord pose3;
        pose3.PoseId = "pose_run_fwd_20";
        pose3.ClipName = "hero_run_fwd.anim";
        pose3.FrameIndex = 20;
        pose3.SampleTimeSec = 0.66f;
        pose3.Features = {0.0f, 4.5f, 1.2f, 0.0f};
        pose3.LeftFootContact = true;
        pose3.RightFootContact = false;
        db.Poses.push_back(pose3);

        std::vector<AnimationDiagnostic> valDiag;
        CHECK(ValidateMotionMatchingDatabase(db, valDiag));

        // Serialization and Deserialization check
        nlohmann::json serialized = SerializeMotionMatchingDatabase(db);
        std::vector<AnimationDiagnostic> deserDiag;
        auto deserialized = DeserializeMotionMatchingDatabase(serialized, deserDiag);
        CHECK(deserialized.has_value());
        CHECK(deserialized->DatabaseId == "HeroCombatLocomotion");
        CHECK(deserialized->Poses.size() == 3);

        // Register in Motion Matching Runtime
        RegisterMotionMatchingDatabase(db);

        // Query evaluation
        MotionMatchingQuery query;
        query.DatabaseId = "HeroCombatLocomotion";
        query.DesiredVelocity = {0.0f, 4.2f, 0.0f};
        query.Trajectory = PredictTrajectorySamples(query.DesiredVelocity, 0.6f, 0.2f);

        MotionMatchingResult matchResult = EvaluateMotionMatchingDatabase(query);
        CHECK(!matchResult.UsedFallback);
        CHECK(!matchResult.SelectedPoseId.empty());
        CHECK(matchResult.SelectedPoseId == "pose_run_fwd_20");

        ClearMotionMatchingDatabases();
    }

    // 4. Animator Parameter Channels
    {
        Core::ECS::AnimatorComponent animator;
        animator.Parameters["Speed"] = Core::ECS::AnimatorParameterValue::CreateFloat(0.0f);

        AnimatorParameterSetRequest req;
        req.ParameterName = "Speed";
        req.ParameterType = AnimatorParameterChannelType::Float;
        req.EventMode = EventSyncMode::Immediate;
        req.FloatValue = 5.2f;

        auto res = SetAnimatorParameterValue(animator, req);
        CHECK(res.Success);
        CHECK(animator.GetFloat("Speed") == 5.2f);
    }

    return 0;
}
