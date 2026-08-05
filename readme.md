# WARNING: Experimental AI Project (Not for Production)

Do NOT use this project in production.

This repository is a purely AI-generated experimental project created to test the limits of current LLMs while building a custom multiplayer 3D game engine. Expect incomplete features, unstable behavior, rapid architectural changes, and missing hardening.

# Artificial-Intelligence-Game-Engine

A C++20 custom engine prototype focused on modern real-time rendering, ECS-driven gameplay, multiplayer architecture, and AI-assisted world tooling through MCP (Model Context Protocol).

The long-term vision combines:
- Performance on lower-end hardware (including modern iGPUs)
- High visual quality via a Forward+ rendering direction
- Authoritative listen-server multiplayer for small co-op/competitive sessions
- AI-driven scene and gameplay tooling through an internal MCP server

## Project Status (August 2026)

This codebase is in active experimental development.

Implemented foundation (code present and wired into build):
- Core application loop, windowing/events/input (SDL3)
- Logging, profiling hooks, crash/assert infrastructure
- Custom memory allocators and basic job system
- Vulkan RHI base, shader compilation integration, and renderer subsystems
- ECS foundation (EnTT), scene/entity/component/system layout
- Physics integration path (Jolt)
- Networking layer (GameNetworkingSockets) with replication/prediction modules
- MCP server, HTTP interface, and scene serialization/tool scaffolding
- Runtime MCP hosting in `Core::Application` (default startup + runtime host/port controls)
- Runtime UI authoring stack with widget blueprint/layout assets, data bindings, transitions, world-space widgets, localization, and modal focus routing

Roadmap snapshot from `engine_roadmap.md`:
- Completed: Roadmap phases through **Phase 30** (Field Integrity Remediation, Hardening & Closure)
- Latest release milestone: `v0.30.5.4`
- Current focus: stabilization, release hardening, and regression prevention workflows

### Wiring status (August 2026 audit)

A source audit found that much of the code above compiled but never ran. What
changed:

- `Scene::OnUpdate` previously ticked only the UI system. A `SystemPipeline`
  (`Core/ECS/SystemPipeline.h`) now owns and orders the simulation frame: input →
  character → physics → animation → IK → cameras → transform hierarchy → lighting
  and render collection.
- `JobSystem::Initialize()` was never called, so any parallel system would have
  spun forever in `Wait()`. The pipeline starts and stops it, and the job system
  now runs work inline rather than hanging when no workers exist.
- 18 `.cpp` files (all of Navigation, Terrain, Foliage, Skybox, IK, and five
  post-process passes) were absent from `CMakeLists.txt`. Nine of them turned out
  not to compile at all — the exclusion had hidden years of API drift against
  both their own headers and the installed Recast/Detour. All 18 are now fixed
  and building; no `.cpp` under `Core/` is left out of the build.
- `NavigationSystem` now ticks inside the frame pipeline, so pathfinding, crowd
  steering, and patrol routes actually run.
- `PostProcessManager` now registers its five passes (SSAO, depth of field,
  motion blur, bloom, color grading); previously the chain was empty at runtime.
- `VulkanContext::DrawFrame` drew a hard-coded triangle and ignored
  `RenderSystem`. The renderer is now scene-driven: a depth buffer, a
  vertex-input pipeline with an MVP push constant, `Mesh::UploadToGPU`, and the
  `Application` -> `VulkanContext` draw hand-off. The mesh pass is unlit; lights
  and materials are the next step.
- `RHI::RHIDevice` had no implementation anywhere in the tree, which is why
  `TerrainSystem`, `FoliageSystem`, and `SkyboxSystem` could not be constructed.
  `Core/RHI/Vulkan/VulkanDevice` now implements it over the existing
  `VulkanContext`, and all three run inside the pipeline when a renderer is
  present (headless runs pass `nullptr` and skip them).
- No MCP tool family compiled, and `CreateAllMCPTools()` was never called.
  Ten families now build and are registered at startup; four are quarantined with
  reasons recorded in `Core/MCP/MCPAllTools.h`.

### Rendering stack (August 2026)

The frame is no longer a single pass into the swapchain. The scene renders into
offscreen targets at a render resolution independent of the display, which is
what makes GI, upscaling, and an editor viewport possible at all:

```
shadow cascades + spot atlas -> GPU cluster cull -> G-buffer pass
      -> HZB build -> late cull + pass -> GI (compute)
      -> resolve -> FSR (EASU + RCAS) -> composite -> UI
```

**Clustered lighting** (`Core/Renderer/Lighting/ClusteredLightCuller.*`).
Punctual lights live in a storage buffer culled into a
`ceil(w/16) x ceil(h/16) x 32` froxel grid, exponential in Z. That removed the
old fixed 16-point/8-spot cap: verified with 40 point lights producing over a
million light-to-froxel assignments with no overflow.

**GPU-driven cluster culling** (`Core/Renderer/GPUDriven/`). Meshes are
clusterised into runs of <= 128 triangles along a Morton curve and copied into a
single merged vertex/index arena, so a frame is one vertex bind, one index bind,
and one indirect draw per material. A compute pass tests every cluster against
the frustum, its backface cone, and a hierarchical-Z pyramid, then writes
`VkDrawIndexedIndirectCommand`s straight into the buffer the draw consumes — the
visibility decision never returns to the CPU. Culling is two-phase: clusters
rejected for occlusion against the previous frame's HZB get a second test
against an HZB rebuilt from this frame's depth, so a camera cut does not pop
geometry. Requires `multiDrawIndirect` and `drawIndirectFirstInstance`; without
them the renderer falls back to per-mesh direct draws.

**Dynamic global illumination** (`Core/Renderer/GI/`). Screen-space radiance
gathering at half resolution backed by a world-space irradiance cache: 16,384 L1
spherical-harmonic probes on a camera-centred grid, fed by screen-space
injection and used as the fallback where a screen trace misses. Temporally
reprojected through the previous frame's view-projection, so a handful of rays
per pixel per frame converges. Indirect light is modulated by the G-buffer
albedo in the resolve pass. Lumen-shaped rather than Lumen: no surface cache, no
hardware ray tracing, no distance fields.

**Material graph** (`Core/Renderer/Material/`). A DAG of ~22 node types
(constants, UV, texture sample, math, Fresnel, panner, PBR output) compiled to
GLSL and injected into the lit shader template, with a permutation cache keyed by
graph hash so an unchanged graph never rebuilds its pipeline. Cycles, missing
outputs, and unknown node types are rejected at authoring time. Graphs serialise
to JSON and are authorable over MCP. `DrawCommand::MaterialIndex` now indexes a
real `MaterialLibrary` rather than nothing.

**FSR upscaling** (`Core/Renderer/Upscaling/FSRUpscaler.*`). An in-engine
implementation of the FSR 1 pipeline — EASU edge-adaptive spatial upsampling
followed by RCAS contrast-adaptive sharpening — as compute passes, with the
standard quality presets driving the render resolution, and Halton(2,3) jitter
folded into the projection. Not a binding of AMD's SDK, which is not a dependency
of this project.

**Shadows** (`Core/Renderer/Shadows/ShadowRenderer.*`). Cascaded shadow maps for
the directional light, plus a shared atlas for spot lights and point-light cube
shadows (six contiguous tiles each, face picked by major axis). Cascades are
page-cached: a 16x16 page grid per cascade is invalidated only where occluders
actually changed, so a static scene does no shadow work at all after its first
frame. Occluder identity comes from hashing (mesh, transform) rather than an
index, because the draw list is rebuilt every frame and its order is not stable;
anything that appears *or disappears* from that set dirties both its old and new
footprint, which is what stops a shadow standing where an object used to be. Cascades use the
practical split scheme, fit a bounding sphere per frustum slice so their size
does not change as the camera rotates, and snap to a texel grid so edges do not
crawl. Spot tiles are allocated per frame by importance, so when there are more
casters than tiles the tiles go to the lights most likely to be noticed and the
rest stay lit but unshadowed. Every shadow view culls with the same cluster
shader as the main view and draws from the same merged arena — a mesh is never
resident twice or clustered differently for its own shadow. Sampling is a
comparison sampler (one tap is already 2x2 hardware PCF) widened by a 3x3 loop,
with a normal offset scaled by the grazing angle.

**Asset import** (`Core/Renderer/Textures/TextureLibrary.*`). Images load through
stb_image with a full mip chain generated by successive blits, and a material
graph's `TextureSample` slot name is the library key — so importing a texture
called `BaseColor` fills every slot of that name, and a graph can be authored
before the texture it references exists. Each material owns its own texture
descriptor set, because slot 0 of one material is not slot 0 of another.
`Mesh::LoadGLTF` was implemented against cgltf but never called; it is now
reachable over MCP and feeds the same GPU-driven path as procedural primitives.

**Editor viewport** (`Core/Editor/Panels/ViewportPanel.*`). The renderer's own
post-upscale output as an ImGui image, so the editor shows exactly what the game
sees rather than a second render path that would drift. Fly camera, click-to-
select picking, a translate/rotate/scale gizmo, and play/pause/single-step.

**Render-thread split** (`Core/Renderer/RenderThread.*`). Simulation for frame
N+1 runs while the render thread submits frame N. The sync point sits immediately
before ImGui's `NewFrame`, and the frame packet (draw commands, lights, camera)
is copied rather than referenced, so the render thread never reads live ECS
state. `Scene::OnUpdate` is split into simulation and UI halves for this;
`--no-render-thread` runs submission inline.

Every one of these is reachable from the MCP tool surface — see
[`MCP_SERVER_GUIDE.md`](MCP_SERVER_GUIDE.md).

A note on one fix worth knowing about: the engine built its projection matrices
with `glm::perspective` and no `GLM_FORCE_DEPTH_ZERO_TO_ONE`, producing OpenGL's
`[-1, 1]` clip depth while Vulkan clips to `[0, 1]`. The near half of every
frustum was being clipped away, and the HZB occlusion test and GI reprojection
both read that depth back assuming `[0, 1]`. It is now a `PUBLIC` compile
definition so no translation unit can disagree.

See [`docs/SOTA_GAP_ANALYSIS.md`](docs/SOTA_GAP_ANALYSIS.md) for what still
separates this from Unreal 5.7 / Unity 6, in priority order.

## Architecture Overview

Primary layers in this repository:
- Core System Layer: app lifecycle, memory allocators, job scheduling, diagnostics
- RHI Layer: abstract interfaces with Vulkan implementation
- Renderer Layer: Forward+/Z-prepass/shadow/taa/dynamic-resolution modules
- ECS Gameplay Layer: EnTT scene, components, systems, hierarchy updates
- Physics Layer: Jolt world setup and ECS synchronization systems
- Networking Layer: server/client + replication/prediction/reconciliation systems
- MCP Layer: local AI tool endpoint for scene inspection, manipulation, and
  development control (pause/step the simulation, read engine logs, run the
  play-mode and performance suites, capture profiler traces), plus rendering
  control: read the live culling/GI/upscaling pipeline, toggle each culling
  stage, tune GI, switch FSR quality, author material graphs, and drive the
  editor viewport. Connect an agent with the bundled stdio bridge — see
  [`MCP_SERVER_GUIDE.md`](MCP_SERVER_GUIDE.md).

## Technology Stack

From build configuration and project design:
- Language: C++20
- Build: CMake
- Package management: vcpkg (manifest mode)
- Windowing/Input: SDL3
- Rendering API: Vulkan (current implementation)
- Math: GLM
- ECS: EnTT
- Physics: Jolt
- Networking: GameNetworkingSockets
- Shaders/Compilation: shaderc + GLSL/SPIR-V workflow
- Logging/Profiling: spdlog + Tracy
- MCP/Tools: cpp-httplib + nlohmann_json
- Scripting dependency (planned integration): Lua

## Repository Layout

Key directories:
- `Core/` - Engine systems and subsystems
- `Core/RHI/` - Rendering abstraction and Vulkan backend
- `Core/Renderer/` - High-level rendering passes/systems
- `Core/ECS/` - Scene, entities, components, and systems
- `Core/Physics/` - Physics world and integration adapters
- `Core/Network/` - Multiplayer networking stack
- `Core/Renderer/GPUDriven/` - Merged geometry arena and cluster culling
- `Core/Renderer/GI/` - Dynamic global illumination
- `Core/Renderer/Material/` - Material graph and GLSL codegen
- `Core/Renderer/Shadows/` - Cascaded shadow maps and the spot shadow atlas
- `Core/Renderer/Textures/` - Image import and GPU texture residency
- `Core/Editor/` - Editor module, viewport, and panels
- `Core/MCP/` - AI tool server and scene serialization
- `Shaders/` - Shader sources
- `src/` - Entry point (`main.cpp`)
- `build/` - Local build outputs and generated project files

## Documentation

- Build guide: [`BUILD_GUIDE.md`](BUILD_GUIDE.md)
- MCP server guide: [`MCP_SERVER_GUIDE.md`](MCP_SERVER_GUIDE.md)
- Implementation roadmap: [`engine_roadmap.md`](engine_roadmap.md)
- Detailed phase plans: [`docs/plans/`](docs/plans/)

## Build Prerequisites

Windows-oriented setup (current workspace target):
- Visual Studio 2022 with MSVC C++ toolchain
- CMake 3.20+
- Vulkan SDK installed
- Git

## Build and Run (Windows)

```powershell
git submodule update --init --recursive
.\vcpkg\bootstrap-vcpkg.bat

cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE=.\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows

cmake --build build --config Release --target ALL_BUILD --clean-first -- /m /nologo /verbosity:minimal
ctest --test-dir build -C Release
.\build\release\AIGameEngine.exe
```

For debug builds, replace `Release` with `Debug`.
For full setup/troubleshooting instructions, see [`BUILD_GUIDE.md`](BUILD_GUIDE.md).

## Known Limitations

- This repository is not production-ready by design.
- Feature completeness and runtime stability vary by subsystem.
- Security hardening for AI-exposed tool surfaces is not complete.
- Automated test coverage and release packaging are still limited.
- Shadow paging caches the *work*, not the memory: every cascade page is always
  allocated. True sparse residency needs an optional device feature, and gating
  shadows on it costs more than the memory it saves.
- Point-light cube shadows are capped at two lights, because the uniform block
  carries six matrices each.
- No texture compression: images upload as RGBA8 with mips. `stb_dxt` is
  available for a BCn cook step but nothing calls it.
- glTF import brings in geometry only. Materials, skeletons, and animations in
  the file are ignored, and the mesh is shaded with whichever material index the
  caller names.
- Skeletal meshes do not go through the new geometry pass. The merged arena
  stores one vertex layout, and GPU skinning is not wired into it yet.
- Shadows are not part of the new frame. `ShadowPass` and `VirtualShadowMapCache`
  still have no consumer.
- Vulkan validation is off in release builds; set `AIGE_VULKAN_VALIDATION=1` to
  force the layers on when they are installed.

## Why This Exists

This project is an experiment in AI-assisted engine development at scale:
- To probe how far current LLM workflows can go in end-to-end engine architecture
- To evaluate where human oversight remains critical (stability, validation, security, and final optimization)

If you use this repository, treat it as research code and prototype infrastructure only.

<!-- release-doc-sync:2026-08-05 -->

## Release Sync (2026-08-05)

- Verified Release build of `ALL_BUILD` and test sweep: `ctest --test-dir build -C Release`
  (**20/20 passed**), including the new `EngineCoreRenderPipelineTests` covering
  material-graph codegen, cycle rejection, JSON round-tripping, and the mesh
  clusteriser's index-permutation invariants.
- Verified the rendering stack live against an NVIDIA RTX 3050 by driving it over
  MCP: two primitives resident in the GPU scene as 43 clusters, 18 of them
  backface-cone culled, one indirect draw per material; `SetGPUCulling
  {gpuDriven:false}` correctly falls back to 2 direct draws; `SetUpscaler
  {quality:"performance"}` moved the render resolution from 853x480 to 640x360
  against a 1280x720 swapchain; `SetMaterialGraph` compiled a Fresnel-emissive
  graph and the material pipeline count went 1 -> 2.
- Fixed a latent bug the above uncovered: `VulkanBuffer` ignored
  `BufferDescriptor::mapped` for vertex and index buffers, so every
  `Mesh::UploadToGPU` had been failing silently.
- Runtime flags: `--disable-mcp`, `--mcp-host=<host>`, `--mcp-port=<port>`,
  `--mcp-token=<secret>`, `--no-render-thread`, and the
  `AIGE_VULKAN_VALIDATION=1` environment variable to force the Vulkan validation
  layers on in a release build.

### Earlier: Release Sync (2026-04-15)

- Verified clean Release rebuild: `cmake --build build --config Release --target ALL_BUILD --clean-first -- /m /nologo /verbosity:minimal`.
- Verified Release test sweep: `ctest --test-dir build -C Release` (**18/18 passed**).
- Confirmed executable composition: `AIGameEngine` links `EngineCore`, and `EngineCore` includes `Core/MCP/HttpServer.cpp` + `Core/MCP/MCPServer.cpp`.
- Runtime MCP integration is now enabled in `Core::Application` by default; runtime flags: `--disable-mcp`, `--mcp-host=<host>`, `--mcp-port=<port>`.
