# Implementation Roadmap: Custom Engine

This roadmap breaks down the engine development into strict, single-action steps to ensure continuous progress without overwhelming complexity at any single point.

## Phase 1: Core Foundation & Tooling (Weeks 1-2)

* [x] Step 1.1: Initialize Git repository and ``.gitignore`` for C++/CMake.
* [x] Step 1.2: Set up the root ``CMakeLists.txt`` with standard C++20 compiler flags.
* [x] Step 1.3: Integrate ``vcpkg`` as a git submodule and set up the manifest file (``vcpkg.json``).
* [x] Step 1.4: Add ``spdlog`` dependency and implement the engine ``Log`` static class.
* [x] Step 1.5: Create the ``Core/Memory/Allocator.h`` interface class.
* [x] Step 1.6: Implement a Linear Allocator for fast, per-frame scratch memory.
* [x] Step 1.7: Implement a Pool Allocator for fixed-size object allocations.
* [x] Step 1.8: Add ``glm`` dependency and create engine-specific math alias typedfs.
* [x] Step 1.9: Implement the multi-threaded Job System using standard C++ threads and concurrent queues.
* [x] Step 1.10: Add ``Tracy`` profiler dependency and implement profiling macros across existing systems.
* [x] Step 1.11: Implement the Assertion and Crash-handling system.

## Phase 2: Platform & Windowing (Week 3)

* [x] Step 2.1: Add ``SDL3`` dependency.
* [x] Step 2.2: Implement ``Application`` singleton to manage the main game loop.
* [x] Step 2.3: Implement ``Window`` class wrapping SDL3 window creation.
* [x] Step 2.4: Implement ``EventSystem`` utilizing a dispatcher pattern mapped to SDL events.
* [x] Step 2.5: Implement ``InputSystem`` for capturing keyboard, mouse, and gamepad states.
* [x] Step 2.6: Bind window close and input events to the ``Application`` loop to allow safe shutdown.

## Phase 3: Render Hardware Interface (RHI) - Base & Vulkan Setup (Weeks 4-6)

* [x] Step 3.1: Define abstract base RHI classes: ``RHIDevice``, ``RHICommandList``, ``RHIBuffer``, ``RHITexture``.
* [x] Step 3.2: Add ``Vulkan SDK`` dependency.
* [x] Step 3.3: Implement Vulkan Instance creation with Validation Layers enabled in Debug builds.
* [x] Step 3.4: Implement Vulkan Physical Device selection (preferring discrete GPUs, supporting iGPUs).
* [x] Step 3.5: Implement Vulkan Logical Device and Queue retrieval.
* [x] Step 3.6: Integrate Vulkan Memory Allocator (VMA).
* [x] Step 3.7: Implement Vulkan Swapchain creation and recreation logic on window resize.
* [x] Step 3.8: Implement Vulkan Command Pool and Command Buffer allocation.
* [x] Step 3.9: Implement Vulkan Synchronization objects (Semaphores, Fences).
* [x] Step 3.10: Integrate ``glslang`` or ``DXC`` to compile GLSL/HLSL to SPIR-V dynamically.
* [x] Step 3.11: Implement Shader Module creation.
* [x] Step 3.12: Implement Graphics Pipeline State Object (PSO) creation logic.
* [x] Step 3.13: Get a colored triangle rendering on screen using raw RHI calls.

## Phase 4: High-Level Renderer & Forward+ Architecture (Weeks 7-9)

* [x] Step 4.1: Implement generalized Buffer abstraction (Vertex, Index, Uniform, Storage).
* [x] Step 4.2: Implement generalized Texture abstraction (2D, 3D, Cubemap) and Sampler objects.
* [x] Step 4.3: Build an asynchronous texture/mesh uploader using a staging buffer and transfer queue.
* [x] Step 4.4: Implement a primitive specific Mesh loader (assimp/cgltf) to load GLTF files.
* [x] Step 4.5: Implement the fundamental PBR shader (Albedo, Normal, Metallic, Roughness).
* [x] Step 4.6: Implement Render Passes structure grouping command buffer submissions.
* [x] Step 4.7: Implement the Z-Prepass render pass.
* [x] Step 4.8: Implement the Compute Shader for Frustum Cluster generation.
* [x] Step 4.9: Implement the Compute Shader for Light bounding sphere to Cluster intersection testing.
* [x] Step 4.10: Create GPU buffers for Light Index Lists and Grid definitions.
* [x] Step 4.11: Update the PBR shader to iterate over lights within the current fragment's cluster.
* [x] Step 4.12: Implement simple shadow mapping (Directional Light).

## Phase 5: Entity Component System & Gameplay Rules (Weeks 10-11)

* [x] Step 5.1: Add ``EnTT`` dependency.
* [x] Step 5.2: Create the ``Scene`` class to wrap the EnTT registry.
* [x] Step 5.3: Implement basic components: ``TransformComponent``, ``MeshComponent``, ``LightComponent``.
* [x] Step 5.4: Implement the Render System: iterates over Transform and Mesh components to submit draw calls to the Renderer.
* [x] Step 5.5: Implement the Light System: iterates over Light components to update RHI light buffers.
* [x] Step 5.6: Implement parent-child Transform hierarchy and Local-to-World matrix recalculation system.

## Phase 6: Physics Integration (Weeks 12-13)

* [x] Step 6.1: Add ``Jolt Physics`` dependency.
* [x] Step 6.2: Initialize the Jolt Physics System and configure BroadPhase layers.
* [x] Step 6.3: Implement Jolt JobSystem integration with the engine's multi-threaded Job System.
* [x] Step 6.4: Create physics components: ``RigidBodyComponent``, ``ColliderComponent`` (Box, Sphere, Capsule, Mesh).
* [x] Step 6.5: Implement Physics System: step simulation based on Delta Time.
* [x] Step 6.6: Implement Physics-to-Transform Sync System (updating ECS transforms from Jolt results).

## Phase 7: Switchable Camera System (Week 14)

* [x] Step 7.1: Implement ``CameraComponent`` (FOV, Near/Far planes, Projection Matrix calculation).
* [x] Step 7.2: Create the Character Controller System interfacing with Jolt Physics for player movement.
* [x] Step 7.3: Implement input mapping to velocity and rotation.
* [x] Step 7.4: Implement First-Person View logic: Attach camera coordinate space to the player entity.
* [x] Step 7.5: Implement Third-Person View logic: Implement a Spring-Arm constraint utilizing Raycasts against the physics world.
* [x] Step 7.6: Implement the interpolator state machine to smoothly pan and zoom between FP and TP view modes upon user input.

## Phase 8: Multi-Player Networking (Weeks 15-18)

* [x] Step 8.1: Add ``GameNetworkingSockets`` dependency.
* [x] Step 8.2: Implement ``NetworkManager`` to initialize sockets.
* [x] Step 8.3: Implement Server listen socket bindings.
* [x] Step 8.4: Implement Client connection connection requests and handshake.
* [x] Step 8.5: Define network packet schemas using fixed-size structs or FlatBuffers.
* [x] Step 8.6: Create the ``NetworkTransformComponent`` to flag entities for replication.
* [x] Step 8.7: Implement Server serialization loops broadcasting replicated components.
* [x] Step 8.8: Implement Client deserialization loop updating local proxy entities.
* [x] Step 8.9: Implement Client-Side Prediction: decouple client input execution from server acknowledgment.
* [x] Step 8.10: Implement Server Reconciliation: validate client states and correct mispredictions without snapping.

## Phase 9: Optimization & Polish (Weeks 19+)

* [x] Step 9.1: Implement GPU-driven Frustum Culling via Compute Shaders generating indirect draw commands.
* [x] Step 9.2: Implement Temporal Anti-Aliasing (TAA) pass.
* [x] Step 9.3: Add Dynamic Resolution Scaling based on frame completion time metrics.
* [x] Step 9.4: Optimize ECS cache lines and parallelize remaining Systems via the Job System.
* [x] Step 9.5: Finalize asset cooking pipelines for production.

## Phase 10: Automatic Game Design & AI Agents (MCP) (Weeks 20-22)

* [x] Step 10.1: Add lightweight HTTP/WebSocket dependency for internal server communication (e.g., `cpp-httplib` or `nlohmann_json`).
* [x] Step 10.2: Implement the MCP (Model Context Protocol) Server base architecture to listen for local tool calls from external AI environments (like VS Code or custom UI).
* [x] Step 10.3: Define JSON schemas for serializing the active `EnTT` Registry and core Game Objects (Transforms, Lights, Meshes).
* [x] Step 10.4: Implement the `GetSceneContext` MCP tool capable of dumping the current world state, active cameras, and level layout in human/LLM-readable text format.
* [x] Step 10.5: Implement the `SpawnEntity` MCP tool for AI agents to instantiate meshes, colliders, and lights programmatically via prompt execution.
* [x] Step 10.6: Implement the `ModifyComponent` MCP tool allowing the AI to tweak lighting colors, physics mass, or translate/rotate objects based on semantic commands.
* [x] Step 10.7: Implement the `ExecuteScript` MCP tool for injecting dynamic AI-authored Lua/C++ script snippets to define custom gameplay behaviors on the fly.
* [x] Step 10.8: Integrate a sandboxed action-validation layer to ensure AI agents cannot crash the engine when generating invalid transforms or extreme array allocations.
* [x] Step 10.9: Build an overarching "Auto-Level Designer" loop that translates abstract user prompts into sequence of MCP tool calls.

## Phase 11: Spatial Audio & Acoustic Environments (Weeks 23-24)

* [x] Step 11.1: Add an audio backend dependency (e.g., `miniaudio`, `FMOD`, or `Wwise`).
* [x] Step 11.2: Implement the `AudioListenerComponent` and bind its position/orientation to the active Camera.
* [x] Step 11.3: Implement the `AudioSourceComponent` for playing 3D spatialized sound files.
* [x] Step 11.4: Integrate the Audio System with Jolt Physics (Phase 6) to trigger sounds dynamically based on collision impulses and material types.
* [x] Step 11.5: Implement basic Reverb Zones or Audio Volumes to simulate acoustic changes when entering different spaces.
* [x] Step 11.6: **MCP Server Update:** Implement the `PlayAudio` and `ModifyAcoustics` MCP tools, allowing AI agents to dynamically trigger sound effects, change background tracks, and alter reverb parameters based on semantic logic.

## Phase 12: Advanced Skeletal Animation & Kinematics (Weeks 25-27)

* [x] Step 12.1: Expand the mesh loader (Phase 4) to parse bone weights, indices, and skeletal hierarchies from GLTF files.
* [x] Step 12.2: Implement GPU Skinning via Compute Shaders or Vertex Shader SSBOs to deform meshes based on bone matrices.
* [x] Step 12.3: Implement an `AnimatorComponent` and a data-driven Animation State Machine (Idle, Walk, Run, Jump).
* [x] Step 12.4: Implement Animation Blending to smoothly cross-fade between different animation states.
* [x] Step 12.5: Implement basic Inverse Kinematics (IK) for procedural foot placement, querying the physics world to align feet with uneven terrain.
* [x] Step 12.6: **MCP Server Update:** Implement the `SetAnimationState` and `SetIKTarget` MCP tools, enabling AI agents to programmatically drive character animations, script cutscenes, and direct NPC gaze or limb placement.

## Phase 13: World Building & Procedural Environments (Weeks 28-30)

* [x] Step 13.1: Implement a chunk-based LOD Terrain System (using heightmaps or voxels) with asynchronous generation.
* [x] Step 13.2: Implement a Foliage Scattering system utilizing compute-driven instanced rendering to efficiently draw thousands of trees/grass blades.
* [x] Step 13.3: Implement a physically based Volumetric Skybox (Rayleigh/Mie scattering) and a dynamic Day/Night cycle updating directional lights.
* [x] Step 13.4: **MCP Server Update:** Implement the `GenerateBiome` and `SetTimeOfDay` MCP tools. This allows the AI to procedurally sculpt terrain, scatter foliage based on high-level prompts, and manipulate the lighting environment dynamically.

## Phase 14: Particle Systems & Visual Effects (Weeks 31-33)

* [x] Step 14.1: Implement a GPU-driven compute particle system to handle millions of particles without bottlenecking the CPU.
* [x] Step 14.2: Create a `ParticleEmitterComponent` with adjustable parameters (emission rate, lifetime, velocity, color over time, size over time).
* [x] Step 14.3: Implement specialized render passes for particle sorting and blending (Additive for fire/magic, Alpha Blend for smoke/dust).
* [x] Step 14.4: Add support for texture atlases (sprite sheets) within the particle renderer to support animated VFX.
* [x] Step 14.5: **MCP Server Update:** Implement the `SpawnParticleEffect` and `ModifyEmitter` MCP tools. This allows your AI agents to trigger explosions, spawn magic auras, or dynamically alter weather effects (like rain or snow) via text commands.

## Phase 15: Post-Processing & Cinematic Polish (Weeks 34-36)

* [x] Step 15.1: Implement a ping-pong framebuffer architecture to chain multiple sequential post-processing passes together.
* [x] Step 15.2: Implement physically based Bloom (using a multi-tap downsample/upsample blur technique) to make emissive materials and lights glow realistically.
* [x] Step 15.3: Add Screen Space Ambient Occlusion (SSAO) to ground your geometry with realistic contact shadows.
* [x] Step 15.4: Implement Depth of Field (DoF) and Motion Blur, linking their parameters directly to the active `CameraComponent`.
* [x] Step 15.5: Implement a Color Grading pass using 3D Lookup Tables (LUTs) and ACES Tonemapping for professional cinematic color correction.
* [x] Step 15.6: **MCP Server Update:** Implement the `SetPostProcessProfile` and `BlendCameraEffects` MCP tools. Your AI can now act as a cinematographer, instantly shifting the mood of the game (e.g., "make the scene look gloomy, desaturated, and heavily blurred") based on narrative context.

## Phase 16: In-Game UI Framework & State Management (Weeks 37-39)

* [x] Step 16.1: Add a lightweight UI rendering library (like `RmlUi` or a custom Signed-Distance Field text renderer) to draw HUD elements independently of the 3D scene.
  * [x] Sub-step 16.1.1: Dear ImGui Vulkan Integration (v0.16.1.1)
  * [x] Sub-step 16.1.2: MSDF Font Atlas Pipeline (v0.16.1.2)
  * [x] Sub-step 16.1.3: MSDF Text Renderer (v0.16.1.3)
  * [x] Sub-step 16.1.4: Text Shaders (v0.16.1.4)
  * [x] Sub-step 16.1.5: UIManager Singleton (v0.16.1.5)
* [x] Step 16.2: Implement the `UIComponent` to anchor 2D screens to the viewport or project 3D world-space widgets (like health bars floating above enemies).
  * [x] Sub-step 16.2.1: Anchor System (v0.16.2.1)
  * [x] Sub-step 16.2.2: UIComponent ECS Integration (v0.16.3.1)
  * [x] Sub-step 16.2.3: UISystem for World Widgets (v0.16.2.3)
  * [x] Sub-step 16.2.4: Base Widget Class (v0.16.2.4)
  * [x] Sub-step 16.2.5: HUD Widgets (HealthBar, ProgressBar, Label, Crosshair, etc.) (v0.16.2.5)
  * [x] Sub-step 16.2.6: World Widget Renderer (v0.16.2.6)
  * [x] Sub-step 16.2.7: Widget Batching System (v0.16.2.7)
* [x] Step 16.3: Build a robust Save/Load state system, utilizing the JSON schemas you defined in Phase 10 to serialize the entire `EnTT` registry and player state to the disk.
  * [x] Sub-step 16.3.1: Extended Scene Serialization (v0.16.3.1)
  * [x] Sub-step 16.3.2: Save File Format (v0.16.3.2)
  * [x] Sub-step 16.3.3: SaveManager Implementation (v0.16.3.3)
  * [x] Sub-step 16.3.4: Async Save/Load (v0.16.3.4)
  * [x] Sub-step 16.3.5: Auto-Save System (v0.16.3.5)
  * [x] Sub-step 16.3.6: Player State Serialization (v0.16.3.6)
* [x] Step 16.4: Implement asynchronous Scene Loading and transition management (building the architecture for loading screens and level streaming).
  * [x] Sub-step 16.4.1: SceneLoader Core (v0.16.4.1)
  * [x] Sub-step 16.4.2: Async Loading Pipeline (v0.16.4.2)
  * [x] Sub-step 16.4.3: TransitionManager (v0.16.4.3)
  * [x] Sub-step 16.4.4: Loading Screen Widget (v0.16.4.4)
  * [x] Sub-step 16.4.5: Level Streaming Infrastructure (v0.16.4.5)
  * [x] Sub-step 16.4.6: Scene Callbacks (v0.16.4.6)
* [x] Step 16.5: **MCP Server Update:** Implement the `DisplayScreenMessage`, `UpdateHUD`, and `TriggerSaveState` MCP tools. The AI can now act as a dynamic Game Master—speaking directly to the player via on-screen text, updating objective trackers, and forcing save points before generating a boss encounter.
  * [x] Sub-step 16.5.1: MCPUITools Header (v0.16.5.1)
  * [x] Sub-step 16.5.2: DisplayScreenMessage Tool (v0.16.5.2)
  * [x] Sub-step 16.5.3: UpdateHUD Tool (v0.16.5.3)
  * [x] Sub-step 16.5.4: TriggerSaveState Tool (v0.16.5.4)
  * [x] Sub-step 16.5.5: ShowLoadingScreen Tool (v0.16.5.5)
  * [x] Sub-step 16.5.6: Tool Registration (v0.16.5.6)

## Phase 17: Navigation & AI Pathfinding (Weeks 40-42)

* [x] Step 17.1: Add a navigation mesh dependency (like `RecastNavigation` / `Detour`) to generate walkable surfaces from your collision geometry.
  * [x] Sub-step 17.1.1: RecastNavigation vcpkg Integration (v0.17.1.1)
  * [x] Sub-step 17.1.2: NavMeshConfig & Types (v0.17.1.2)
  * [x] Sub-step 17.1.3: NavMeshBuilder Implementation (v0.17.1.3)
  * [x] Sub-step 17.1.4: NavMeshManager Singleton (v0.17.1.4)
  * [x] Sub-step 17.1.5: Tile-Based NavMesh Support (v0.17.1.5)
  * [x] Sub-step 17.1.6: NavMesh Debug Visualization (v0.17.1.6)
  * [x] Sub-step 17.1.7: NavMeshComponent ECS Integration (v0.17.1.7)
* [x] Step 17.2: Implement dynamic NavMesh building to allow real-time "carving" when obstacles (like a spawned wall or destroyed bridge) alter the environment.
  * [x] Sub-step 17.2.1: TileCache Integration (v0.17.2.1)
  * [x] Sub-step 17.2.2: NavObstacleComponent (v0.17.2.2)
  * [x] Sub-step 17.2.3: Dynamic Obstacle Carving (v0.17.2.3)
  * [x] Sub-step 17.2.4: Dirty Tile Tracking (v0.17.2.4)
  * [x] Sub-step 17.2.5: Async Tile Rebuild (v0.17.2.5)
  * [x] Sub-step 17.2.6: Off-Mesh Connections (v0.17.2.6)
  * [x] Sub-step 17.2.7: Area Masking (v0.17.2.7)
* [x] Step 17.3: Create the `NavAgentComponent` to handle A\* path calculation, smooth corner rounding, and basic steering behaviors.
  * [x] Sub-step 17.3.1: NavAgentComponent (v0.17.3.1)
  * [x] Sub-step 17.3.2: CrowdManager Core (v0.17.3.2)
  * [x] Sub-step 17.3.3: Path Request System (v0.17.3.3)
  * [x] Sub-step 17.3.4: Path Corridor & Smoothing (v0.17.3.4)
  * [x] Sub-step 17.3.5: Steering Behaviors (v0.17.3.5)
  * [x] Sub-step 17.3.6: CharacterController Integration (v0.17.3.6)
  * [x] Sub-step 17.3.7: NavigationSystem (v0.17.3.7)
* [x] Step 17.4: Implement Crowd Simulation algorithms (like RVO / Reciprocal Velocity Obstacles) so dozens of NPCs can navigate without colliding into one another.
  * [x] Sub-step 17.4.1: RVO Integration via DetourCrowd (v0.17.4.1)
  * [x] Sub-step 17.4.2: Agent Priority System (v0.17.4.2)
  * [x] Sub-step 17.4.3: Separation Steering (v0.17.4.3)
  * [x] Sub-step 17.4.4: Parallel Crowd Update (v0.17.4.4)
  * [x] Sub-step 17.4.5: Velocity Obstacle Visualization (v0.17.4.5)
  * [x] Sub-step 17.4.6: Crowd Statistics (v0.17.4.6)
* [x] Step 17.5: **MCP Server Update:** Implement the `RebuildNavMesh`, `CommandAgentMove`, and `SetAgentPatrolRoute` MCP tools. Your AI can now command hordes of enemies, dynamically generate mazes while ensuring they remain solvable, and orchestrate complex NPC movements.
  * [x] Sub-step 17.5.1: MCPNavigationTools Header (v0.17.5.1)
  * [x] Sub-step 17.5.2: RebuildNavMesh Tool (v0.17.5.2)
  * [x] Sub-step 17.5.3: CommandAgentMove Tool (v0.17.5.3)
  * [x] Sub-step 17.5.4: SetAgentPatrolRoute Tool (v0.17.5.4)
  * [x] Sub-step 17.5.5: QueryNavMesh & GetNavigationStats Tools (v0.17.5.5)
  * [x] Sub-step 17.5.6: Tool Registration in MCPAllTools (v0.17.5.6)

## Phase 18: Advanced Gameplay Systems & Logic (Weeks 43-45)

* [x] Step 18.1: Implement a data-driven Behavior Tree or Finite State Machine (FSM) framework for complex, hierarchical NPC decision-making. (v0.18.1)
* [x] Step 18.2: Implement a global Event/Message Bus pattern. This allows decoupled systems (like Health, UI, and Audio) to listen for generic events (e.g., `OnPlayerDamaged`) without hard dependencies. (v0.18.2)
* [x] Step 18.3: Build a structured Quest and Inventory System, serializing item data and objective states alongside the player's profile. (v0.18.3)
* [x] Step 18.4: Create a branching Dialogue Data structure, capable of triggering camera cuts and animations during conversations. (v0.18.4)
* [x] Step 18.5: **MCP Server Update:** Implement the `InjectDialogueNode`, `UpdateQuestObjective`, and `ModifyInventory` MCP tools. The AI can now act as a dynamic storyteller—generating personalized quests on the fly, altering NPC dialogue based on player actions, and rewarding players with procedurally generated loot. (v0.18.5)

## Phase 19: Hardware Ray Tracing & Modern Illumination (Weeks 46-49)

* [x] Step 19.1: Expand your Render Hardware Interface (RHI) to support Vulkan Ray Tracing or DX12 DXR. (v0.19.1)
* [x] Step 19.2: Build the Acceleration Structures (Bottom-Level BLAS for meshes, Top-Level TLAS for the scene) and update them seamlessly as entities move. (v0.19.2)
* [x] Step 19.3: Implement Ray-Traced Hard/Soft Shadows and Ray-Traced Reflections, offering a high-end alternative to Phase 4's shadow maps and standard screen-space reflections. (v0.19.3)
* [x] Step 19.4: Implement a Global Illumination (GI) solution (either Voxel-based or Screen-Space) to simulate light bouncing realistically in indoor environments. (v0.19.4)
* [x] Step 19.5: **MCP Server Update:** Implement the `ToggleRayTracingFeatures` and `BakeGlobalIllumination` MCP tools. The AI can dynamically scale the engine's graphical fidelity or adjust GI bounce intensity to instantly transform a bright, sunny room into a moody, terrifying dungeon. (v0.19.5)

## Phase 20: Advanced Physics & Destruction (Weeks 50-52)

* [x] Step 20.1: Extend your Phase 6 Jolt Physics integration to support complex constraints (Hinges, Sliders, Springs) for vehicles, doors, and machinery. (v0.20.1)
* [x] Step 20.2: Implement a Ragdoll generation system, translating the `AnimatorComponent` skeletal hierarchy into a network of rigidbodies and constraints upon entity death. (v0.20.2)
* [x] Step 20.3: Implement a Destructible Mesh pipeline using Voronoi fracturing, allowing static meshes to dynamically shatter into physics-enabled debris based on impact force. (v0.20.3)
* [x] Step 20.4: Integrate Cloth or Soft Body simulation for capes, flags, and vegetation wind interaction. (v0.20.4)
* [x] Step 20.5: **MCP Server Update:** Implement the `TriggerDestruction`, `SpawnRagdoll`, and `ModifyConstraint` MCP tools. The AI can now blow up level geometry in front of the player, unlock doors by snapping virtual hinges, or dynamically drop the player into an active physics simulation. (v0.20.5)

## Phase 21: Editor Foundations, Prefab Workflow & Visual Authoring (Weeks 53-56)

* [x] Step 21.1: Build the core editor panel framework for hierarchy, inspector, and entity editing interactions. (v0.21.1.4)
  * [x] Sub-step 21.1.1: Implement `OpenSceneHierarchyPanel()` with scene tree virtualization, search, and selection sync. (v0.21.1.1)
  * [x] Sub-step 21.1.2: Implement `OpenInspectorPanel()` with dynamic component reflection and category grouping. (v0.21.1.2)
  * [x] Sub-step 21.1.3: Implement `SelectEntityInEditor()` to synchronize world selection, hierarchy focus, and gizmo targets. (v0.21.1.3)
  * [x] Sub-step 21.1.4: Implement `EditComponentPropertiesInEditor()` with validation, dirty state tracking, and undo integration points. (v0.21.1.4)
* [x] Step 21.2: Add transactional editor history for deterministic undo/redo behavior across entity and component edits. (v0.21.2.2)
  * [x] Sub-step 21.2.1: Implement `UndoEditorAction()` with grouped command stack playback. (v0.21.2.1)
  * [x] Sub-step 21.2.2: Implement `RedoEditorAction()` with divergence handling after branch edits. (v0.21.2.2)
* [x] Step 21.3: Add prefab authoring and instance management pipeline. (v0.21.3.4)
  * [x] Sub-step 21.3.1: Implement `CreatePrefabAsset()` for serializing selected entity graphs into reusable assets. (v0.21.3.1)
  * [x] Sub-step 21.3.2: Implement `InstantiatePrefabAsset()` for scene placement with stable instance IDs. (v0.21.3.2)
  * [x] Sub-step 21.3.3: Implement `ApplyPrefabOverrides()` for writing instance deltas back to source prefab assets. (v0.21.3.3)
  * [x] Sub-step 21.3.4: Implement `CreatePrefabVariant()` for inheritance-based prefab specialization. (v0.21.3.4)
* [x] Step 21.4: Build graph-based non-code authoring for gameplay and sequencing. (v0.21.4.3)
  * [x] Sub-step 21.4.1: Implement `OpenVisualScriptingGraphEditor()` for node graph authoring and serialization. (v0.21.4.1)
  * [x] Sub-step 21.4.2: Implement `CompileVisualScriptingGraph()` into runtime-executable intermediate representation. (v0.21.4.2)
  * [x] Sub-step 21.4.3: Implement `ExecuteVisualScriptingGraph()` with deterministic frame stepping and event hooks. (v0.21.4.3)
* [x] Step 21.5: Add cinematic timeline authoring in-editor. (v0.21.5.5)
  * [x] Sub-step 21.5.1: Implement `OpenCinematicSequencerEditor()` with track view, clip lanes, and playhead controls. (v0.21.5.1)
  * [x] Sub-step 21.5.2: Implement `AddTimelineTrack()` for camera, animation, audio, and event channels. (v0.21.5.2)
  * [x] Sub-step 21.5.3: Implement `AddTimelineClip()` with blend and easing metadata. (v0.21.5.3)
  * [x] Sub-step 21.5.4: Implement `EvaluateTimelineAtTime()` for scrub-time preview and runtime playback parity. (v0.21.5.4)
  * [x] Sub-step 21.5.5: Implement `RecordAnimationTake()` for live capture into timeline clips. (v0.21.5.5)

## Phase 22: Scene Serialization, Streaming & Open-World Runtime (Weeks 57-59)

* [x] Step 22.1: Build additive scene streaming runtime and lifecycle hooks.
  * [x] Sub-step 22.1.1: Implement `LoadSceneAdditiveAsync()` with staged asset prefetch and non-blocking activation. (v0.22.1.1)
  * [x] Sub-step 22.1.2: Implement `UnloadSceneAsync()` with dependency-aware release and safe teardown callbacks. (v0.22.1.2)
* [x] Step 22.2: Extend scene persistence beyond current context dump helpers.
  * [x] Sub-step 22.2.1: Implement `SerializeSceneToAsset()` for full scene-asset authoring from runtime/editor state. (v0.22.2.1)
  * [x] Sub-step 22.2.2: Implement `DeserializeSceneFromAsset()` for complete reconstruction of entities, components, and references. (v0.22.2.2)
* [x] Step 22.3: Add large-world partitioning and streaming controls.
  * [x] Sub-step 22.3.1: Implement `StreamWorldPartitionCellIn()` with cell-level dependency graph hydration. (v0.22.3.1)
  * [x] Sub-step 22.3.2: Implement `StreamWorldPartitionCellOut()` with ownership transfer and memory budget enforcement. (v0.22.3.2)
  * [x] Sub-step 22.3.3: Implement `BuildHierarchicalLOD()` for streaming-friendly distant representation generation. (v0.22.3.3)
  * [x] Sub-step 22.3.4: Implement `RebaseWorldOrigin()` with physics/network-safe coordinate remapping. (v0.22.3.4)

## Phase 23: Addressable Assets, Bundles & Hot Content Delivery (Weeks 60-62)

* [x] Step 23.1: Build addressable catalog generation and runtime query system.
  * [x] Sub-step 23.1.1: Implement `BuildAddressablesCatalog()` with deterministic GUID/address mapping and build manifest output. (v0.23.1.1)
  * [x] Sub-step 23.1.2: Implement `LoadAddressableAssetAsync()` with dependency resolution, caching, and cancellation. (v0.23.1.2)
  * [x] Sub-step 23.1.3: Implement `ReleaseAddressableAsset()` with ref-counted lifecycle and eviction policy. (v0.23.1.3)
* [x] Step 23.2: Build bundle packaging and mount pipeline for DLC/mod/patch workflows.
  * [x] Sub-step 23.2.1: Implement `BuildAssetBundle()` with platform-specific compression and chunk layout strategies. (v0.23.2.1)
  * [x] Sub-step 23.2.2: Implement `MountAssetBundle()` with integrity validation and mount priority controls. (v0.23.2.2)
  * [x] Sub-step 23.2.3: Implement `PatchAssetBundleDelta()` for binary-diff deployment without full rebundles. (v0.23.2.3)
* [x] Step 23.3: Add dependency intelligence and runtime hot-reload tooling.
  * [x] Sub-step 23.3.1: Implement `TrackAssetDependencyGraph()` with reverse-lookups and cycle detection. (v0.23.3.1)
  * [x] Sub-step 23.3.2: Implement `HotReloadAssetAtRuntime()` with safe rebinding of renderer/audio/animation users. (v0.23.3.2)

## Phase 24: Render Graph, Virtualized Geometry & Advanced Upscaling (Weeks 63-67)

* [x] Step 24.1: Move pass scheduling to an explicit render graph architecture.
  * [x] Sub-step 24.1.1: Implement `RegisterRenderGraphPass()` for declarative pass resources, barriers, and dependencies. (v0.24.1.1)
  * [x] Sub-step 24.1.2: Implement `ExecuteRenderGraph()` with topological scheduling and transient resource aliasing. (v0.24.1.2)
  * [x] Sub-step 24.1.3: Graph validation tooling + diagnostics export for scheduling/resource hazard visibility. (v0.24.1.3)
* [x] Step 24.2: Build virtualized geometry streaming path (Nanite-class capability target).
  * [x] Sub-step 24.2.1: Implement `BuildVirtualizedGeometryClusters()` from imported mesh topology and material partitions. (v0.24.2.1)
  * [x] Sub-step 24.2.2: Implement `StreamVirtualGeometryPages()` with camera-driven residency management. (v0.24.2.2)
  * [x] Sub-step 24.2.3: Residency telemetry + failure recovery for virtual geometry streaming. (v0.24.2.3)
* [x] Step 24.3: Add modern shadow/GI and hardware RT fallback hierarchy.
  * [x] Sub-step 24.3.1: Implement `BuildVirtualShadowMapCache()` for high-density shadow page reuse. (v0.24.3.1)
  * [x] Sub-step 24.3.2: Implement `ComputeDynamicGlobalIllumination()` with hybrid SSGI/RT pathways. (v0.24.3.2)
  * [x] Sub-step 24.3.3: Implement `TraceHardwareReflections()` with denoise and fallback blending. (v0.24.3.3)
  * [x] Sub-step 24.3.4: Implement `TraceHardwareShadows()` with per-light quality tiers. (v0.24.3.4)
  * [x] Sub-step 24.3.5: Unified hybrid lighting policy governor with deterministic downgrade order. (v0.24.3.5)
* [x] Step 24.4: Harden shader compilation and runtime warmup behavior.
  * [x] Sub-step 24.4.1: Implement `CompileShaderPermutationLibrary()` for platform/material feature matrices. (v0.24.4.1)
  * [x] Sub-step 24.4.2: Implement `WarmupPipelineCache()` to reduce runtime hitching. (v0.24.4.2)
  * [x] Sub-step 24.4.3: Pipeline cache persistence + invalidation policy tied to device/driver/shader digest. (v0.24.4.3)
* [x] Step 24.5: Add modern temporal upscaling + frame generation controls.
  * [x] Sub-step 24.5.1: Implement `SetTemporalUpscalerFSR2()` integration path and quality presets. (v0.24.5.1)
  * [x] Sub-step 24.5.2: Implement `SetTemporalUpscalerDLSS()` integration path and quality presets. (v0.24.5.2)
  * [x] Sub-step 24.5.3: Implement `SetTemporalUpscalerXeSS()` integration path and quality presets. (v0.24.5.3)
  * [x] Sub-step 24.5.4: Implement `EnableFrameGeneration()` with input-latency guardrails and fallback modes. (v0.24.5.4)
  * [x] Sub-step 24.5.5: Implement `CaptureGPUFrameTrace()` for graphics diagnostics and regression triage. (v0.24.5.5)
  * [x] Sub-step 24.5.6: Runtime upscaler/frame-generation policy coordination with deterministic transition events. (v0.24.5.6)

## Phase 25: Animation Runtime 2.0, Rig Retargeting & Motion Matching (Weeks 68-71)

* [x] Step 25.1: Upgrade animation state orchestration and blend logic.
  * [x] Sub-step 25.1.1: Implement `CreateAnimationStateMachineGraph()` for data-authored, layered runtime state machines. (v0.25.1.1)
  * [x] Sub-step 25.1.2: Implement `CreateAnimationBlendTree()` with directional locomotion and additive overlays. (v0.25.1.2)
  * [x] Sub-step 25.1.3: Implement `SetAnimatorParameterValue()` with typed parameter channels and event sync. (v0.25.1.3)
  * [x] Sub-step 25.1.4: Runtime orchestration compatibility bridge across legacy/hybrid/graph modes. (v0.25.1.4)
* [x] Step 25.2: Add retargeting and procedural rig bake pipeline.
  * [x] Sub-step 25.2.1: Implement `RetargetAnimationBetweenSkeletons()` with bone-map templates and retarget profiles. (v0.25.2.1)
  * [x] Sub-step 25.2.2: Implement `BakeControlRigToAnimation()` for runtime/editor interchange and cinematic capture. (v0.25.2.2)
  * [x] Sub-step 25.2.3: Retarget + control-rig bake interop batch workflow with lineage metadata and rollback-safe publication. (v0.25.2.3)
* [x] Step 25.3: Add data-driven high-fidelity locomotion.
  * [x] Sub-step 25.3.1: Implement `EvaluateMotionMatchingDatabase()` for pose-search locomotion selection. (v0.25.3.1)
  * [x] Sub-step 25.3.2: Motion database build, integrity checks, and deterministic search-index pipeline. (v0.25.3.2)
  * [x] Sub-step 25.3.3: Runtime telemetry, query budget governance, and stable fallback routing. (v0.25.3.3)

## Phase 26: Multiplayer Product Layer, Replay System & Rollback Framework (Weeks 72-76)

* [x] Step 26.1: Add dedicated-server and session orchestration APIs.
  * [x] Sub-step 26.1.1: Implement `StartDedicatedServerInstance()` with headless runtime profile and server-only feature flags. (v0.26.1.1)
  * [x] Sub-step 26.1.2: Implement `JoinSessionByInviteCode()` with session lookup, validation, and secure join flow. (v0.26.1.2)
  * [x] Sub-step 26.1.3: Implement `DiscoverLANSessions()` with query broadcast and compatibility filters. (v0.26.1.3)
  * [x] Sub-step 26.1.4: Session product-layer integration bridge for runtime mode boundaries, diagnostics, and MCP controls. (v0.26.1.4)
* [x] Step 26.2: Strengthen replication and RPC contract control.
  * [x] Sub-step 26.2.1: Implement `RegisterReplicatedPropertyPolicy()` for update rate, relevance, and quantization rules. (v0.26.2.1)
  * [x] Sub-step 26.2.2: Implement `RegisterNetworkRPC()` with reliability class, auth checks, and replay support. (v0.26.2.2)
  * [x] Sub-step 26.2.3: Contract compatibility and runtime enforcement bridge across handshake/session workflows. (v0.26.2.3)
* [x] Step 26.3: Build deterministic replay and rollback systems.
  * [x] Sub-step 26.3.1: Implement `RecordNetworkReplay()` capturing network stream + authoritative frame markers. (v0.26.3.1)
  * [x] Sub-step 26.3.2: Implement `PlayNetworkReplay()` with time controls and frame-accurate playback state. (v0.26.3.2)
  * [x] Sub-step 26.3.3: Implement `RollbackSimulationFrame()` for corrected authoritative rewinds. (v0.26.3.3)
  * [x] Sub-step 26.3.4: Implement `ResimulatePredictedFrames()` for reconciliation without hard snaps. (v0.26.3.4)
  * [x] Sub-step 26.3.5: Implement `MigrateHostSession()` for resilient peer-host continuity. (v0.26.3.5)
  * [x] Sub-step 26.3.6: Replay/rollback/migration observability and governance hardening (MCP/UI telemetry + feature gates). (v0.26.3.6)

## Phase 27: Runtime UI Authoring, Data Binding & World Widgets (Weeks 77-79)

* [x] Step 27.1: Build declarative widget asset pipeline and runtime loader.
  * [x] Sub-step 27.1.1: Implement `CreateWidgetBlueprintAsset()` for reusable UI prefabs and style inheritance. (v0.27.1.1)
  * [x] Sub-step 27.1.2: Implement `LoadWidgetLayoutAsset()` with async dependency loading and instance pooling. (v0.27.1.2)
  * [x] Sub-step 27.1.3: Integrate Stage 27 UI asset cook/load pipeline and dependency tracking. (v0.27.1.3)
  * [x] Sub-step 27.1.4: Reconcile `Widget`/`WidgetSystem` runtime contract for Stage 27. (v0.27.1.4)
  * [x] Sub-step 27.1.5: Wire Stage 27 compile surface + runtime initialization flow. (v0.27.1.5)
* [x] Step 27.2: Add robust UI data binding and transition system.
  * [x] Sub-step 27.2.1: Implement `BindWidgetPropertyToData()` with one-way/two-way binding modes and validation hooks. (v0.27.2.1)
  * [x] Sub-step 27.2.2: Implement `AnimateWidgetTransition()` with timeline-driven state changes. (v0.27.2.2)
  * [x] Sub-step 27.2.3: Add binding/transition precedence arbitration and conflict diagnostics. (v0.27.2.3)
  * [x] Sub-step 27.2.4: Extend MCP/debug tooling for live binding/transition control. (v0.27.2.4)
* [x] Step 27.3: Expand world-space and localized modal UI primitives.
  * [x] Sub-step 27.3.1: Implement `RenderWorldSpaceWidget()` with depth-aware compositing and interaction routing. (v0.27.3.1)
  * [x] Sub-step 27.3.2: Implement `ShowLocalizedModalDialog()` with locale fallback, focus management, and action callbacks. (v0.27.3.2)
  * [x] Sub-step 27.3.3: Build unified interaction/focus arbitration across modal/world/screen/ImGui layers. (v0.27.3.3)
  * [x] Sub-step 27.3.4: Extend scene serialization/persistence for Stage 27 UI metadata. (v0.27.3.4)

## Phase 28: Profiling, Automation & Production Build Pipeline (Weeks 80-83)

* [x] Step 28.1: Add first-class profiling capture APIs across CPU and GPU.
  * [x] Sub-step 28.1.1: Implement `StartCPUProfilerCapture()` with scoped capture sessions and marker channels. (v0.28.1.1)
  * [x] Sub-step 28.1.2: Implement `StartGPUProfilerCapture()` with pass-level timing and queue breakdowns. (v0.28.1.2)
  * [x] Sub-step 28.1.3: Implement `ExportProfilerTrace()` to persistent trace formats for post-analysis. (v0.28.1.3)
* [x] Step 28.2: Integrate automated runtime validation suites.
  * [x] Sub-step 28.2.1: Implement `RunAutomatedPlayModeTests()` for deterministic gameplay regression packs. (v0.28.2.1)
  * [x] Sub-step 28.2.2: Implement `RunAutomatedPerformanceTests()` for budget regression gates by platform tier. (v0.28.2.2)
* [x] Step 28.3: Build shipping-grade multi-platform packaging flow.
  * [x] Sub-step 28.3.1: Implement `BuildForPlatformTarget()` with profile-driven compile/cook/package stages. (v0.28.3.1)
  * [x] Sub-step 28.3.2: Implement `PackageStoreSubmissionArtifacts()` for storefront-compliant output bundles. (v0.28.3.2)
  * [x] Sub-step 28.3.3: Implement `GenerateDedicatedServerBuildArtifacts()` for headless deploy images and symbols. (v0.28.3.3)

## Phase 29: Comprehensive Field Audit & Contract Validation (Weeks 84-87)

* [x] Step 29.1: Build an exhaustive field inventory across runtime, serialized assets, build outputs, and tooling surfaces.
  * [x] Sub-step 29.1.1: Implement `GenerateRuntimeFieldInventory()` to enumerate ECS/component/renderer/physics/network/UI/editor-exposed fields with canonical identifiers. (v0.29.1.1)
  * [x] Sub-step 29.1.2: Implement `GenerateSerializedFieldInventory()` to enumerate scene/prefab/save/widget/localization/addressable/bundle field schemas. (v0.29.1.2)
  * [x] Sub-step 29.1.3: Implement `GenerateProtocolFieldInventory()` for packet/RPC/replay/MCP payload fields and transport-level metadata constraints. (v0.29.1.3)
  * [x] Sub-step 29.1.4: Implement `MergeFieldInventorySnapshots()` with stable field IDs, ownership tags, source traces, and version lineage metadata. (v0.29.1.4)
* [x] Step 29.2: Add strict field-level validation rules and invariant analysis.
  * [x] Sub-step 29.2.1: Implement `ValidateFieldTypeAndNullabilityContracts()` across runtime reflection, serializers, and generated manifests. (v0.29.2.1)
  * [x] Sub-step 29.2.2: Implement `ValidateFieldRangeEnumAndPatternDomains()` for numeric bounds, enum domains, string formats, and identifier normalization. (v0.29.2.2)
  * [x] Sub-step 29.2.3: Implement `ValidateCrossFieldInvariantRules()` for dependency ordering, conditional required fields, and subsystem coupling assumptions. (v0.29.2.3)
  * [x] Sub-step 29.2.4: Implement `ValidateFieldEvolutionCompatibility()` for forward/backward schema evolution and migration safety checks. (v0.29.2.4)
* [x] Step 29.3: Execute full-surface audit runs over live runtime and offline artifacts.
  * [x] Sub-step 29.3.1: Implement `RunRuntimeStateFieldAudit()` with deterministic snapshots during gameplay/editor/runtime service transitions. (v0.29.3.1)
  * [x] Sub-step 29.3.2: Implement `RunCookedAndPackagedArtifactFieldAudit()` for cooked assets, build manifests, store bundles, and dedicated-server outputs. (v0.29.3.2)
  * [x] Sub-step 29.3.3: Implement `RunNetworkAndReplayFieldAudit()` to verify replication payload parity, rollback/replay schema fidelity, and host-migration continuity fields. (v0.29.3.3)
  * [x] Sub-step 29.3.4: Implement `RunToolingAndAuthoringFieldAudit()` for editor pipelines, MCP tool payloads, and automation report schemas. (v0.29.3.4)
* [x] Step 29.4: Generate actionable findings with severity, ownership, and traceable evidence.
  * [x] Sub-step 29.4.1: Implement `GenerateFieldAuditIssueLedger()` with deduplicated issue IDs, first-seen revision, and reproducible evidence pointers. (v0.29.4.1)
  * [x] Sub-step 29.4.2: Implement `ComputeFieldIssueSeverityAndBlastRadius()` with gameplay/runtime/build/release impact scoring heuristics. (v0.29.4.2)
  * [x] Sub-step 29.4.3: Implement `ExportFieldAuditComplianceReport()` with machine-readable summaries and human triage views. (v0.29.4.3)
  * [x] Sub-step 29.4.4: Implement `CreateFieldRemediationBacklogFromAudit()` that emits prioritized fix tasks grouped by subsystem ownership. (v0.29.4.4)

## Phase 30: Field Integrity Remediation, Hardening & Closure (Weeks 88-92)

* [x] Step 30.1: Fix schema and contract defects discovered during Phase 29 audit.
  * [x] Sub-step 30.1.1: Implement `PatchFieldSchemaDefinitions()` for incorrect types, nullability flags, and required/optional mismatches. (v0.30.1.1)
  * [x] Sub-step 30.1.2: Implement `NormalizeFieldDefaultAndFallbackPolicies()` to remove ambiguous defaults and ensure deterministic initialization. (v0.30.1.2)
  * [x] Sub-step 30.1.3: Implement `FixFieldSerializationMappings()` aligning runtime/editor/cooked naming, aliases, and path mappings. (v0.30.1.3)
  * [x] Sub-step 30.1.4: Implement `VersionAndApplyFieldSchemaMigrations()` with explicit compatibility windows and rollback-safe transforms. (v0.30.1.4)
* [x] Step 30.2: Repair data-at-rest integrity across authored and generated content.
  * [x] Sub-step 30.2.1: Implement `MigrateSceneAndPrefabFieldData()` to backfill required values and normalize stale object graphs. (v0.30.2.1)
  * [x] Sub-step 30.2.2: Implement `MigrateUIAndLocalizationFieldData()` to correct binding keys, locale schema drift, and modal/world-widget metadata. (v0.30.2.2)
  * [x] Sub-step 30.2.3: Implement `MigrateAddressableBundleAndBuildManifestFieldData()` for catalog/bundle/build profile parity. (v0.30.2.3)
  * [x] Sub-step 30.2.4: Implement `RepairPlayerSaveReplayAndAutomationFieldData()` to restore compatibility for persistence and deterministic test baselines. (v0.30.2.4)
* [x] Step 30.3: Fix runtime, networking, and deployment-time field integrity defects.
  * [x] Sub-step 30.3.1: Implement `FixRuntimeFieldBindingAndReflectionRoutes()` across ECS systems, UI binding, animation, and tool-facing APIs. (v0.30.3.1)
  * [x] Sub-step 30.3.2: Implement `FixReplicationRPCAndRollbackFieldParity()` to guarantee authoritative/client schema consistency and replay determinism. (v0.30.3.2)
  * [x] Sub-step 30.3.3: Implement `FixFieldUpdateOrderingForDeterminism()` across frame phases, job boundaries, and serialization checkpoints. (v0.30.3.3)
  * [x] Sub-step 30.3.4: Implement `FixStoreAndDedicatedServerFieldContracts()` for release metadata, artifact manifests, and deployment descriptors. (v0.30.3.4)
* [ ] Step 30.4: Add permanent guardrails to prevent future field regressions.
  * [x] Sub-step 30.4.1: Implement `AddFieldInvariantAssertions()` in runtime/editor/build pipelines with explicit error taxonomies. (v0.30.4.1)
  * [x] Sub-step 30.4.2: Implement `AddFieldContractRegressionSuites()` spanning profiling, automation, packaging, networking, persistence, and tooling flows. (v0.30.4.2)
  * [ ] Sub-step 30.4.3: Implement `AddFieldAuditGateToBuildPipeline()` that blocks release lanes on unresolved critical/high field defects. (v0.30.4.3)
  * [ ] Sub-step 30.4.4: Implement `AddFieldDriftMonitoringAndAlerting()` for schema drift detection between commits and build artifacts. (v0.30.4.4)
* [ ] Step 30.5: Close remediation with independent verification and governance sign-off.
  * [ ] Sub-step 30.5.1: Implement `ReRunFullFieldAuditAndDiffAgainstBaseline()` to verify net defect reduction and detect regressions. (v0.30.5.1)
  * [ ] Sub-step 30.5.2: Implement `EnforceZeroCriticalFieldDefectGate()` across runtime, network, persistence, and release artifact domains. (v0.30.5.2)
  * [ ] Sub-step 30.5.3: Implement `PublishFieldIntegritySignoffReport()` with unresolved-risk disclosures and ownership acknowledgements. (v0.30.5.3)
  * [ ] Sub-step 30.5.4: Implement `FreezeFieldContractVersionForNextPhase()` with policy checkpoints for controlled schema evolution. (v0.30.5.4)
