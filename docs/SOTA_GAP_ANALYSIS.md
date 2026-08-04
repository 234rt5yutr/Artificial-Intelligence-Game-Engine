# SOTA Gap Analysis

State of this engine measured against Unreal Engine 5.7 and Unity 6, based on a
first-hand audit of the source tree (August 2026) rather than the feature names in
the roadmap.

---

## Executive summary

The repository contains headers and implementation files named after most modern
engine features: virtualized geometry clustering, virtual shadow map caching,
global illumination, hardware ray tracing passes, a render graph, temporal
upscaling, frame generation, addressables, asset bundles, world partition,
motion matching, control rigs, rollback netcode, and an MCP tool surface.

The gap that matters is not the feature list. It is that **most of this code was
never connected to a running frame, and a large part of it was never compiled.**
Findings from the audit:

| Finding | Scale |
| --- | --- |
| `Scene::OnUpdate` ran only the UI system | Every gameplay/physics/animation/camera/render system was dead code |
| `JobSystem::Initialize()` was never called | Any parallel system would have hung forever on `Wait()` |
| `.cpp` files excluded from `CMakeLists.txt` | 18 files: all of Navigation, Terrain, Foliage, Skybox, IK, and 5 post-process passes |
| Of those 18, files that did not compile at all | 9 — the exclusion had hidden years of API drift (all now fixed and building) |
| MCP tool families that did not compile | 14 of 14 — `CreateAllMCPTools()` was never called from anywhere |
| No `PhysicsWorld` or `InputMapper` was ever constructed | The entire gameplay stack had no dependencies to run against |

These are addressed in the accompanying changes. What follows is the remaining
distance to Unreal/Unity parity, ordered by how much it actually blocks shipping a
game.

---

## Tier 1 — structural, blocks everything above it

### 1.1 The renderer is now scene-driven

`VulkanContext::DrawFrame()` used to draw a hard-coded 3-vertex triangle with a
vertex-input-less pipeline, ignoring `RenderSystem` entirely. Closing this needed
four things that were all missing:

- **A depth buffer.** The context had none at all, so 3D geometry would have
  resolved in submission order rather than by distance. Added a depth image,
  attachment, and `VK_COMPARE_OP_LESS` depth test, recreated with the swapchain.
- **A vertex-input pipeline.** The existing pipeline declares
  `vertexBindingDescriptionCount = 0`. Added a second pipeline whose vertex input
  matches `Renderer::Vertex` exactly, with an MVP push constant (no descriptor
  sets, so it needs nothing the RHI does not yet route).
- **Mesh GPU upload.** `Mesh::vertexBuffer`/`indexBuffer` were declared but never
  written anywhere. `Mesh::UploadToGPU(RHIDevice&)` fills them.
- **The hand-off.** `Application` now passes `RenderSystem::GetDrawCommands()` and
  `CameraSystem::GetViewProjectionMatrix()` to the context each frame.

Draws skip any mesh without GPU buffers rather than issuing a draw against a null
binding, and an empty draw list falls back to the original triangle so a scene
with no geometry still shows the renderer is alive.

**Still missing:** materials and lighting. The mesh pass is unlit (a hemisphere
term off the vertex normal) because per-material descriptor sets need the RHI to
own command submission first. `LightSystem::GetForwardPlusLights()` still has no
consumer, and `PostProcessManager::Execute` is still not called from the frame.

### 1.2 No render-thread / simulation-thread split

Everything runs on the main thread. Unreal has Game → Render → RHI threads with a
one-frame pipeline; Unity 6 has the SRP batcher plus its job-based render pipeline.
Until this exists, CPU-bound scenes cannot overlap simulation with submission and
the engine is limited to roughly the cost of `simulate + submit` per frame.

### 1.3 Render graph is built but not used

`Core/Renderer/RenderGraph/` implements a builder, executor, and transient resource
allocator, but the actual passes (`ForwardPlus`, `ZPrepass`, `ShadowPass`,
`PostProcessManager`) do not register through it. Unity 6 made Render Graph
mandatory in URP for exactly the reason it matters here: automatic barrier
placement and transient-memory aliasing.

**Missing:** port each pass to `RegisterRenderGraphPass` and execute one graph per
frame.

### 1.4 Post-process passes are now registered

`BloomPass`, `SSAOPass`, `DepthOfFieldPass`, `MotionBlurPass`, and
`ColorGradingPass` compiled but `PostProcessManager` never instantiated any of
them, so the chain was empty at runtime regardless of settings.
`PostProcessManager::RegisterDefaultPasses()` now builds the chain in execution
order (occlusion -> depth of field -> motion blur -> bloom -> grading).

Remaining: the passes are registered but `PostProcessManager::Execute` is not yet
called from the frame, because the renderer is still not scene-driven (1.1).

---

## Tier 2 — visual parity

### 2.1 Virtualized geometry is a cluster builder, not Nanite

`VirtualGeometryClusterBuilder` and `VirtualGeometryStreamingService` produce and
stream clusters, but there is no GPU-driven culling pipeline: no persistent
hierarchical cluster culling compute pass, no software rasterizer for sub-pixel
triangles, no visibility buffer. UE 5.7 additionally ships Nanite Foliage
(assemblies + skinning + voxel LOD).

**Missing:** visibility-buffer pass, two-phase occlusion culling against a HZB,
GPU-driven indirect draw of cluster batches.

### 2.2 No dynamic global illumination that runs

`GlobalIllumination.h` declares SSGI/VXGI/RTGI/probe paths; the compiled unit does
not implement the async bake, probe update, or radiance propagation entry points.
Unity 6's Adaptive Probe Volumes auto-place probes and support day/night blending;
Lumen in UE 5.7 is moving to hardware ray tracing at 60 Hz.

**Missing:** pick one path and finish it. Given the iGPU target stated in
`engine_plan.md`, screen-space GI with an irradiance-probe fallback is the
defensible choice, not hardware RT.

### 2.3 Shadows

`VirtualShadowMapCache` exists but there is no page table, no clipmap for the
directional light, and no per-page invalidation on geometry change. There is also
no equivalent of MegaLights — the many-shadow-casting-light path that UE 5.7 moved
to beta.

### 2.4 Upscaling is a manager without backends

`TemporalUpscalerManager` and `FrameGenerationController` compile, but FSR/DLSS/XeSS
are enum values with no vendor SDK integration. `DynamicResolution` marks them as
stubs.

**Missing:** at minimum FSR (open source, no vendor gating), wired to the TAA
history and motion vectors that `TAASystem` already produces.

---

## Tier 3 — content and tooling parity

### 3.1 No asset import path

`AssetCooker` and `AssetLoader` read and write a cooked binary format, but nothing
imports source assets. `cgltf` is a declared dependency and unused;
`TerrainGenerator::LoadHeightmap` generates placeholder data instead of reading an
image.

**Missing:** glTF mesh/material/skeleton import, texture import with BCn
compression, and a deterministic cook step that produces the existing cooked
headers.

### 3.2 Editor is panels without a viewport

`EditorAPI`, `EditorSelection`, `CommandStack`, `SceneHierarchyPanel`, and
`InspectorPanel` exist. There is no scene viewport, no transform gizmo, no
drag-and-drop asset browser, and no play-in-editor state transition. Prefabs,
visual scripting graphs, and timelines are asset structs with no editor.

### 3.3 Materials are not authorable

There is no material graph, no shader permutation authoring path from a material
definition, and `MeshComponent` carries a mesh path rather than a material
instance. `ShaderPermutationLibrary` compiles permutations but nothing declares
them from content.

### 3.4 No physics debug or visual scripting runtime

`GraphAsset` is a serialization struct with no interpreter. Unreal's Blueprints and
Unity's Visual Scripting are both major reasons non-programmers ship with them.

---

## Tier 4 — production concerns

- **No platform abstraction beyond Windows/Vulkan.** No D3D12 backend despite the
  RHI being designed for it, no console/mobile targets.
- **Test coverage is 18 executables over audit/build/diagnostics services only.**
  Nothing tests the renderer, physics, ECS, or networking.
- **No crash telemetry or symbol server integration**, despite
  `StoreSubmissionPackager` producing symbol bundles.
- **Networking is unproven.** Replication, prediction, reconciliation, rollback, and
  host migration all compile, but nothing exercises them; there is no loopback
  integration test.

---

## Recommended order

Done in this pass:

- ~~Register post-process passes in `PostProcessManager`~~
- ~~Port the Navigation module to its own headers and the installed Detour~~ (all
  six files build; `NavigationSystem` now ticks inside `SystemPipeline`)
- ~~Bring Skybox and TerrainSystem back into the build~~ (they used
  `RHI::BufferDesc`/`MemoryType`/`UpdateBuffer`, none of which exist; the real API
  is `RHI::BufferDescriptor` + `CreateBuffer` + `Map`/`Unmap`)
- ~~Make the renderer scene-driven~~ — depth buffer, vertex-input pipeline with
  MVP push constant, and the `Application` -> `VulkanContext` draw hand-off.
  Verified running windowed with no validation errors.
- ~~Mesh GPU upload~~ — `Mesh::vertexBuffer`/`indexBuffer` were declared but
  never written anywhere in the tree. `Mesh::UploadToGPU(RHIDevice&)` fills them
  from the CPU vertex/index vectors, picking the skinned or static stream by mesh
  kind and publishing both buffers only if both allocations succeed.
- ~~Give Terrain/Foliage/Sky a real `RHI::RHIDevice`~~ — `RHIDevice` had **no
  implementation anywhere in the tree**, which is what kept those three systems
  out of the pipeline. `Core/RHI/Vulkan/VulkanDevice` now implements it over the
  existing `VulkanContext`: buffers, textures, and samplers are real
  (`VulkanBuffer`/`VulkanTexture` already implemented their RHI interfaces and
  only needed the VMA allocator). Command-list/pipeline/render-pass creation
  deliberately warn-once and return null, because the engine still submits
  through `VulkanContext` directly — implementing them would create a second,
  unused submission path.

Remaining, in order:

1. Feed lights and materials into the mesh pass, and call
   `PostProcessManager::Execute` from the frame. The geometry path works; it is
   unlit.
2. Wire `Mesh::LoadGLTF` (already implemented with cgltf, but nothing calls it)
   into the asset pipeline so scenes can reference mesh files, plus BCn texture
   cook.
3. Port passes onto the render graph.
4. Editor viewport with gizmos and play-in-editor.
5. FSR upscaling wired to existing motion vectors.
6. Render-thread split.
7. GPU-driven cluster culling.

The structural gate is closed: simulation runs, the renderer consumes the scene,
and every subsystem is constructed. What remains is depth of feature rather than
missing plumbing — lighting, materials, asset import wiring, and the GPU-driven
work in Tier 2. Items 6–7 are the ones that
genuinely approach UE/Unity rendering architecture, and should not be started
before item 1 makes the frame observable.

---

## Sources

- [Unreal Engine 5.7: Nanite Foliage and MegaLights](https://wccftech.com/unreal-engine-5-7-out-now-with-nanite-foliage-and-megalights-powered-stunning-dynamic-shadow-casting-lights/)
- [What's New in Unreal Engine 5.7](https://vagon.io/blog/what-s-new-in-unreal-engine-5-7)
- [Unreal Engine 5.7 Performance Highlights — Tom Looman](https://tomlooman.com/unreal-engine-5-7-performance-highlights/)
- [Unity 6 is here: See what's new](https://unity.com/blog/unity-6-features-announcement)
- [Unity 6 URP E-Book (GPU Resident Drawer, Render Graph, APV)](https://unity.com/blog/biggest-edition-urp-ebook-unity-6)
