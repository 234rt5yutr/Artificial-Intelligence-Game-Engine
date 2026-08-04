#pragma once

// Declaration-only entry point for the MCP tool set.
//
// Include this (not MCPAllTools.h) when you just want to register the tools.
// MCPAllTools.h pulls in every family header, and instantiating all of their
// json-schema templates in one object file exceeds the COFF section limit.
// The definition lives in MCPAllTools.cpp.

#include "MCPTool.h"
#include <vector>

namespace Core {
namespace MCP {

    std::vector<MCPToolPtr> CreateAllMCPTools();

} // namespace MCP
} // namespace Core
