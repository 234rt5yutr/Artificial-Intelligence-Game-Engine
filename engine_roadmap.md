# Implementation Roadmap: Custom Engine

This roadmap breaks down the engine development into strict, single-action steps to ensure continuous progress without overwhelming complexity at any single point.

## Phase 1: Core Foundation & Tooling (Weeks 1-2)
*   [x] Step 1.1: Initialize Git repository and ``.gitignore`` for C++/CMake.
*   [x] Step 1.2: Set up the root ``CMakeLists.txt`` with standard C++20 compiler flags.
*   [x] Step 1.3: Integrate ``vcpkg`` as a git submodule and set up the manifest file (``vcpkg.json``).
*   [x] Step 1.4: Add ``spdlog`` dependency and implement the engine ``Log`` static class.
*   [x] Step 1.5: Create the ``Core/Memory/Allocator.h`` interface class.
*   [x] Step 1.6: Implement a Linear Allocator for fast, per-frame scratch memory.
*   [x] Step 1.7: Implement a Pool Allocator for fixed-size object allocations.
*   [x] Step 1.8: Add ``glm`` dependency and create engine-specific math alias typedfs.
*   [x] Step 1.9: Implement the multi-threaded Job System using standard C++ threads and concurrent queues.
*   [x] Step 1.10: Add ``Tracy`` profiler dependency and implement profiling macros across existing systems.
*   [x] Step 1.11: Implement the Assertion and Crash-handling system.

## Phase 2: Platform & Windowing (Week 3)
*   [x] Step 2.1: Add ``SDL3`` dependency.
*   [x] Step 2.2: Implement ``Application`` singleton to manage the main game loop.
*   [x] Step 2.3: Implement ``Window`` class wrapping SDL3 window creation.
*   [x] Step 2.4: Implement ``EventSystem`` utilizing a dispatcher pattern mapped to SDL events.
*   [x] Step 2.5: Implement ``InputSystem`` for capturing keyboard, mouse, and gamepad states.
*   [x] Step 2.6: Bind window close and input events to the ``Application`` loop to allow safe shutdown.

## Phase 3: Render Hardware Interface (RHI) - Base & Vulkan Setup (Weeks 4-6)
*   [x] Step 3.1: Define abstract base RHI classes: ``RHIDevice``, ``RHICommandList``, ``RHIBuffer``, ``RHITexture``.
*   [x] Step 3.2: Add ``Vulkan SDK`` dependency.
*   [x] Step 3.3: Implement Vulkan Instance creation with Validation Layers enabled in Debug builds.
*   [x] Step 3.4: Implement Vulkan Physical Device selection (preferring discrete GPUs, supporting iGPUs).
*   [x] Step 3.5: Implement Vulkan Logical Device and Queue retrieval.
*   [x] Step 3.6: Integrate Vulkan Memory Allocator (VMA).
*   [x] Step 3.7: Implement Vulkan Swapchain creation and recreation logic on window resize.
*   [x] Step 3.8: Implement Vulkan Command Pool and Command Buffer allocation.
*   [x] Step 3.9: Implement Vulkan Synchronization objects (Semaphores, Fences).
*   [x] Step 3.10: Integrate ``glslang`` or ``DXC`` to compile GLSL/HLSL to SPIR-V dynamically.
*   [x] Step 3.11: Implement Shader Module creation.
*   [x] Step 3.12: Implement Graphics Pipeline State Object (PSO) creation logic.
*   [x] Step 3.13: Get a colored triangle rendering on screen using raw RHI calls.

## Phase 4: High-Level Renderer & Forward+ Architecture (Weeks 7-9)
*   [x] Step 4.1: Implement generalized Buffer abstraction (Vertex, Index, Uniform, Storage).
*   [x] Step 4.2: Implement generalized Texture abstraction (2D, 3D, Cubemap) and Sampler objects.
*   [ ] Step 4.3: Build an asynchronous texture/mesh uploader using a staging buffer and transfer queue.
*   [ ] Step 4.4: Implement a primitive specific Mesh loader (assimp/cgltf) to load GLTF files.
*   [x] Step 4.5: Implement the fundamental PBR shader (Albedo, Normal, Metallic, Roughness).
*   [x] Step 4.6: Implement Render Passes structure grouping command buffer submissions.
*   [x] Step 4.7: Implement the Z-Prepass render pass.
*   [x] Step 4.8: Implement the Compute Shader for Frustum Cluster generation.
*   [ ] Step 4.9: Implement the Compute Shader for Light bounding sphere to Cluster intersection testing.
*   [x] Step 4.10: Create GPU buffers for Light Index Lists and Grid definitions.
*   [ ] Step 4.11: Update the PBR shader to iterate over lights within the current fragment's cluster.
*   [ ] Step 4.12: Implement simple shadow mapping (Directional Light).

## Phase 5: Entity Component System & Gameplay Rules (Weeks 10-11)
*   [ ] Step 5.1: Add ``EnTT`` dependency.
*   [ ] Step 5.2: Create the ``Scene`` class to wrap the EnTT registry.
*   [ ] Step 5.3: Implement basic components: ``TransformComponent``, ``MeshComponent``, ``LightComponent``.
*   [ ] Step 5.4: Implement the Render System: iterates over Transform and Mesh components to submit draw calls to the Renderer.
*   [ ] Step 5.5: Implement the Light System: iterates over Light components to update RHI light buffers.
*   [ ] Step 5.6: Implement parent-child Transform hierarchy and Local-to-World matrix recalculation system.

## Phase 6: Physics Integration (Weeks 12-13)
*   [ ] Step 6.1: Add ``Jolt Physics`` dependency.
*   [ ] Step 6.2: Initialize the Jolt Physics System and configure BroadPhase layers.
*   [ ] Step 6.3: Implement Jolt JobSystem integration with the engine's multi-threaded Job System.
*   [ ] Step 6.4: Create physics components: ``RigidBodyComponent``, ``ColliderComponent`` (Box, Sphere, Capsule, Mesh).
*   [ ] Step 6.5: Implement Physics System: step simulation based on Delta Time.
*   [ ] Step 6.6: Implement Physics-to-Transform Sync System (updating ECS transforms from Jolt results).

## Phase 7: Switchable Camera System (Week 14)
*   [ ] Step 7.1: Implement ``CameraComponent`` (FOV, Near/Far planes, Projection Matrix calculation).
*   [ ] Step 7.2: Create the Character Controller System interfacing with Jolt Physics for player movement.
*   [ ] Step 7.3: Implement input mapping to velocity and rotation.
*   [ ] Step 7.4: Implement First-Person View logic: Attach camera coordinate space to the player entity.
*   [ ] Step 7.5: Implement Third-Person View logic: Implement a Spring-Arm constraint utilizing Raycasts against the physics world.
*   [ ] Step 7.6: Implement the interpolator state machine to smoothly pan and zoom between FP and TP view modes upon user input.

## Phase 8: Multi-Player Networking (Weeks 15-18)
*   [ ] Step 8.1: Add ``GameNetworkingSockets`` dependency.
*   [ ] Step 8.2: Implement ``NetworkManager`` to initialize sockets.
*   [ ] Step 8.3: Implement Server listen socket bindings.
*   [ ] Step 8.4: Implement Client connection connection requests and handshake.
*   [ ] Step 8.5: Define network packet schemas using fixed-size structs or FlatBuffers.
*   [ ] Step 8.6: Create the ``NetworkTransformComponent`` to flag entities for replication.
*   [ ] Step 8.7: Implement Server serialization loops broadcasting replicated components.
*   [ ] Step 8.8: Implement Client deserialization loop updating local proxy entities.
*   [ ] Step 8.9: Implement Client-Side Prediction: decouple client input execution from server acknowledgment.
*   [ ] Step 8.10: Implement Server Reconciliation: validate client states and correct mispredictions without snapping.

## Phase 9: Optimization & Polish (Weeks 19+)
*   [ ] Step 9.1: Implement GPU-driven Frustum Culling via Compute Shaders generating indirect draw commands.
*   [ ] Step 9.2: Implement Temporal Anti-Aliasing (TAA) pass.
*   [ ] Step 9.3: Add Dynamic Resolution Scaling based on frame completion time metrics.
*   [ ] Step 9.4: Optimize ECS cache lines and parallelize remaining Systems via the Job System.
*   [ ] Step 9.5: Finalize asset cooking pipelines for production.

## Phase 10: Automatic Game Design & AI Agents (MCP) (Weeks 20-22)
*   [ ] Step 10.1: Add lightweight HTTP/WebSocket dependency for internal server communication (e.g., `cpp-httplib` or `nlohmann_json`).
*   [ ] Step 10.2: Implement the MCP (Model Context Protocol) Server base architecture to listen for local tool calls from external AI environments (like VS Code or custom UI).
*   [ ] Step 10.3: Define JSON schemas for serializing the active `EnTT` Registry and core Game Objects (Transforms, Lights, Meshes).
*   [ ] Step 10.4: Implement the `GetSceneContext` MCP tool capable of dumping the current world state, active cameras, and level layout in human/LLM-readable text format.
*   [ ] Step 10.5: Implement the `SpawnEntity` MCP tool for AI agents to instantiate meshes, colliders, and lights programmatically via prompt execution.
*   [ ] Step 10.6: Implement the `ModifyComponent` MCP tool allowing the AI to tweak lighting colors, physics mass, or translate/rotate objects based on semantic commands.
*   [ ] Step 10.7: Implement the `ExecuteScript` MCP tool for injecting dynamic AI-authored Lua/C++ script snippets to define custom gameplay behaviors on the fly.
*   [ ] Step 10.8: Integrate a sandboxed action-validation layer to ensure AI agents cannot crash the engine when generating invalid transforms or extreme array allocations.
*   [ ] Step 10.9: Build an overarching "Auto-Level Designer" loop that translates abstract user prompts into sequence of MCP tool calls.
