#include "MCPAllTools.h"
#include "MCPToolFactory.h"

// The tool factory lives in its own translation unit on purpose.
//
// Every family header is header-only and heavily templated (nlohmann::json schema
// literals in particular). Instantiating all of them inside Application.cpp pushed
// that object past the COFF section limit ("error C1128: number of sections
// exceeded object file format limit"). Compiling them once here keeps Application
// small and makes incremental builds of the app cheap.

namespace Core {
namespace MCP {

    std::vector<MCPToolPtr> CreateAllMCPTools() {
        return CreateAllMCPToolsImpl();
    }

} // namespace MCP
} // namespace Core
