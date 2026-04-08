# Implementation Roadmap: Custom Engine

This roadmap breaks down the engine development into strict, single-action steps to ensure continuous progress without overwhelming complexity at any single point.

## Phase 1: Core Foundation & Tooling (Weeks 1-2)

* [X]  Step 1.1: Initialize Git repository and ``.gitignore`` for C++/CMake.
* [X]  Step 1.2: Set up the root ``CMakeLists.txt`` with standard C++20 compiler flags.
* [X]  Step 1.3: Integrate ``vcpkg`` as a git submodule and set up the manifest file (``vcpkg.json``).
* [X]  Step 1.4: Add ``spdlog`` dependency and implement the engine ``Log`` static class.
* [X]  Step 1.5: Create the ``Core/Memory/Allocator.h`` interface class.
* [X]  Step 1.6: Implement a Linear Allocator for fast, per-frame scratch memory.
* [X]  Step 1.7: Implement a Pool Allocator for fixed-size object allocations.
* [X]  Step 1.8: Add ``glm`` dependency and create engine-specific math alias typedfs.
* [X]  Step 1.9: Implement the multi-threaded Job System using standard C++ threads and concurrent queues.
* [X]  Step 1.10: Add ``Tracy`` profiler dependency and implement profiling macros across existing systems.
* [X]  Step 1.11: Implement the Assertion and Crash-handling system.

## Phase 2: Platform & Windowing (Week 3)

* [X]  Step 2.1: Add ``SDL3`` dependency.
* [X]  Step 2.2: Implement ``Application`` singleton to manage the main game loop.
* [X]  Step 2.3: Implement ``Window`` class wrapping SDL3 window creation.
* [X]  Step 2.4: Implement ``EventSystem`` utilizing a dispatcher pattern mapped to SDL events.
* [X]  Step 2.5: Implement ``InputSystem`` for capturing keyboard, mouse, and gamepad states.
* [X]  Step 2.6: Bind window close and input events to the ``Application`` loop to allow safe shutdown.

## Phase 3: Render Hardware Interface (RHI) - Base & Vulkan Setup (Weeks 4-6)

* [X]  Step 3.1: Define abstract base RHI classes: ``RHIDevice``, ``RHICommandList``, ``RHIBuffer``, ``RHITexture``.
* [X]  Step 3.2: Add ``Vulkan SDK`` dependency.
* [X]  Step 3.3: Implement Vulkan Instance creation with Validation Layers enabled in Debug builds.
* [X]  Step 3.4: Implement Vulkan Physical Device selection (preferring discrete GPUs, supporting iGPUs).
* [X]  Step 3.5: Implement Vulkan Logical Device and Queue retrieval.
* [X]  Step 3.6: Integrate Vulkan Memory Allocator (VMA).
* [X]  Step 3.7: Implement Vulkan Swapchain creation and recreation logic on window resize.
* [X]  Step 3.8: Implement Vulkan Command Pool and Command Buffer allocation.
* [X]  Step 3.9: Implement Vulkan Synchronization objects (Semaphores, Fences).
* [X]  Step 3.10: Integrate ``glslang`` or ``DXC`` to compile GLSL/HLSL to SPIR-V dynamically.
* [X]  Step 3.11: Implement Shader Module creation.
* [X]  Step 3.12: Implement Graphics Pipeline State Object (PSO) creation logic.
* [X]  Step 3.13: Get a colored triangle rendering on screen using raw RHI calls.

## Phase 4: High-Level Renderer & Forward+ Architecture (Weeks 7-9)

* [X]  Step 4.1: Implement generalized Buffer abstraction (Vertex, Index, Uniform, Storage).
* [X]  Step 4.2: Implement generalized Texture abstraction (2D, 3D, Cubemap) and Sampler objects.
* [X]  Step 4.3: Build an asynchronous texture/mesh uploader using a staging buffer and transfer queue.
* [X]  Step 4.4: Implement a primitive specific Mesh loader (assimp/cgltf) to load GLTF files.
* [X]  Step 4.5: Implement the fundamental PBR shader (Albedo, Normal, Metallic, Roughness).
* [X]  Step 4.6: Implement Render Passes structure grouping command buffer submissions.
* [X]  Step 4.7: Implement the Z-Prepass render pass.
* [X]  Step 4.8: Implement the Compute Shader for Frustum Cluster generation.
* [X]  Step 4.9: Implement the Compute Shader for Light bounding sphere to Cluster intersection testing.
* [X]  Step 4.10: Create GPU buffers for Light Index Lists and Grid definitions.
* [X]  Step 4.11: Update the PBR shader to iterate over lights within the current fragment's cluster.
* [X]  Step 4.12: Implement simple shadow mapping (Directional Light).

## Phase 5: Entity Component System & Gameplay Rules (Weeks 10-11)

* [X]  Step 5.1: Add ``EnTT`` dependency.
* [X]  Step 5.2: Create the ``Scene`` class to wrap the EnTT registry.
* [X]  Step 5.3: Implement basic components: ``TransformComponent``, ``MeshComponent``, ``LightComponent``.
* [X]  Step 5.4: Implement the Render System: iterates over Transform and Mesh components to submit draw calls to the Renderer.
* [X]  Step 5.5: Implement the Light System: iterates over Light components to update RHI light buffers.
* [X]  Step 5.6: Implement parent-child Transform hierarchy and Local-to-World matrix recalculation system.

## Phase 6: Physics Integration (Weeks 12-13)

* [X]  Step 6.1: Add ``Jolt Physics`` dependency.
* [X]  Step 6.2: Initialize the Jolt Physics System and configure BroadPhase layers.
* [X]  Step 6.3: Implement Jolt JobSystem integration with the engine's multi-threaded Job System.
* [X]  Step 6.4: Create physics components: ``RigidBodyComponent``, ``ColliderComponent`` (Box, Sphere, Capsule, Mesh).
* [X]  Step 6.5: Implement Physics System: step simulation based on Delta Time.
* [X]  Step 6.6: Implement Physics-to-Transform Sync System (updating ECS transforms from Jolt results).

## Phase 7: Switchable Camera System (Week 14)

* [X]  Step 7.1: Implement ``CameraComponent`` (FOV, Near/Far planes, Projection Matrix calculation).
* [X]  Step 7.2: Create the Character Controller System interfacing with Jolt Physics for player movement.
* [X]  Step 7.3: Implement input mapping to velocity and rotation.
* [X]  Step 7.4: Implement First-Person View logic: Attach camera coordinate space to the player entity.
* [X]  Step 7.5: Implement Third-Person View logic: Implement a Spring-Arm constraint utilizing Raycasts against the physics world.
* [X]  Step 7.6: Implement the interpolator state machine to smoothly pan and zoom between FP and TP view modes upon user input.

## Phase 8: Multi-Player Networking (Weeks 15-18)

* [X]  Step 8.1: Add ``GameNetworkingSockets`` dependency.
* [X]  Step 8.2: Implement ``NetworkManager`` to initialize sockets.
* [X]  Step 8.3: Implement Server listen socket bindings.
* [X]  Step 8.4: Implement Client connection connection requests and handshake.
* [X]  Step 8.5: Define network packet schemas using fixed-size structs or FlatBuffers.
* [X]  Step 8.6: Create the ``NetworkTransformComponent`` to flag entities for replication.
* [X]  Step 8.7: Implement Server serialization loops broadcasting replicated components.
* [X]  Step 8.8: Implement Client deserialization loop updating local proxy entities.
* [X]  Step 8.9: Implement Client-Side Prediction: decouple client input execution from server acknowledgment.
* [X]  Step 8.10: Implement Server Reconciliation: validate client states and correct mispredictions without snapping.

## Phase 9: Optimization & Polish (Weeks 19+)

* [X]  Step 9.1: Implement GPU-driven Frustum Culling via Compute Shaders generating indirect draw commands.
* [X]  Step 9.2: Implement Temporal Anti-Aliasing (TAA) pass.
* [X]  Step 9.3: Add Dynamic Resolution Scaling based on frame completion time metrics.
* [X]  Step 9.4: Optimize ECS cache lines and parallelize remaining Systems via the Job System.
* [X]  Step 9.5: Finalize asset cooking pipelines for production.

## Phase 10: Automatic Game Design & AI Agents (MCP) (Weeks 20-22)

* [X]  Step 10.1: Add lightweight HTTP/WebSocket dependency for internal server communication (e.g., `cpp-httplib` or `nlohmann_json`).
* [X]  Step 10.2: Implement the MCP (Model Context Protocol) Server base architecture to listen for local tool calls from external AI environments (like VS Code or custom UI).
* [X]  Step 10.3: Define JSON schemas for serializing the active `EnTT` Registry and core Game Objects (Transforms, Lights, Meshes).
* [X]  Step 10.4: Implement the `GetSceneContext` MCP tool capable of dumping the current world state, active cameras, and level layout in human/LLM-readable text format.
* [X]  Step 10.5: Implement the `SpawnEntity` MCP tool for AI agents to instantiate meshes, colliders, and lights programmatically via prompt execution.
* [X]  Step 10.6: Implement the `ModifyComponent` MCP tool allowing the AI to tweak lighting colors, physics mass, or translate/rotate objects based on semantic commands.
* [X]  Step 10.7: Implement the `ExecuteScript` MCP tool for injecting dynamic AI-authored Lua/C++ script snippets to define custom gameplay behaviors on the fly.
* [X]  Step 10.8: Integrate a sandboxed action-validation layer to ensure AI agents cannot crash the engine when generating invalid transforms or extreme array allocations.
* [X]  Step 10.9: Build an overarching "Auto-Level Designer" loop that translates abstract user prompts into sequence of MCP tool calls.

## Phase 11: Spatial Audio & Acoustic Environments (Weeks 23-24)

* [X]  Step 11.1: Add an audio backend dependency (e.g., `miniaudio`, `FMOD`, or `Wwise`).
* [X]  Step 11.2: Implement the `AudioListenerComponent` and bind its position/orientation to the active Camera.
* [X]  Step 11.3: Implement the `AudioSourceComponent` for playing 3D spatialized sound files.
* [X]  Step 11.4: Integrate the Audio System with Jolt Physics (Phase 6) to trigger sounds dynamically based on collision impulses and material types.
* [X]  Step 11.5: Implement basic Reverb Zones or Audio Volumes to simulate acoustic changes when entering different spaces.
* [X]  Step 11.6: **MCP Server Update:** Implement the `PlayAudio` and `ModifyAcoustics` MCP tools, allowing AI agents to dynamically trigger sound effects, change background tracks, and alter reverb parameters based on semantic logic.

## Phase 12: Advanced Skeletal Animation & Kinematics (Weeks 25-27)

* [X]  Step 12.1: Expand the mesh loader (Phase 4) to parse bone weights, indices, and skeletal hierarchies from GLTF files.
* [X]  Step 12.2: Implement GPU Skinning via Compute Shaders or Vertex Shader SSBOs to deform meshes based on bone matrices.
* [X]  Step 12.3: Implement an `AnimatorComponent` and a data-driven Animation State Machine (Idle, Walk, Run, Jump).
* [X]  Step 12.4: Implement Animation Blending to smoothly cross-fade between different animation states.
* [X]  Step 12.5: Implement basic Inverse Kinematics (IK) for procedural foot placement, querying the physics world to align feet with uneven terrain.
* [X]  Step 12.6: **MCP Server Update:** Implement the `SetAnimationState` and `SetIKTarget` MCP tools, enabling AI agents to programmatically drive character animations, script cutscenes, and direct NPC gaze or limb placement.

## Phase 13: World Building & Procedural Environments (Weeks 28-30)

* [X]  Step 13.1: Implement a chunk-based LOD Terrain System (using heightmaps or voxels) with asynchronous generation.
* [X]  Step 13.2: Implement a Foliage Scattering system utilizing compute-driven instanced rendering to efficiently draw thousands of trees/grass blades.
* [X]  Step 13.3: Implement a physically based Volumetric Skybox (Rayleigh/Mie scattering) and a dynamic Day/Night cycle updating directional lights.
* [X]  Step 13.4: **MCP Server Update:** Implement the `GenerateBiome` and `SetTimeOfDay` MCP tools. This allows the AI to procedurally sculpt terrain, scatter foliage based on high-level prompts, and manipulate the lighting environment dynamically.

## Phase 14: Particle Systems & Visual Effects (Weeks 31-33)

* [X]  Step 14.1: Implement a GPU-driven compute particle system to handle millions of particles without bottlenecking the CPU.
* [X]  Step 14.2: Create a `ParticleEmitterComponent` with adjustable parameters (emission rate, lifetime, velocity, color over time, size over time).
* [X]  Step 14.3: Implement specialized render passes for particle sorting and blending (Additive for fire/magic, Alpha Blend for smoke/dust).
* [X]  Step 14.4: Add support for texture atlases (sprite sheets) within the particle renderer to support animated VFX.
* [X]  Step 14.5: **MCP Server Update:** Implement the `SpawnParticleEffect` and `ModifyEmitter` MCP tools. This allows your AI agents to trigger explosions, spawn magic auras, or dynamically alter weather effects (like rain or snow) via text commands.

## Phase 15: Post-Processing & Cinematic Polish (Weeks 34-36)

* [X]  Step 15.1: Implement a ping-pong framebuffer architecture to chain multiple sequential post-processing passes together.
* [X]  Step 15.2: Implement physically based Bloom (using a multi-tap downsample/upsample blur technique) to make emissive materials and lights glow realistically.
* [X]  Step 15.3: Add Screen Space Ambient Occlusion (SSAO) to ground your geometry with realistic contact shadows.
* [X]  Step 15.4: Implement Depth of Field (DoF) and Motion Blur, linking their parameters directly to the active `CameraComponent`.
* [X]  Step 15.5: Implement a Color Grading pass using 3D Lookup Tables (LUTs) and ACES Tonemapping for professional cinematic color correction.
* [X]  Step 15.6: **MCP Server Update:** Implement the `SetPostProcessProfile` and `BlendCameraEffects` MCP tools. Your AI can now act as a cinematographer, instantly shifting the mood of the game (e.g., "make the scene look gloomy, desaturated, and heavily blurred") based on narrative context.

## Phase 16: In-Game UI Framework & State Management (Weeks 37-39)

* [X]  Step 16.1: Add a lightweight UI rendering library (like `RmlUi` or a custom Signed-Distance Field text renderer) to draw HUD elements independently of the 3D scene.
  * [X] Sub-step 16.1.1: Dear ImGui Vulkan Integration (v0.16.1.1)
  * [X] Sub-step 16.1.2: MSDF Font Atlas Pipeline (v0.16.1.2)
  * [X] Sub-step 16.1.3: MSDF Text Renderer (v0.16.1.3)
  * [X] Sub-step 16.1.4: Text Shaders (v0.16.1.4)
  * [X] Sub-step 16.1.5: UIManager Singleton (v0.16.1.5)
* [X]  Step 16.2: Implement the `UIComponent` to anchor 2D screens to the viewport or project 3D world-space widgets (like health bars floating above enemies).
  * [X] Sub-step 16.2.1: Anchor System (v0.16.2.1)
  * [X] Sub-step 16.2.2: UIComponent ECS Integration (v0.16.3.1)
  * [X] Sub-step 16.2.3: UISystem for World Widgets (v0.16.2.3)
  * [X] Sub-step 16.2.4: Base Widget Class (v0.16.2.4)
  * [X] Sub-step 16.2.5: HUD Widgets (HealthBar, ProgressBar, Label, Crosshair, etc.) (v0.16.2.5)
  * [X] Sub-step 16.2.6: World Widget Renderer (v0.16.2.6)
  * [X] Sub-step 16.2.7: Widget Batching System (v0.16.2.7)
* [X]  Step 16.3: Build a robust Save/Load state system, utilizing the JSON schemas you defined in Phase 10 to serialize the entire `EnTT` registry and player state to the disk.
  * [X] Sub-step 16.3.1: Extended Scene Serialization (v0.16.3.1)
  * [X] Sub-step 16.3.2: Save File Format (v0.16.3.2)
  * [X] Sub-step 16.3.3: SaveManager Implementation (v0.16.3.3)
  * [X] Sub-step 16.3.4: Async Save/Load (v0.16.3.4)
  * [X] Sub-step 16.3.5: Auto-Save System (v0.16.3.5)
  * [X] Sub-step 16.3.6: Player State Serialization (v0.16.3.6)
* [X]  Step 16.4: Implement asynchronous Scene Loading and transition management (building the architecture for loading screens and level streaming).
  * [X] Sub-step 16.4.1: SceneLoader Core (v0.16.4.1)
  * [X] Sub-step 16.4.2: Async Loading Pipeline (v0.16.4.2)
  * [X] Sub-step 16.4.3: TransitionManager (v0.16.4.3)
  * [X] Sub-step 16.4.4: Loading Screen Widget (v0.16.4.4)
  * [X] Sub-step 16.4.5: Level Streaming Infrastructure (v0.16.4.5)
  * [X] Sub-step 16.4.6: Scene Callbacks (v0.16.4.6)
* [X]  Step 16.5: **MCP Server Update:** Implement the `DisplayScreenMessage`, `UpdateHUD`, and `TriggerSaveState` MCP tools. The AI can now act as a dynamic Game Master—speaking directly to the player via on-screen text, updating objective trackers, and forcing save points before generating a boss encounter.
  * [X] Sub-step 16.5.1: MCPUITools Header (v0.16.5.1)
  * [X] Sub-step 16.5.2: DisplayScreenMessage Tool (v0.16.5.2)
  * [X] Sub-step 16.5.3: UpdateHUD Tool (v0.16.5.3)
  * [X] Sub-step 16.5.4: TriggerSaveState Tool (v0.16.5.4)
  * [X] Sub-step 16.5.5: ShowLoadingScreen Tool (v0.16.5.5)
  * [X] Sub-step 16.5.6: Tool Registration (v0.16.5.6)

## Phase 17: Navigation & AI Pathfinding (Weeks 40-42)

* [X]  Step 17.1: Add a navigation mesh dependency (like `RecastNavigation` / `Detour`) to generate walkable surfaces from your collision geometry.
  * [X] Sub-step 17.1.1: RecastNavigation vcpkg Integration (v0.17.1.1)
  * [X] Sub-step 17.1.2: NavMeshConfig & Types (v0.17.1.2)
  * [X] Sub-step 17.1.3: NavMeshBuilder Implementation (v0.17.1.3)
  * [X] Sub-step 17.1.4: NavMeshManager Singleton (v0.17.1.4)
  * [X] Sub-step 17.1.5: Tile-Based NavMesh Support (v0.17.1.5)
  * [X] Sub-step 17.1.6: NavMesh Debug Visualization (v0.17.1.6)
  * [X] Sub-step 17.1.7: NavMeshComponent ECS Integration (v0.17.1.7)
* [X]  Step 17.2: Implement dynamic NavMesh building to allow real-time "carving" when obstacles (like a spawned wall or destroyed bridge) alter the environment.
  * [X] Sub-step 17.2.1: TileCache Integration (v0.17.2.1)
  * [X] Sub-step 17.2.2: NavObstacleComponent (v0.17.2.2)
  * [X] Sub-step 17.2.3: Dynamic Obstacle Carving (v0.17.2.3)
  * [X] Sub-step 17.2.4: Dirty Tile Tracking (v0.17.2.4)
  * [X] Sub-step 17.2.5: Async Tile Rebuild (v0.17.2.5)
  * [X] Sub-step 17.2.6: Off-Mesh Connections (v0.17.2.6)
  * [X] Sub-step 17.2.7: Area Masking (v0.17.2.7)
* [X]  Step 17.3: Create the `NavAgentComponent` to handle A\* path calculation, smooth corner rounding, and basic steering behaviors.
  * [X] Sub-step 17.3.1: NavAgentComponent (v0.17.3.1)
  * [X] Sub-step 17.3.2: CrowdManager Core (v0.17.3.2)
  * [X] Sub-step 17.3.3: Path Request System (v0.17.3.3)
  * [X] Sub-step 17.3.4: Path Corridor & Smoothing (v0.17.3.4)
  * [X] Sub-step 17.3.5: Steering Behaviors (v0.17.3.5)
  * [X] Sub-step 17.3.6: CharacterController Integration (v0.17.3.6)
  * [X] Sub-step 17.3.7: NavigationSystem (v0.17.3.7)
* [X]  Step 17.4: Implement Crowd Simulation algorithms (like RVO / Reciprocal Velocity Obstacles) so dozens of NPCs can navigate without colliding into one another.
  * [X] Sub-step 17.4.1: RVO Integration via DetourCrowd (v0.17.4.1)
  * [X] Sub-step 17.4.2: Agent Priority System (v0.17.4.2)
  * [X] Sub-step 17.4.3: Separation Steering (v0.17.4.3)
  * [X] Sub-step 17.4.4: Parallel Crowd Update (v0.17.4.4)
  * [X] Sub-step 17.4.5: Velocity Obstacle Visualization (v0.17.4.5)
  * [X] Sub-step 17.4.6: Crowd Statistics (v0.17.4.6)
* [X]  Step 17.5: **MCP Server Update:** Implement the `RebuildNavMesh`, `CommandAgentMove`, and `SetAgentPatrolRoute` MCP tools. Your AI can now command hordes of enemies, dynamically generate mazes while ensuring they remain solvable, and orchestrate complex NPC movements.
  * [X] Sub-step 17.5.1: MCPNavigationTools Header (v0.17.5.1)
  * [X] Sub-step 17.5.2: RebuildNavMesh Tool (v0.17.5.2)
  * [X] Sub-step 17.5.3: CommandAgentMove Tool (v0.17.5.3)
  * [X] Sub-step 17.5.4: SetAgentPatrolRoute Tool (v0.17.5.4)
  * [X] Sub-step 17.5.5: QueryNavMesh & GetNavigationStats Tools (v0.17.5.5)
  * [X] Sub-step 17.5.6: Tool Registration in MCPAllTools (v0.17.5.6)

## Phase 18: Advanced Gameplay Systems & Logic (Weeks 43-45)

* [X]  Step 18.1: Implement a data-driven Behavior Tree or Finite State Machine (FSM) framework for complex, hierarchical NPC decision-making. (v0.18.1)
* [X]  Step 18.2: Implement a global Event/Message Bus pattern. This allows decoupled systems (like Health, UI, and Audio) to listen for generic events (e.g., `OnPlayerDamaged`) without hard dependencies. (v0.18.2)
* [X]  Step 18.3: Build a structured Quest and Inventory System, serializing item data and objective states alongside the player's profile. (v0.18.3)
* [X]  Step 18.4: Create a branching Dialogue Data structure, capable of triggering camera cuts and animations during conversations. (v0.18.4)
* [X]  Step 18.5: **MCP Server Update:** Implement the `InjectDialogueNode`, `UpdateQuestObjective`, and `ModifyInventory` MCP tools. The AI can now act as a dynamic storyteller—generating personalized quests on the fly, altering NPC dialogue based on player actions, and rewarding players with procedurally generated loot. (v0.18.5)

## Phase 19: Hardware Ray Tracing & Modern Illumination (Weeks 46-49)

* [X]  Step 19.1: Expand your Render Hardware Interface (RHI) to support Vulkan Ray Tracing or DX12 DXR. (v0.19.1)
* [X]  Step 19.2: Build the Acceleration Structures (Bottom-Level BLAS for meshes, Top-Level TLAS for the scene) and update them seamlessly as entities move. (v0.19.2)
* [X]  Step 19.3: Implement Ray-Traced Hard/Soft Shadows and Ray-Traced Reflections, offering a high-end alternative to Phase 4's shadow maps and standard screen-space reflections. (v0.19.3)
* [X]  Step 19.4: Implement a Global Illumination (GI) solution (either Voxel-based or Screen-Space) to simulate light bouncing realistically in indoor environments. (v0.19.4)
* [X]  Step 19.5: **MCP Server Update:** Implement the `ToggleRayTracingFeatures` and `BakeGlobalIllumination` MCP tools. The AI can dynamically scale the engine's graphical fidelity or adjust GI bounce intensity to instantly transform a bright, sunny room into a moody, terrifying dungeon. (v0.19.5)

## Phase 20: Advanced Physics & Destruction (Weeks 50-52)

* [ ]  Step 20.1: Extend your Phase 6 Jolt Physics integration to support complex constraints (Hinges, Sliders, Springs) for vehicles, doors, and machinery.
* [ ]  Step 20.2: Implement a Ragdoll generation system, translating the `AnimatorComponent` skeletal hierarchy into a network of rigidbodies and constraints upon entity death.
* [ ]  Step 20.3: Implement a Destructible Mesh pipeline using Voronoi fracturing, allowing static meshes to dynamically shatter into physics-enabled debris based on impact force.
* [ ]  Step 20.4: Integrate Cloth or Soft Body simulation for capes, flags, and vegetation wind interaction.
* [ ]  Step 20.5: **MCP Server Update:** Implement the `TriggerDestruction`, `SpawnRagdoll`, and `ModifyConstraint` MCP tools. The AI can now blow up level geometry in front of the player, unlock doors by snapping virtual hinges, or dynamically drop the player into an active physics simulation.
