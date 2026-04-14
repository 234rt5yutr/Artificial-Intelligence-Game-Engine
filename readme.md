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

## Project Status (April 2026)

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
- Runtime UI authoring stack with widget blueprint/layout assets, data bindings, transitions, world-space widgets, localization, and modal focus routing

Roadmap snapshot from `engine_roadmap.md`:
- Completed: Roadmap phases through **Phase 27** (including Stage 27 UI persistence sub-step `27.3.4`)
- Latest release milestone: `v0.27.3.4`
- Current focus: **Phase 28** profiling, automation, and production build pipeline

## Architecture Overview

Primary layers in this repository:
- Core System Layer: app lifecycle, memory allocators, job scheduling, diagnostics
- RHI Layer: abstract interfaces with Vulkan implementation
- Renderer Layer: Forward+/Z-prepass/shadow/taa/dynamic-resolution modules
- ECS Gameplay Layer: EnTT scene, components, systems, hierarchy updates
- Physics Layer: Jolt world setup and ECS synchronization systems
- Networking Layer: server/client + replication/prediction/reconciliation systems
- MCP Layer: local AI tool endpoint for scene inspection and manipulation workflows

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

cmake --build build --config Release
.\build\release\AIGameEngine.exe
```

For debug builds, replace `Release` with `Debug`.
For full setup/troubleshooting instructions, see [`BUILD_GUIDE.md`](BUILD_GUIDE.md).

## Known Limitations

- This repository is not production-ready by design.
- Feature completeness and runtime stability vary by subsystem.
- Security hardening for AI-exposed tool surfaces is not complete.
- Automated test coverage and release packaging are still limited.

## Why This Exists

This project is an experiment in AI-assisted engine development at scale:
- To probe how far current LLM workflows can go in end-to-end engine architecture
- To evaluate where human oversight remains critical (stability, validation, security, and final optimization)

If you use this repository, treat it as research code and prototype infrastructure only.
