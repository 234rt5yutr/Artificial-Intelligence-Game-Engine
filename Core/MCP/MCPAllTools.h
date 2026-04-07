#pragma once

// MCP All Tools Factory
// Combines all MCP tools (scene tools + auto-level designer + audio tools + animation tools + particle tools + world tools + post-process tools + UI tools + navigation tools) into a single collection
// This file exists to avoid circular dependencies between MCPSceneTools.h and AutoLevelDesigner.h

#include "MCPTool.h"
#include "MCPSceneTools.h"
#include "AutoLevelDesigner.h"
#include "MCPAudioTools.h"
#include "MCPAnimationTools.h"
#include "MCPParticleTools.h"
#include "MCPWorldTools.h"
#include "MCPPostProcessTools.h"
#include "MCPUITools.h"
#include "MCPNavigationTools.h"
#include "MCPGameplayTools.h"

namespace Core {
namespace MCP {

    // ============================================================================
    // Combined factory function to create all MCP tools
    // ============================================================================
    
    // Creates all available MCP tools including:
    // - GetSceneContext: Query scene state
    // - SpawnEntity: Create new entities
    // - ModifyComponent: Modify existing entities
    // - ExecuteScript: Run Lua scripts
    // - AutoLevelDesigner: Generate levels from prompts
    // - DesignQuery: Analyze prompts without executing
    // - DesignTemplates: List available templates
    // - PlayAudio: Play sounds and music
    // - StopAudio: Stop playing audio
    // - ModifyAcoustics: Alter reverb and acoustic parameters
    // - GetAudioState: Query audio system state
    // - SetMasterVolume: Control master volume
    // - SetAnimationState: Control animation state machines
    // - SetIKTarget: Set inverse kinematics targets
    // - GetAnimationInfo: Query animation states and bone transforms
    // - SpawnParticleEffect: Spawn particle emitters at locations
    // - ModifyEmitter: Modify existing particle emitter parameters
    // - GetParticleInfo: Query particle emitter state and information
    // - GenerateBiome: Procedurally generate terrain and foliage
    // - SetTimeOfDay: Control day/night cycle and lighting
    // - SetPostProcessProfile: Apply cinematic mood presets
    // - BlendCameraEffects: Smooth transitions for DoF, motion blur, etc.
    // - GetPostProcessInfo: Query current post-process state
    // - DisplayScreenMessage: Show text messages on screen
    // - UpdateHUD: Modify HUD widget values
    // - TriggerSaveState: Save or load game state
    // - ShowLoadingScreen: Display loading screen with progress
    // - RebuildNavMesh: Build navigation mesh from scene
    // - CommandAgentMove: Direct agents to navigate to positions
    // - SetPatrolRoute: Define patrol waypoints for agents
    // - QueryNavMesh: Pathfinding and point queries
    // - AddNavMeshObstacle: Add dynamic obstacles
    // - GetNavigationStats: Query navigation system state
    // - InjectDialogueNode: Inject dynamic dialogue nodes
    // - UpdateQuestObjective: Update quest progress
    // - ModifyInventory: Add/remove inventory items
    // - GetGameplayState: Query gameplay systems state
    // - SetAIState: Modify AI behavior/FSM state
    // - StartDialogue: Start dialogue conversations
    inline std::vector<MCPToolPtr> CreateAllMCPTools() {
        std::vector<MCPToolPtr> tools;

        // Add scene tools (GetSceneContext, SpawnEntity, ModifyComponent, ExecuteScript)
        auto sceneTools = CreateSceneTools();
        tools.insert(tools.end(), sceneTools.begin(), sceneTools.end());

        // Add auto-level designer tools (AutoLevelDesigner, DesignQuery, DesignTemplates)
        auto designerTools = CreateAutoLevelDesignerTools();
        tools.insert(tools.end(), designerTools.begin(), designerTools.end());

        // Add audio tools (PlayAudio, StopAudio, ModifyAcoustics, GetAudioState, SetMasterVolume)
        auto audioTools = CreateAudioTools();
        tools.insert(tools.end(), audioTools.begin(), audioTools.end());

        // Add animation tools (SetAnimationState, SetIKTarget, GetAnimationInfo)
        auto animationTools = CreateAnimationTools();
        tools.insert(tools.end(), animationTools.begin(), animationTools.end());

        // Add particle tools (SpawnParticleEffect, ModifyEmitter, GetParticleInfo)
        auto particleTools = CreateParticleTools();
        tools.insert(tools.end(), particleTools.begin(), particleTools.end());

        // Add world tools (GenerateBiome, SetTimeOfDay)
        auto worldTools = CreateWorldTools();
        tools.insert(tools.end(), worldTools.begin(), worldTools.end());

        // Add post-process tools (SetPostProcessProfile, BlendCameraEffects, GetPostProcessInfo)
        auto postProcessTools = CreatePostProcessTools();
        tools.insert(tools.end(), postProcessTools.begin(), postProcessTools.end());

        // Add UI tools (DisplayScreenMessage, UpdateHUD, TriggerSaveState, ShowLoadingScreen)
        auto uiTools = CreateUITools();
        tools.insert(tools.end(), uiTools.begin(), uiTools.end());

        // Add navigation tools (RebuildNavMesh, CommandAgentMove, SetPatrolRoute, QueryNavMesh, etc.)
        auto navigationTools = CreateNavigationTools();
        tools.insert(tools.end(), navigationTools.begin(), navigationTools.end());

        // Add gameplay tools (InjectDialogueNode, UpdateQuestObjective, ModifyInventory, GetGameplayState, SetAIState, StartDialogue)
        auto gameplayTools = CreateGameplayTools();
        tools.insert(tools.end(), gameplayTools.begin(), gameplayTools.end());

        return tools;
    }

} // namespace MCP
} // namespace Core
