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
#include "MCPGameplayTools.h"
#include "MCPPhysicsTools.h"
#include "MCPNetworkTools.h"
#include "MCPDevTools.h"
#include "MCPProjectTools.h"

// NOT INCLUDED - these four families do not compile against the current MCPTool
// base class and are excluded from the build until they are ported:
//
//   MCPPostProcessTools.h  - uses ToolInputSchema::SchemaProperty, which does not exist
//   MCPUITools.h           - uses lowercase ToolInputSchema::{type,properties,required}
//                            and brace-initialised ToolResult{bool, string}
//   MCPNavigationTools.h   - derives from an older MCPTool with virtual GetName/
//                            GetDescription and a different Execute signature
//   MCPRayTracingTools.h   - written against an AIEngine::Rendering namespace that
//                            is not present in this codebase
//
// Each needs a port to the MCPTool interface in MCPTool.h, not a small patch.

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
    // - ToggleRayTracingFeatures: Enable/disable RT features with quality control
    // - BakeGlobalIllumination: Bake GI probes for indirect lighting
    // - SetReflectionQuality: Control reflection quality per material/scene
    // - QueryRTCapabilities: Query RT hardware capabilities and settings
    // - TriggerDestruction: Trigger physics-based destruction
    // - SpawnRagdoll: Convert skeletal mesh to ragdoll
    // - ModifyConstraint: Modify constraint properties at runtime
    // - QueryPhysicsState: Query physics body state
    // - ApplyForce: Apply force/impulse/torque to physics body
    // Declared here, defined in MCPAllTools.cpp. Callers that only need to
    // register the tools should include this header and link; they do not pay to
    // instantiate every family's schema templates in their own object file.
    std::vector<MCPToolPtr> CreateAllMCPTools();

    // Implementation detail: instantiates every family. Only MCPAllTools.cpp
    // calls this.
    inline std::vector<MCPToolPtr> CreateAllMCPToolsImpl() {
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

        // Add gameplay tools (InjectDialogueNode, UpdateQuestObjective, ModifyInventory, GetGameplayState, SetAIState, StartDialogue)
        auto gameplayTools = CreateGameplayTools();
        tools.insert(tools.end(), gameplayTools.begin(), gameplayTools.end());

        // Add physics tools (TriggerDestruction, SpawnRagdoll, ModifyConstraint, QueryPhysicsState, ApplyForce)
        auto physicsTools = CreatePhysicsTools();
        tools.insert(tools.end(), physicsTools.begin(), physicsTools.end());

        // Add network product-layer tools (session, discovery, diagnostics)
        auto networkTools = CreateNetworkTools();
        tools.insert(tools.end(), networkTools.begin(), networkTools.end());

        // Add development-control tools (engine status, simulation pause/step,
        // engine log, play-mode + performance suites, profiler capture)
        auto devTools = CreateDevTools();
        tools.insert(tools.end(), devTools.begin(), devTools.end());

        // Add project-control tools (platform builds, scene save/load, project
        // file listing) so an agent can drive the project loop, not just runtime
        auto projectTools = CreateProjectTools();
        tools.insert(tools.end(), projectTools.begin(), projectTools.end());

        return tools;
    }

} // namespace MCP
} // namespace Core
