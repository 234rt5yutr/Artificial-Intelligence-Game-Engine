# MCP Server Guide

This guide explains how to start and use the engine's MCP (Model Context Protocol) server,
and how to connect an agent such as Claude Code to it.

---

## 1. What the MCP server provides

The MCP server is an HTTP + JSON-RPC 2.0 endpoint implemented in:

- `Core/MCP/MCPServer.h/.cpp`
- `Core/MCP/HttpServer.h/.cpp`

Default server config:

- Host: `127.0.0.1`
- Port: `3000`
- Main RPC endpoint: `POST /mcp` (also `POST /`)
- Health endpoint: `GET /health`
- Convenience tools endpoint: `GET /tools`

Protocol revisions supported: `2025-06-18` (default), `2025-03-26`, `2024-11-05`.
The version is negotiated during `initialize`; the server echoes back the client's
requested revision when it recognises it.

---

## 2. Runtime integration

MCP is bootstrapped by default inside the engine runtime (`Core::Application`):

1. Creates an `MCPServer` with runtime host/port/auth options.
2. Binds the active runtime scene.
3. **Registers every available tool family** via `MCP::CreateAllMCPTools()`.
4. Starts the server and pumps queued requests each frame.
5. Unbinds the scene and stops MCP on shutdown.

### Threading model

`tools/call` never runs on the HTTP worker thread. Tool execution mutates the ECS
registry, so the server marshals the call onto the engine main thread through
`ProcessPendingRequests()` and blocks the HTTP worker until the frame runs it.
If the game loop stalls, the call fails with a timeout
(`MCPServerConfig::MainThreadDispatchTimeoutMs`, default 10s) rather than hanging
the client. Hosts that never pump fall back to executing inline.

Since the engine gained a render thread, `ProcessPendingRequests()` is pumped
*inside the frame's idle window* — after the render thread has been drained and
before the next frame is handed to it. That is what lets the rendering tools
below mutate pipeline state, rebuild render targets, and recompile material
pipelines directly, without racing a frame that is mid-submission.

Runtime flags:

| Flag | Meaning |
| --- | --- |
| `--disable-mcp` | Do not start the MCP server |
| `--mcp-host=<host>` | Bind address (default `127.0.0.1`) |
| `--mcp-port=<port>` | Bind port (default `3000`) |
| `--mcp-token=<secret>` | Require `Authorization: Bearer <secret>` on every request |
| `--mcp-allow-origin=<origin>` | Allow a browser `Origin` (repeatable) |

---

## 3. Connecting Claude Code

The engine speaks JSON-RPC over HTTP POST. A dependency-free stdio bridge is included
so stdio-based MCP clients can talk to it:

```
tools/mcp/engine-mcp-stdio.mjs
```

`.mcp.json` in the repo root already registers it:

```json
{
  "mcpServers": {
    "aigameengine": {
      "command": "node",
      "args": ["tools/mcp/engine-mcp-stdio.mjs"],
      "env": { "AIGE_MCP_URL": "http://127.0.0.1:3000/mcp" }
    }
  }
}
```

With a token, add `"AIGE_MCP_TOKEN": "your-secret"` to that `env` block.

Start the engine first (`.\build\Release\AIGameEngine.exe`), then the agent session.
The bridge requires Node 18+ and has no npm dependencies.

---

## 4. Tool families

`CreateAllMCPTools()` registers these families:

| Family | Header | Examples |
| --- | --- | --- |
| Scene | `MCPSceneTools.h` | `GetSceneContext`, `SpawnEntity`, `ModifyComponent`, `ExecuteScript` |
| Level design | `AutoLevelDesigner.h` | `AutoLevelDesigner`, `DesignQuery`, `DesignTemplates` |
| Audio | `MCPAudioTools.h` | `PlayAudio`, `ModifyAcoustics`, `SetMasterVolume` |
| Animation | `MCPAnimationTools.h` | `SetAnimationState`, `SetIKTarget`, `GetAnimationInfo` |
| Particles | `MCPParticleTools.h` | `SpawnParticleEffect`, `ModifyEmitter`, `GetParticleInfo` |
| World | `MCPWorldTools.h` | `GenerateBiome`, `SetTimeOfDay` |
| Gameplay | `MCPGameplayTools.h` | `UpdateQuestObjective`, `ModifyInventory`, `SetAIState` |
| Physics | `MCPPhysicsTools.h` | `TriggerDestruction`, `SpawnRagdoll`, `ApplyForce` |
| Network | `MCPNetworkTools.h` | session, discovery, diagnostics |
| **Development** | **`MCPDevTools.h`** | see below |
| **Project** | **`MCPProjectTools.h`** | `BuildForPlatform`, `SaveScene`, `LoadScene`, `ListProjectFiles` |
| **Rendering** | **`MCPRenderTools.h`** | see below |
| **Assets** | **`MCPAssetTools.h`** | `LoadTexture`, `ListTextures`, `LoadMesh` |

### Not currently registered

Four families in `Core/MCP/` are excluded from the build because they were written
against an older tool API and do not compile. `MCPAllTools.h` documents each one:

| Header | Why it is excluded |
| --- | --- |
| `MCPPostProcessTools.h` | Uses `ToolInputSchema::SchemaProperty`, which does not exist |
| `MCPUITools.h` | Uses lowercase `ToolInputSchema::{type,properties,required}` and brace-initialised `ToolResult{bool, string}` |
| `MCPNavigationTools.h` | Derives from an older `MCPTool` with virtual `GetName`/`GetDescription` and a different `Execute` signature |
| `MCPRayTracingTools.h` | Written against an `AIEngine::Rendering` namespace that is not present in this codebase |

Each needs a port to the `MCPTool` interface in `MCPTool.h`, not a small patch.

### Development-control tools

These let an agent drive the development loop, not just author content:

| Tool | Purpose |
| --- | --- |
| `GetEngineStatus` | Live runtime state: active systems, frame count, last frame cost, pause/time-scale, entity count |
| `ControlSimulation` | `pause` / `resume` / `step N frames` / `timeScale` / `fixedFrameDelta`. Rendering keeps running while paused so the frozen frame stays inspectable |
| `GetEngineLog` | Tail the in-memory log ring buffer (2048 lines) with an optional substring filter |
| `RunPlayModeTests` | Run the deterministic play-mode suite; returns per-case and per-assertion results |
| `RunPerformanceTests` | Run the performance suite against frame/CPU/GPU/memory budgets |
| `CaptureProfilerTrace` | Capture a CPU/GPU profiler session and export it (`json` or `chrome` format) |

### Project-control tools

| Tool | Purpose |
| --- | --- |
| `BuildForPlatform` | Run the platform build pipeline; returns per-stage status, build id, toolchain signature |
| `SaveScene` | Serialize the active scene to a project-relative asset |
| `LoadScene` | Replace the active scene from an asset on disk |
| `ListProjectFiles` | List project files by directory/extension, with an explicit `truncated` flag |

Every path argument is resolved against the project root and rejected if it escapes
(`"path escapes the project root"`). There is deliberately **no** arbitrary command
execution tool: that would be a remote code execution primitive on the developer's
machine.

A failing test suite comes back with `isError: true`, so a passing tool call is never
mistaken for a passing suite.

### Rendering, material, and editor tools

The other families let an agent author content and drive the development loop.
None of them could see or touch the renderer, which made the GPU-driven / GI /
upscaling stack invisible to tooling. `MCPRenderTools.h` closes that.

| Tool | Purpose |
| --- | --- |
| `GetRenderStats` | Read-only. Render vs display resolution, GPU cluster-culling counters (frustum / backface cone / HZB occlusion, early and late phase), GPU scene residency, GI state, FSR mode, material pipeline count, render-thread timings |
| `SetGPUCulling` | Toggle the indirect path, HZB occlusion, cone culling, and the two-phase re-test independently — isolates where an artefact or a perf change comes from |
| `SetGlobalIllumination` | Rays per pixel, steps per ray, trace distance, indirect intensity, temporal blend rate, probe spacing, probe cache on/off, sky fallback colour |
| `SetUpscaler` | FSR quality preset, sharpening, jitter. Changing quality moves the render resolution and rebuilds every offscreen target |
| `ListMaterials` | Read-only. Every material with its index, node/link counts, compile status, and texture slots |
| `GetMaterialGraph` | Read-only. A material's graph as JSON, optionally with the GLSL it compiles to |
| `SetMaterialGraph` | Create or replace a material's node graph; compiles it and rebuilds its pipeline next frame |
| `SetShadows` | Cascade count and resolution, shadow distance, split distribution, depth/slope/normal bias, PCF radius, spot atlas size |
| `SetEditorViewport` | Editor fly camera on/off, gizmo mode, frame the selection, pause/resume/single-step |
| `LoadTexture` | Import an image (PNG/JPEG/TGA/BMP/PSD/GIF/HDR) with mips under a library name |
| `ListTextures` | Read-only. Every imported texture with dimensions, mips, colour space, and GPU footprint |
| `LoadMesh` | Import a glTF/GLB mesh and spawn it as an entity |

`SpawnEntity` also gained a `primitive` option on its mesh component
(`box`, `sphere`, `plane`, `cylinder`, with `subdivisions`), because until now
nothing in the engine could produce renderable geometry without a glTF file on
disk — an agent could spawn a mesh entity, but it drew nothing.

`GetRenderStats` counters come from the frame that has finished on the GPU, so
they lag the caller by up to one frame. They are read after the in-flight fence,
never mid-write.

Material graph documents look like this:

```json
{
  "name": "FresnelGlow",
  "nodes": [
    {"id": 1, "type": "ConstantColor",  "value": [0.1, 0.35, 0.8, 1]},
    {"id": 2, "type": "ConstantScalar", "value": [4, 0, 0, 0]},
    {"id": 3, "type": "Fresnel"},
    {"id": 4, "type": "ConstantColor",  "value": [0.9, 0.6, 0.2, 1]},
    {"id": 5, "type": "Multiply"},
    {"id": 6, "type": "ConstantScalar", "value": [0.25, 0, 0, 0]},
    {"id": 7, "type": "Output"}
  ],
  "links": [
    {"from": 1, "to": 7, "slot": 0},
    {"from": 2, "to": 3, "slot": 0},
    {"from": 3, "to": 5, "slot": 0},
    {"from": 4, "to": 5, "slot": 1},
    {"from": 5, "to": 7, "slot": 3},
    {"from": 6, "to": 7, "slot": 2}
  ]
}
```

Output slots are `0` BaseColor, `1` Metallic, `2` Roughness, `3` Emissive,
`4` Normal (tangent space), `5` Opacity. Node types: `ConstantScalar`,
`ConstantColor`, `TextureSample`, `UV`, `WorldPosition`, `WorldNormal`,
`VertexColor`, `Time`, `CameraVector`, `Multiply`, `Add`, `Subtract`, `Lerp`,
`DotProduct`, `OneMinus`, `Saturate`, `Power`, `Fresnel`, `Panner`, `Normalize`,
`Split`, `Output`.

A graph with a cycle, more than one `Output`, a dangling link, or an unknown node
type is rejected and the existing material is left untouched. A graph that parses
but generates GLSL the compiler refuses is stored and reported with
`compiled: false`; the object then renders with the shader template's default
surface rather than disappearing.

A `TextureSample` node's `texture` field is a **texture library name**, not a
path: `LoadTexture` puts an image into the library under a name, and every
material slot with that name binds it on the next frame. That means order does
not matter — a graph can name a texture that has not been imported yet, and it
resolves as soon as one arrives. Slots with no matching texture keep the 1x1
white placeholder, so an unresolved slot is a neutral multiply rather than a
broken material.

`LoadTexture` and `LoadMesh` resolve paths against the project root and reject
anything that escapes it, the same rule the project-tool family uses.

**Caveat:** textures upload as RGBA8 with mips; there is no BCn compression yet,
so a large texture set costs more GPU memory than it needs to.

### Tools that validate but do not yet apply

Some tools in the physics and gameplay families parse and validate their arguments
but have no backend wired to them. They used to return
`"success": true, "message": "... successfully"`, which tells an agent the world
changed when it did not. They now return `isError: true` with
`"implemented": false` and a `NOT APPLIED:` message:

- `TriggerDestruction`, `SpawnRagdoll`, `ModifyConstraint`, `QueryPhysicsState`,
  `ApplyForce`
- `ModifyInventory`

Argument validation on these is real — an invalid entity id is still rejected — so
they remain useful for checking a call before the backend lands.

Typical debugging loop for an agent:

1. `GetEngineStatus` — see what is running
2. `ControlSimulation {action: "pause"}` — freeze the world
3. `SpawnEntity` / `ModifyComponent` — change something
4. `ControlSimulation {action: "step", frames: 1}` — advance exactly one frame
5. `GetSceneContext` / `GetEngineLog` — observe the result
6. `RunPlayModeTests` — confirm nothing regressed

Rendering loop:

1. `GetRenderStats` — what the GPU actually drew and culled last frame
2. `SpawnEntity {template: "mesh", components: {mesh: {primitive: "sphere"}}}` — put
   something in front of the camera
3. `SetGPUCulling {occlusion: false}` / `SetShadows {enabled: false}` — did the
   artefact come from the HZB, or from the shadows?
4. `SetMaterialGraph` — change how it shades
5. `SetUpscaler {quality: "performance"}` — check the cost at a lower render
   resolution
6. `GetRenderStats` — confirm the counters moved the way you expected

---

## 5. Manual MCP embedding (custom hosts)

```cpp
#include "Core/MCP/MCPServer.h"
#include "Core/MCP/MCPToolFactory.h"  // declaration only; keeps this TU small

Core::MCP::MCPServerConfig cfg;
cfg.Host = "127.0.0.1";
cfg.Port = 3000;
cfg.ThreadPoolSize = 4;
cfg.AuthToken = "optional-secret";

auto mcpServer = std::make_unique<Core::MCP::MCPServer>(cfg);
mcpServer->SetActiveScene(scenePtr);

for (auto& tool : Core::MCP::CreateAllMCPTools()) {
    mcpServer->RegisterTool(std::move(tool));
}

if (!mcpServer->Start()) {
    // Start() now fails if the port cannot be bound, rather than reporting
    // success and failing asynchronously on the server thread.
}
```

Call `mcpServer->ProcessPendingRequests()` once per frame on the main thread, and
`mcpServer->SetActiveScene(nullptr)` followed by `mcpServer->Stop()` on shutdown.

---

## 6. Using the MCP server from a client

All JSON-RPC requests use `jsonrpc: "2.0"`. Requests carry an `id`; notifications
(for example `notifications/initialized`) do not, and are answered with `202 Accepted`
and an empty body. Batched arrays of messages are supported.

### Health check

```powershell
curl http://127.0.0.1:3000/health
```

### Initialize

```powershell
curl -X POST http://127.0.0.1:3000/mcp `
  -H "Content-Type: application/json" `
  -d "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\",\"clientInfo\":{\"name\":\"my-client\",\"version\":\"1.0.0\"}}}"
```

### List and call tools

```powershell
curl -X POST http://127.0.0.1:3000/mcp `
  -H "Content-Type: application/json" `
  -d "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}"

curl -X POST http://127.0.0.1:3000/mcp `
  -H "Content-Type: application/json" `
  -d "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"GetEngineStatus\",\"arguments\":{}}}"
```

With a bearer token add `-H "Authorization: Bearer your-secret"`.

---

## 6a. Tool annotations

Every tool publishes MCP annotations so a host can decide what needs confirmation:

```json
"annotations": {
  "title": "Get engine status",
  "readOnlyHint": true,
  "destructiveHint": false,
  "idempotentHint": true,
  "openWorldHint": false
}
```

- `readOnlyHint: true` — `GetRenderStats`, `ListMaterials`, `GetMaterialGraph`,
  `ListTextures`,
  `GetEngineStatus`, `GetEngineLog`, `ListProjectFiles`,
  `RunPlayModeTests`, `RunPerformanceTests`. Safe to auto-permit.
- `destructiveHint: true` — `SaveScene` (overwrites the target), `LoadScene`
  (destroys every entity in the active scene). Hosts always prompt on these.
- `openWorldHint: true` — `BuildForPlatform`, `CaptureProfilerTrace`. They write
  artifacts and invoke the toolchain.

A tool that declares nothing is treated as the most dangerous case, so annotate
deliberately. Tool names are validated against the 64-character MCP limit at
registration; an over-long name is rejected at startup instead of failing a
client's `tools/list`.

## 7. Supported core MCP methods

`initialize`, `ping`, `tools/list`, `tools/call`, `logging/setLevel`,
`resources/list`, `resources/read`, `prompts/list`, and any `notifications/*`
message (accepted and acknowledged).

---

## 8. Security model

1. **Loopback by default.** The server binds `127.0.0.1`. Widening `--mcp-host`
   without `--mcp-token` logs a warning.
2. **Bearer token.** When `AuthToken` is set, every request must carry
   `Authorization: Bearer <token>`; anything else gets `401`.
3. **DNS-rebinding protection.** Any request carrying a browser `Origin` header is
   rejected with `403` unless the origin is in `AllowedOrigins`. Non-browser clients
   send no `Origin` and are unaffected. There is no wildcard
   `Access-Control-Allow-Origin`: on a loopback server a wildcard lets any page the
   user visits drive the engine.
4. **Body size limit.** Requests above `MaxRequestBodyBytes` (8 MB) are rejected
   before parsing.
5. **Capability scopes.** Tools declare required scopes (`engine.read`,
   `engine.control`, `engine.test`, `engine.profile`, ...). Set
   `MCPServerConfig::EnforceCapabilityScopes` and grant scopes per session via the
   `capabilityScopes` parameter of `initialize` to restrict what an agent may do.
