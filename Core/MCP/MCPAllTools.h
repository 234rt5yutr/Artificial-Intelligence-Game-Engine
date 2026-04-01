#pragma once

// MCP All Tools Factory
// Combines all MCP tools (scene tools + auto-level designer + audio tools) into a single collection
// This file exists to avoid circular dependencies between MCPSceneTools.h and AutoLevelDesigner.h

#include "MCPTool.h"
#include "MCPSceneTools.h"
#include "AutoLevelDesigner.h"
#include "MCPAudioTools.h"

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

        return tools;
    }

} // namespace MCP
} // namespace Core
