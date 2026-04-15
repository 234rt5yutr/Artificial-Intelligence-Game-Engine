# MCP Server Guide

This guide explains how to start and use the engine's MCP (Model Context Protocol) server.

---

## 1. What the MCP server provides

The MCP server is an HTTP + JSON-RPC endpoint implemented in:

- `Core/MCP/MCPServer.h/.cpp`
- `Core/MCP/HttpServer.h/.cpp`

Default server config:

- Host: `127.0.0.1`
- Port: `3000`
- Main RPC endpoint: `POST /mcp` (also `POST /`)
- Health endpoint: `GET /health`
- Convenience tools endpoint: `GET /tools`

---

## 2. Current integration model

MCP is now bootstrapped by default inside the engine runtime (`Core::Application`):

1. Creates an `MCPServer` with runtime host/port options.
2. Binds the active runtime scene.
3. Starts server lifecycle on app startup and pumps pending requests each frame.
4. Stops MCP on app shutdown.

The runtime bootstrap provides the MCP transport and core JSON-RPC method handlers (`initialize`, `ping`, `tools/list`, `tools/call`, `logging/setLevel`). Custom hosts can register tool families (example below) if you want domain-specific MCP actions active in your build.

Runtime flags:

- `--disable-mcp`
- `--mcp-host=<host>`
- `--mcp-port=<port>`

---

## 3. Manual MCP embedding (custom hosts)

## 3.1 Includes

```cpp
#include "Core/MCP/MCPServer.h"
#include "Core/MCP/MCPAllTools.h"
```

## 3.2 Create, register tools, and start

```cpp
Core::MCP::MCPServerConfig cfg;
cfg.Host = "127.0.0.1";
cfg.Port = 3000;
cfg.ThreadPoolSize = 4;

auto mcpServer = std::make_unique<Core::MCP::MCPServer>(cfg);

// Optional but important if tools need scene access:
// mcpServer->SetActiveScene(scenePtr);

for (auto& tool : Core::MCP::CreateAllMCPTools()) {
    mcpServer->RegisterTool(std::move(tool));
}

if (!mcpServer->Start()) {
    // handle startup failure
}
```

## 3.3 Process pending requests on the main thread

If you use tools that require main-thread execution, call this in your frame loop:

```cpp
mcpServer->ProcessPendingRequests();
```

## 3.4 Stop on shutdown

```cpp
if (mcpServer) {
    mcpServer->Stop();
}
```

---

## 4. Using MCP server from a client

All JSON-RPC requests use `jsonrpc: "2.0"` and include an `id`.

## 4.1 Health check

```powershell
curl http://127.0.0.1:3000/health
```

## 4.2 List tools (convenience endpoint)

```powershell
curl http://127.0.0.1:3000/tools
```

## 4.3 Initialize MCP session

```powershell
curl -X POST http://127.0.0.1:3000/mcp `
  -H "Content-Type: application/json" `
  -d "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"clientInfo\":{\"name\":\"my-client\",\"version\":\"1.0.0\"}}}"
```

## 4.4 JSON-RPC tools/list

```powershell
curl -X POST http://127.0.0.1:3000/mcp `
  -H "Content-Type: application/json" `
  -d "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}"
```

## 4.5 JSON-RPC tools/call

Example tool call payload:

```powershell
curl -X POST http://127.0.0.1:3000/mcp `
  -H "Content-Type: application/json" `
  -d "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"GetSceneContext\",\"arguments\":{}}}"
```

## 4.6 Set log level

```powershell
curl -X POST http://127.0.0.1:3000/mcp `
  -H "Content-Type: application/json" `
  -d "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"logging/setLevel\",\"params\":{\"level\":\"debug\"}}"
```

---

## 5. Supported core MCP methods

Implemented by `MCPServer`:

1. `initialize`
2. `ping`
3. `tools/list`
4. `tools/call`
5. `logging/setLevel`

---

## 6. Security and operational notes

1. Keep MCP bound to loopback (`127.0.0.1`) unless you explicitly need remote access.
2. Validate and gate tool exposure in production-like environments.
3. If tools require scene mutation, ensure `SetActiveScene(...)` is configured.
4. Prefer explicit allowlists for external clients and requests.

<!-- release-doc-sync:2026-04-15 -->

## Release Sync (2026-04-15)

- Verified clean Release rebuild: `cmake --build build --config Release --target ALL_BUILD --clean-first -- /m /nologo /verbosity:minimal`.
- Verified Release test sweep: `ctest --test-dir build -C Release` (**18/18 passed**).
- Confirmed executable composition: `AIGameEngine` links `EngineCore`, and `EngineCore` includes `Core/MCP/HttpServer.cpp` + `Core/MCP/MCPServer.cpp`.
- Runtime MCP integration is now enabled in `Core::Application` by default; runtime flags: `--disable-mcp`, `--mcp-host=<host>`, `--mcp-port=<port>`.
