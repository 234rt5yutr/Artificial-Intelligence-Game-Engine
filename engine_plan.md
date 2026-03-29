# Deep Research & Architecture Plan: Custom Multiplayer 3D Game Engine

## 1. Executive Summary
This document outlines the architectural plan for a custom, high-performance 3D game engine written in C++. The engine is designed to bridge the gap between low-end hardware compatibility (modern iGPUs) and extreme high-quality graphics (comparable to Unreal/Unity). It features a switchable first-person and third-person camera system and a robust listen-server multiplayer architecture suitable for co-op or small-scale competitive gameplay.

## 2. Core Philosophy & Design Pillars
*   **Data-Oriented Design (DOD):** Maximizing CPU cache utilization through an Entity Component System (ECS) to ensure consistent framerates even on CPU-constrained systems.
*   **Bandwidth-Aware Rendering:** Targeting iGPUs requires minimizing VRAM bandwidth. Traditional Deferred Rendering is too bandwidth-heavy; therefore, a Forward+ (Clustered Forward) rendering pipeline is the chosen architecture.
*   **Scalability:** The RHI (Render Hardware Interface) will support Vulkan and DirectX 12 for modern explicit control, falling back where necessary.
*   **Authoritative Listen Server:** Multiplayer logic where the host acts as the authoritative server.

## 3. Technology Stack
*   **Language:** C++20
*   **Build System:** CMake
*   **Package Manager:** vcpkg
*   **Windowing & Input:** SDL3 or GLFW
*   **Graphics APIs:** Vulkan (Primary), DirectX 12 (Secondary)
*   **Math Library:** GLM
*   **ECS Framework:** EnTT
*   **Physics Engine:** Jolt Physics (highly optimized, inherently multithreaded)
*   **Networking:** Valve GameNetworkingSockets (GNS) for reliable UDP
*   **Shader Compilation:** glslang / DirectX Shader Compiler (DXC)
*   **Profiling:** Tracy Profiler

## 4. Architectural Layers

### 4.1. Core System Layer
*   **Memory Management:** Custom memory allocators (Linear, Pool, Frame/Scratch allocators) to avoid global `new`/`delete` overhead.
*   **Job System:** A multi-threaded task scheduler using lock-free work-stealing queues. All engine systems (physics, animation, culling) will submit jobs here.
*   **File System & VFS:** Virtual File System to handle asynchronous asset loading from packed archives or raw directories.

### 4.2. Render Hardware Interface (RHI)
A completely abstracted API layer wrapping Vulkan/DX12 concepts:
*   `RHIDevice`, `RHICommandList`, `RHIBuffer`, `RHITexture`, `RHIPipelineState`.
*   **GPU Memory Management:** Integration of Vulkan Memory Allocator (VMA) and D3D12MA.
*   **Bindless Resources:** Utilizing bindless textures and buffers to reduce CPU overhead during draw call submission.

### 4.3. High-Level Renderer (Forward+)
*   **Z-Prepass:** Render depth first to optimize overdraw.
*   **Hierarchical Z-Buffer (HzB):** GPU-driven occlusion culling.
*   **Light Culling (Compute):** Screen space is divided into a 3D grid (frustum clusters). A compute shader calculates which lights intersect which clusters, writing to a light index list.
*   **Opaque Pass:** Shaders read the cluster light list and compute physically-based shading (PBR).
*   **Post-Processing:** Temporal Anti-Aliasing (TAA), Bloom, Tonemapping (ACES), and dynamic resolution scaling (FSR 2.0 / DLSS integration support).

### 4.4. Gameplay & ECS Foundation
*   **EnTT Integration:** Game objects are merely IDs. Functionality is defined by components (Transform, MeshRenderer, RigidBody, PlayerInput).
*   **Systems:** Iterate over specific component combinations tightly packed in memory.
*   **Jolt Physics Integration:** Mapping Jolt's bodies to ECS entities, updated via the Job System.

### 4.5. Switchable Camera & Player Controller
*   **First-Person Mode:** Camera inherits transform from the character's head bone. Arms/weapon meshes render on a separate depth layer to prevent clipping.
*   **Third-Person Mode:** Camera uses a Spring-Arm component with sphere-casting against the environment to prevent clipping through walls.
*   **Transition System:** Smooth interpolation between the two states, driven by a state machine adjusting FOV and near-clip planes dynamically.

### 4.6. Networking Architecture
*   **Listen Server:** One instance acts as the Authority. 
*   **Client-Side Prediction:** The local player simulates movement immediately, recording inputs.
*   **Server Reconciliation:** If the server's authoritative state differs from the client's past state, the client rewinds to the server state and replays unacknowledged inputs.
*   **Entity Interpolation:** Remote players are simulated with a slight delay to ensure smooth movement regardless of network jitter.
*   **Delta Compression:** Only state changes since the last acknowledged packet are sent over the network.

### 4.7. AI-Driven Automatic Game Design (MCP)
*   **Model Context Protocol (MCP) Server:** A dedicated local host service that safely exposes the engine's internal state (ECS registry, file system, active scene) to external AI assistants (e.g., Claude, local LLMs).
*   **Semantic Scene Representation:** Automatic serialization of the 3D scene (entities, transforms, materials, lights) into an optimal text/JSON format for LLM context windows.
*   **Command Execution Interface:** A secure sandboxed API allowing AI agents to spawn entities, modify components, arrange lighting, and tweak physics parameters in real-time.
*   **Auto-Generation Tools:** Prompt-based generation of level layouts, procedural entity population, and gameplay rule scripting via the MCP.
