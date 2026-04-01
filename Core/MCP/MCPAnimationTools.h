#pragma once

// ============================================================================
// MCPAnimationTools.h
// MCP tools for AI agents to control skeletal animations and inverse kinematics
// 
// Tools provided:
// - SetAnimationState: Control animation state machine
// - SetIKTarget: Set IK targets for procedural animation
// - GetAnimationInfo: Query animation states and bone transforms
// ============================================================================

#include "MCPTool.h"
#include "MCPTypes.h"
#include "Core/ECS/Scene.h"
#include "Core/ECS/Entity.h"
#include "Core/ECS/Components/Components.h"
#include "Core/ECS/Components/AnimatorComponent.h"
#include "Core/ECS/Components/SkeletalMeshComponent.h"
#include "Core/ECS/Components/IKComponent.h"
#include "Core/Log.h"

#include <sstream>
#include <algorithm>

namespace Core {
namespace MCP {

    // ========================================================================
    // SetAnimationState Tool
    // ========================================================================
    // Allows AI agents to control animation state machines

    class SetAnimationStateTool : public MCPTool {
    public:
        SetAnimationStateTool()
            : MCPTool("SetAnimationState",
                      "Control the animation state machine of a skeletal mesh entity. "
                      "Can force state transitions, set animation parameters (float, bool, trigger), "
                      "adjust playback speed, and control animation layers. Use to drive character "
                      "animations, script cutscenes, and coordinate NPC behaviors.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            schema.Properties = {
                {"entity", {
                    {"type", "string"},
                    {"description", "Name of the entity with AnimatorComponent"}
                }},
                {"action", {
                    {"type", "string"},
                    {"enum", Json::array({"setParameter", "forceState", "setLayerWeight", 
                                          "play", "pause", "resume", "reset"})},
                    {"description", "Action to perform: "
                                    "'setParameter' - set float/bool/trigger parameter, "
                                    "'forceState' - force transition to a state, "
                                    "'setLayerWeight' - adjust animation layer blend, "
                                    "'play/pause/resume' - control playback, "
                                    "'reset' - reset to default state"}
                }},
                {"state", {
                    {"type", "string"},
                    {"description", "Target state name (for 'forceState' action)"}
                }},
                {"transitionDuration", {
                    {"type", "number"},
                    {"minimum", 0.0},
                    {"description", "Blend duration in seconds (for 'forceState')"},
                    {"default", 0.25}
                }},
                {"parameterName", {
                    {"type", "string"},
                    {"description", "Name of parameter to set (for 'setParameter')"}
                }},
                {"parameterType", {
                    {"type", "string"},
                    {"enum", Json::array({"float", "bool", "trigger"})},
                    {"description", "Type of parameter"},
                    {"default", "float"}
                }},
                {"floatValue", {
                    {"type", "number"},
                    {"description", "Float value to set (for float parameters)"}
                }},
                {"boolValue", {
                    {"type", "boolean"},
                    {"description", "Bool value to set (for bool parameters)"}
                }},
                {"layerName", {
                    {"type", "string"},
                    {"description", "Name of animation layer (for 'setLayerWeight')"}
                }},
                {"layerWeight", {
                    {"type", "number"},
                    {"minimum", 0.0},
                    {"maximum", 1.0},
                    {"description", "Layer blend weight 0-1 (for 'setLayerWeight')"}
                }},
                {"playbackSpeed", {
                    {"type", "number"},
                    {"minimum", 0.0},
                    {"maximum", 5.0},
                    {"description", "Optional playback speed multiplier"}
                }}
            };
            schema.Required = {"entity", "action"};
            return schema;
        }

        ToolResult Execute(const Json& arguments, ECS::Scene* scene) override {
            if (!scene) {
                return ToolResult::Error("Scene is not available");
            }

            // Get entity
            std::string entityName = arguments.value("entity", "");
            if (entityName.empty()) {
                return ToolResult::Error("Entity name is required");
            }

            auto entity = scene->FindEntityByName(entityName);
            if (!entity.IsValid()) {
                return ToolResult::Error("Entity '" + entityName + "' not found");
            }

            if (!entity.HasComponent<ECS::AnimatorComponent>()) {
                return ToolResult::Error("Entity '" + entityName + "' does not have AnimatorComponent");
            }

            auto& animator = entity.GetComponent<ECS::AnimatorComponent>();

            // Get action
            std::string action = arguments.value("action", "");

            std::ostringstream result;

            if (action == "setParameter") {
                std::string paramName = arguments.value("parameterName", "");
                std::string paramType = arguments.value("parameterType", "float");

                if (paramName.empty()) {
                    return ToolResult::Error("Parameter name is required");
                }

                if (paramType == "float") {
                    float value = arguments.value("floatValue", 0.0f);
                    if (animator.SetFloat(paramName, value)) {
                        result << "Set float parameter '" << paramName << "' to " << value;
                    } else {
                        return ToolResult::Error("Float parameter '" + paramName + "' not found");
                    }
                } else if (paramType == "bool") {
                    bool value = arguments.value("boolValue", false);
                    if (animator.SetBool(paramName, value)) {
                        result << "Set bool parameter '" << paramName << "' to " 
                               << (value ? "true" : "false");
                    } else {
                        return ToolResult::Error("Bool parameter '" + paramName + "' not found");
                    }
                } else if (paramType == "trigger") {
                    if (animator.SetTrigger(paramName)) {
                        result << "Activated trigger '" << paramName << "'";
                    } else {
                        return ToolResult::Error("Trigger parameter '" + paramName + "' not found");
                    }
                }
            }
            else if (action == "forceState") {
                std::string stateName = arguments.value("state", "");
                if (stateName.empty()) {
                    return ToolResult::Error("State name is required for forceState");
                }

                float duration = arguments.value("transitionDuration", 0.25f);
                
                if (animator.ForceState(stateName, duration)) {
                    result << "Forced transition to state '" << stateName << "'";
                    if (duration > 0) {
                        result << " with " << duration << "s blend";
                    }
                } else {
                    return ToolResult::Error("State '" + stateName + "' not found in state machine");
                }
            }
            else if (action == "setLayerWeight") {
                std::string layerName = arguments.value("layerName", "");
                if (layerName.empty()) {
                    return ToolResult::Error("Layer name is required for setLayerWeight");
                }

                float weight = arguments.value("layerWeight", 1.0f);
                animator.SetLayerWeight(layerName, weight);
                result << "Set layer '" << layerName << "' weight to " << weight;
            }
            else if (action == "play") {
                animator.Resume();
                result << "Animation playback started";
            }
            else if (action == "pause") {
                animator.Pause();
                result << "Animation playback paused";
            }
            else if (action == "resume") {
                animator.Resume();
                result << "Animation playback resumed";
            }
            else if (action == "reset") {
                animator.Reset();
                result << "Animation reset to default state '" 
                       << animator.StateMachine.DefaultStateName << "'";
            }
            else {
                return ToolResult::Error("Unknown action: " + action);
            }

            // Log the action
            ENGINE_CORE_INFO("MCP SetAnimationState: {} on '{}'", result.str(), entityName);

            return ToolResult::Success(result.str());
        }
    };

    // ========================================================================
    // SetIKTarget Tool
    // ========================================================================
    // Allows AI agents to control inverse kinematics

    class SetIKTargetTool : public MCPTool {
    public:
        SetIKTargetTool()
            : MCPTool("SetIKTarget",
                      "Control inverse kinematics targets on a skeletal mesh entity. "
                      "Can set hand positions for grabbing/pointing, control look-at targets "
                      "for gaze direction, adjust foot placement, and enable/disable IK chains. "
                      "Use to make NPCs look at points of interest, reach for objects, or "
                      "procedurally adjust limb positions.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            schema.Properties = {
                {"entity", {
                    {"type", "string"},
                    {"description", "Name of the entity with IKComponent"}
                }},
                {"chain", {
                    {"type", "string"},
                    {"description", "Name of IK chain (e.g., 'LeftHand', 'RightFoot', 'Head')"}
                }},
                {"action", {
                    {"type", "string"},
                    {"enum", Json::array({"setTarget", "clearTarget", "setWeight", 
                                          "enableFootIK", "disableFootIK"})},
                    {"description", "Action to perform: "
                                    "'setTarget' - set position/rotation target, "
                                    "'clearTarget' - disable IK target (return to animation), "
                                    "'setWeight' - adjust IK blend weight, "
                                    "'enableFootIK/disableFootIK' - control procedural foot placement"}
                }},
                {"position", {
                    {"type", "object"},
                    {"description", "Target position in world space"},
                    {"properties", {
                        {"x", {{"type", "number"}}},
                        {"y", {{"type", "number"}}},
                        {"z", {{"type", "number"}}}
                    }},
                    {"required", Json::array({"x", "y", "z"})}
                }},
                {"rotation", {
                    {"type", "object"},
                    {"description", "Target rotation as euler angles (degrees)"},
                    {"properties", {
                        {"pitch", {{"type", "number"}}},
                        {"yaw", {{"type", "number"}}},
                        {"roll", {{"type", "number"}}}
                    }}
                }},
                {"lookAtEntity", {
                    {"type", "string"},
                    {"description", "Entity name to look at (alternative to position for look-at IK)"}
                }},
                {"weight", {
                    {"type", "number"},
                    {"minimum", 0.0},
                    {"maximum", 1.0},
                    {"description", "IK blend weight (0 = animation, 1 = IK)"},
                    {"default", 1.0}
                }},
                {"poleVector", {
                    {"type", "object"},
                    {"description", "Hint direction for IK bend (e.g., knee/elbow direction)"},
                    {"properties", {
                        {"x", {{"type", "number"}}},
                        {"y", {{"type", "number"}}},
                        {"z", {{"type", "number"}}}
                    }}
                }},
                {"globalWeight", {
                    {"type", "number"},
                    {"minimum", 0.0},
                    {"maximum", 1.0},
                    {"description", "Global IK weight for all chains"}
                }}
            };
            schema.Required = {"entity", "action"};
            return schema;
        }

        ToolResult Execute(const Json& arguments, ECS::Scene* scene) override {
            if (!scene) {
                return ToolResult::Error("Scene is not available");
            }

            // Get entity
            std::string entityName = arguments.value("entity", "");
            if (entityName.empty()) {
                return ToolResult::Error("Entity name is required");
            }

            auto entity = scene->FindEntityByName(entityName);
            if (!entity.IsValid()) {
                return ToolResult::Error("Entity '" + entityName + "' not found");
            }

            if (!entity.HasComponent<ECS::IKComponent>()) {
                return ToolResult::Error("Entity '" + entityName + "' does not have IKComponent");
            }

            auto& ik = entity.GetComponent<ECS::IKComponent>();

            std::string action = arguments.value("action", "");
            std::ostringstream result;

            if (action == "setTarget") {
                std::string chainName = arguments.value("chain", "");
                if (chainName.empty()) {
                    return ToolResult::Error("Chain name is required for setTarget");
                }

                ECS::IKTarget target;
                float weight = arguments.value("weight", 1.0f);
                target.Weight = weight;

                // Position target
                if (arguments.contains("position") && arguments["position"].is_object()) {
                    const auto& pos = arguments["position"];
                    target.Position.x = pos.value("x", 0.0f);
                    target.Position.y = pos.value("y", 0.0f);
                    target.Position.z = pos.value("z", 0.0f);
                    target.UsePosition = true;
                }

                // Look at entity (alternative)
                if (arguments.contains("lookAtEntity") && arguments["lookAtEntity"].is_string()) {
                    std::string targetEntityName = arguments["lookAtEntity"].get<std::string>();
                    auto targetEntity = scene->FindEntityByName(targetEntityName);
                    if (targetEntity.IsValid() && targetEntity.HasComponent<ECS::TransformComponent>()) {
                        const auto& transform = targetEntity.GetComponent<ECS::TransformComponent>();
                        target.Position = transform.Position;
                        target.UsePosition = true;
                    } else {
                        return ToolResult::Error("Look-at target entity '" + targetEntityName + "' not found");
                    }
                }

                // Rotation target
                if (arguments.contains("rotation") && arguments["rotation"].is_object()) {
                    const auto& rot = arguments["rotation"];
                    float pitch = glm::radians(rot.value("pitch", 0.0f));
                    float yaw = glm::radians(rot.value("yaw", 0.0f));
                    float roll = glm::radians(rot.value("roll", 0.0f));
                    target.Rotation = glm::quat(glm::vec3(pitch, yaw, roll));
                    target.UseRotation = true;
                }

                // Pole vector
                if (arguments.contains("poleVector") && arguments["poleVector"].is_object()) {
                    const auto& pole = arguments["poleVector"];
                    target.HintVector.x = pole.value("x", 0.0f);
                    target.HintVector.y = pole.value("y", 0.0f);
                    target.HintVector.z = pole.value("z", 1.0f);
                    target.HintType = ECS::IKHintType::World;
                }

                if (ik.SetTarget(chainName, target)) {
                    result << "Set IK target for chain '" << chainName << "'";
                    if (target.UsePosition) {
                        result << " at (" << target.Position.x << ", " 
                               << target.Position.y << ", " << target.Position.z << ")";
                    }
                    result << " with weight " << weight;
                } else {
                    return ToolResult::Error("IK chain '" + chainName + "' not found");
                }
            }
            else if (action == "clearTarget") {
                std::string chainName = arguments.value("chain", "");
                if (chainName.empty()) {
                    return ToolResult::Error("Chain name is required for clearTarget");
                }

                if (ik.ClearTarget(chainName)) {
                    result << "Cleared IK target for chain '" << chainName << "'";
                } else {
                    return ToolResult::Error("IK chain '" + chainName + "' not found");
                }
            }
            else if (action == "setWeight") {
                std::string chainName = arguments.value("chain", "");
                float weight = arguments.value("weight", 1.0f);

                if (chainName.empty()) {
                    // Set global weight
                    if (arguments.contains("globalWeight")) {
                        ik.GlobalWeight = arguments.value("globalWeight", 1.0f);
                        result << "Set global IK weight to " << ik.GlobalWeight;
                    } else {
                        return ToolResult::Error("Chain name or globalWeight is required");
                    }
                } else {
                    if (ik.SetChainWeight(chainName, weight)) {
                        result << "Set IK chain '" << chainName << "' weight to " << weight;
                    } else {
                        return ToolResult::Error("IK chain '" + chainName + "' not found");
                    }
                }
            }
            else if (action == "enableFootIK") {
                ik.FootSettings.Enabled = true;
                result << "Enabled foot IK";
            }
            else if (action == "disableFootIK") {
                ik.FootSettings.Enabled = false;
                result << "Disabled foot IK";
            }
            else {
                return ToolResult::Error("Unknown action: " + action);
            }

            ENGINE_CORE_INFO("MCP SetIKTarget: {} on '{}'", result.str(), entityName);

            return ToolResult::Success(result.str());
        }
    };

    // ========================================================================
    // GetAnimationInfo Tool
    // ========================================================================
    // Query animation states and bone transforms

    class GetAnimationInfoTool : public MCPTool {
    public:
        GetAnimationInfoTool()
            : MCPTool("GetAnimationInfo",
                      "Query animation information from a skeletal mesh entity. "
                      "Can retrieve current animation state, available states, "
                      "parameter values, bone transforms, and IK state. Use to "
                      "understand character state before making animation changes.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            schema.Properties = {
                {"entity", {
                    {"type", "string"},
                    {"description", "Name of the entity to query"}
                }},
                {"query", {
                    {"type", "string"},
                    {"enum", Json::array({"state", "parameters", "layers", "bones", 
                                          "availableStates", "ikChains"})},
                    {"description", "What information to retrieve: "
                                    "'state' - current animation state, "
                                    "'parameters' - all parameter values, "
                                    "'layers' - animation layer info, "
                                    "'bones' - bone names and transforms, "
                                    "'availableStates' - list of states, "
                                    "'ikChains' - IK chain information"},
                    {"default", "state"}
                }},
                {"boneName", {
                    {"type", "string"},
                    {"description", "Specific bone to query (for 'bones' query)"}
                }}
            };
            schema.Required = {"entity"};
            return schema;
        }

        ToolResult Execute(const Json& arguments, ECS::Scene* scene) override {
            if (!scene) {
                return ToolResult::Error("Scene is not available");
            }

            std::string entityName = arguments.value("entity", "");
            if (entityName.empty()) {
                return ToolResult::Error("Entity name is required");
            }

            auto entity = scene->FindEntityByName(entityName);
            if (!entity.IsValid()) {
                return ToolResult::Error("Entity '" + entityName + "' not found");
            }

            std::string query = arguments.value("query", "state");
            Json result;

            // Query animator info
            if (entity.HasComponent<ECS::AnimatorComponent>()) {
                const auto& animator = entity.GetComponent<ECS::AnimatorComponent>();

                if (query == "state") {
                    result["currentState"] = animator.GetCurrentStateName();
                    result["isPlaying"] = animator.IsPlaying();
                    result["isTransitioning"] = animator.IsTransitioning();
                    if (animator.IsTransitioning()) {
                        result["transitionProgress"] = animator.GetTransitionProgress();
                    }
                    result["normalizedTime"] = animator.RuntimeState.NormalizedTime;
                }
                else if (query == "parameters") {
                    Json params = Json::object();
                    for (const auto& [name, value] : animator.Parameters) {
                        switch (value.Type) {
                            case ECS::AnimatorParameterType::Float:
                                params[name] = value.GetFloat();
                                break;
                            case ECS::AnimatorParameterType::Bool:
                                params[name] = value.GetBool();
                                break;
                            case ECS::AnimatorParameterType::Trigger:
                                params[name] = value.IsTriggerSet() ? "triggered" : "idle";
                                break;
                        }
                    }
                    result["parameters"] = params;
                }
                else if (query == "layers") {
                    Json layers = Json::array();
                    for (const auto& layer : animator.Layers) {
                        Json layerInfo;
                        layerInfo["name"] = layer.Name;
                        layerInfo["index"] = layer.LayerIndex;
                        layerInfo["weight"] = layer.Weight;
                        layerInfo["isAdditive"] = layer.IsAdditive;
                        layerInfo["currentState"] = layer.CurrentStateName;
                        layers.push_back(layerInfo);
                    }
                    result["layers"] = layers;
                }
                else if (query == "availableStates") {
                    Json states = Json::array();
                    for (const auto& state : animator.StateMachine.States) {
                        Json stateInfo;
                        stateInfo["name"] = state.Name;
                        stateInfo["clip"] = state.AnimationClipName;
                        stateInfo["loop"] = state.Loop;
                        stateInfo["speed"] = state.SpeedMultiplier;
                        stateInfo["isBlendTree"] = state.IsBlendTree();
                        states.push_back(stateInfo);
                    }
                    result["states"] = states;
                    result["defaultState"] = animator.StateMachine.DefaultStateName;
                }
            }

            // Query skeletal mesh info
            if (query == "bones" && entity.HasComponent<ECS::SkeletalMeshComponent>()) {
                const auto& skeletal = entity.GetComponent<ECS::SkeletalMeshComponent>();
                
                std::string specificBone = arguments.value("boneName", "");
                
                if (!specificBone.empty()) {
                    // Query specific bone
                    int32_t boneIdx = skeletal.GetBoneIndex(specificBone);
                    if (boneIdx >= 0) {
                        Math::Mat4 transform = skeletal.GetBoneTransform(boneIdx);
                        Math::Vec3 pos = Math::Vec3(transform[3]);
                        
                        result["boneName"] = specificBone;
                        result["boneIndex"] = boneIdx;
                        result["position"] = {
                            {"x", pos.x}, {"y", pos.y}, {"z", pos.z}
                        };
                    } else {
                        return ToolResult::Error("Bone '" + specificBone + "' not found");
                    }
                } else {
                    // List all bones
                    Json bones = Json::array();
                    if (skeletal.HasSkeleton()) {
                        const auto& skeleton = skeletal.MeshData->GetSkeleton();
                        for (size_t i = 0; i < skeleton.Bones.size(); ++i) {
                            Json boneInfo;
                            boneInfo["name"] = skeleton.Bones[i].Name;
                            boneInfo["index"] = static_cast<int32_t>(i);
                            boneInfo["parent"] = skeleton.Bones[i].ParentIndex;
                            bones.push_back(boneInfo);
                        }
                    }
                    result["bones"] = bones;
                    result["boneCount"] = skeletal.GetBoneCount();
                }
            }

            // Query IK info
            if (query == "ikChains" && entity.HasComponent<ECS::IKComponent>()) {
                const auto& ik = entity.GetComponent<ECS::IKComponent>();
                
                Json chains = Json::array();
                for (size_t i = 0; i < ik.Chains.size(); ++i) {
                    const auto& chain = ik.Chains[i];
                    const auto& state = ik.ChainStates[i];
                    
                    Json chainInfo;
                    chainInfo["name"] = chain.Name;
                    chainInfo["type"] = static_cast<int>(chain.Type);
                    chainInfo["bones"] = chain.BoneNames;
                    chainInfo["currentWeight"] = state.CurrentWeight;
                    chainInfo["targetWeight"] = state.TargetWeight;
                    chainInfo["isGrounded"] = state.IsGrounded;
                    
                    if (state.CurrentWeight > 0.001f) {
                        chainInfo["targetPosition"] = {
                            {"x", state.CurrentTarget.Position.x},
                            {"y", state.CurrentTarget.Position.y},
                            {"z", state.CurrentTarget.Position.z}
                        };
                    }
                    
                    chains.push_back(chainInfo);
                }
                result["ikChains"] = chains;
                result["globalIKWeight"] = ik.GlobalWeight;
                result["footIKEnabled"] = ik.FootSettings.Enabled;
            }

            return ToolResult::Success(result.dump(2));
        }
    };

    // ========================================================================
    // Tool Registration Helper
    // ========================================================================

    /**
     * @brief Register all animation MCP tools with the server
     * @param server The MCP server to register tools with
     */
    inline void RegisterAnimationTools(MCPServer& server) {
        server.RegisterTool(std::make_shared<SetAnimationStateTool>());
        server.RegisterTool(std::make_shared<SetIKTargetTool>());
        server.RegisterTool(std::make_shared<GetAnimationInfoTool>());
        
        ENGINE_CORE_INFO("MCP: Registered animation tools (SetAnimationState, SetIKTarget, GetAnimationInfo)");
    }

    /**
     * @brief Create all animation MCP tools as a vector
     * @return Vector of animation tool pointers
     */
    inline std::vector<MCPToolPtr> CreateAnimationTools() {
        return {
            std::make_shared<SetAnimationStateTool>(),
            std::make_shared<SetIKTargetTool>(),
            std::make_shared<GetAnimationInfoTool>()
        };
    }

} // namespace MCP
} // namespace Core
