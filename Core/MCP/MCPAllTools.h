#pragma once

// MCP All Tools Factory
// Combines all MCP tools (scene tools + auto-level designer) into a single collection
// This file exists to avoid circular dependencies between MCPSceneTools.h and AutoLevelDesigner.h

#include "MCPTool.h"
#include "MCPSceneTools.h"
#include "AutoLevelDesigner.h"

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
    inline std::vector<MCPToolPtr> CreateAllMCPTools() {
        std::vector<MCPToolPtr> tools;

        // Add scene tools (GetSceneContext, SpawnEntity, ModifyComponent, ExecuteScript)
        auto sceneTools = CreateSceneTools();
        tools.insert(tools.end(), sceneTools.begin(), sceneTools.end());

        // Add auto-level designer tools (AutoLevelDesigner, DesignQuery, DesignTemplates)
        auto designerTools = CreateAutoLevelDesignerTools();
        tools.insert(tools.end(), designerTools.begin(), designerTools.end());

        return tools;
    }

} // namespace MCP
} // namespace Core
