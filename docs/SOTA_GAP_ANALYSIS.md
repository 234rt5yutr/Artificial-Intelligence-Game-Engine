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

> **Update, later in August 2026:** the six rendering items this document called
> out as the real distance to UE/Unity — GPU-driven cluster culling, dynamic GI,
> a material graph, an editor viewport, FSR, and a render-thread split — are
> implemented and running. Each section below records what was built and what is
> still missing inside it. The sections are rewritten in place rather than
> appended, so this stays a description of the engine rather than a changelog.

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

### 1.2 Render-thread split — done

`Core/Renderer/RenderThread.*` runs Vulkan submission on its own thread while the
simulation for the next frame proceeds. The frame packet (`FrameRenderData`:
draw commands, lights, view matrices) is **copied**, because `RenderSystem`
rebuilds its draw list during the overlapping simulation step.

The sync point sits immediately before ImGui's `NewFrame`, which is what makes
this safe without deep-copying ImGui's draw lists. Two things had to move to keep
that guarantee honest:

- `ImGuiSubsystem::Render` both built the debug panels and recorded them. Panel
  construction reads scene state, so it split into `BuildOverlays()` (simulation
  thread) and `Render()` (render thread).
- `Scene::OnUpdate` pushed text draws into the shared `TextRenderer` while the
  render thread would be flushing it. It split into `OnUpdateSimulation` and
  `OnUpdateUI`, and only the first half overlaps.

Window resizes are deferred from the event callback to the frame's sync point,
since recreating the swapchain under a recording render thread would destroy
objects it is still using.

**Remaining:** UI construction does not overlap (it is sub-millisecond against
the simulation), and there is no separate RHI thread — submission and recording
share one thread.

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

### 2.1 GPU-driven cluster culling — done

`VirtualGeometryClusterBuilder` clusters *submeshes*, not triangles, which is not
a granularity a GPU culling pass can use. `Core/Renderer/GPUDriven/` adds the
real thing:

- **`GPUScene`** merges every registered mesh into one vertex arena and one index
  arena. Without this an indirect draw is pointless, because each mesh would
  still need its own bind. Meshes are clusterised into runs of <= 128 triangles
  ordered along a Morton curve, and the index buffer is reordered so each cluster
  is one contiguous `(firstIndex, indexCount)` range.
- **`GPUDrivenCuller`** dispatches one thread per cluster slot. It locates the
  owning instance by binary-searching a prefix sum the CPU uploads, rather than
  the CPU expanding every cluster every frame, then tests frustum, backface cone
  (meshoptimizer's formulation), and a hierarchical-Z pyramid, writing
  `VkDrawIndexedIndirectCommand`s directly. Culled clusters get `instanceCount`
  0, which the GPU discards for free.
- **Two-phase.** Phase 1 tests against the previous frame's HZB and draws.
  The HZB is rebuilt from the depth those draws produced, and phase 2 re-tests
  only the clusters phase 1 rejected for occlusion. This is the part that
  separates it from a plain one-frame-stale occlusion test.
- Instances are sorted by material on the CPU so each material is one
  `vkCmdDrawIndexedIndirect` over a contiguous slot range.

Verified live over MCP: 43 cluster slots across two meshes, 18 cone-culled, one
indirect batch, and `gpuDriven:false` correctly falls back to direct draws.

**Remaining:** no software rasteriser for sub-pixel triangles, no visibility
buffer, no cluster LOD hierarchy (one LOD per mesh), no streaming — the arena is
fixed-capacity and a mesh that does not fit stays on the direct path. Skinned
meshes are excluded because the arena stores one vertex layout.

### 2.2 Dynamic global illumination — done

The recommendation was to pick one path and finish it: screen-space GI with an
irradiance-probe fallback rather than hardware RT.
`Core/Renderer/GI/DynamicGlobalIllumination.*` is that path.

- Half-resolution screen traces: cosine-hemisphere rays marched in world space
  and re-projected each step, so a ray leaving the screen terminates rather than
  smearing edge pixels inward.
- A world radiance cache of 16,384 L1 spherical-harmonic probes (48 bytes each)
  on a camera-centred grid snapped to whole cells. Probes are fed by screen-space
  injection and only accept a sample when they sit within a cell of the surface
  they projected onto; probes with no coverage decay rather than holding stale
  light.
- Temporal reprojection through the previous frame's view-projection, using exact
  world position — no motion-vector buffer needed.
- Indirect light is modulated by the G-buffer albedo in the resolve pass, which
  is why the frame grew a slim G-buffer (colour, albedo+roughness,
  normal+metallic, depth).

**Remaining:** no surface cache, so off-screen indirect light is only as good as
the probe grid; no hardware ray tracing path; no sky/multi-bounce beyond the
cache's own feedback loop; no per-light shadowing of indirect contributions.

### 2.3 Shadows

`VirtualShadowMapCache` exists but there is no page table, no clipmap for the
directional light, and no per-page invalidation on geometry change. There is also
no equivalent of MegaLights — the many-shadow-casting-light path that UE 5.7 moved
to beta.

### 2.4 FSR upscaling — done

`Core/Renderer/Upscaling/FSRUpscaler.*` implements the FSR 1 pipeline in-engine:
EASU (12-tap edge-adaptive anisotropic upsample with a windowed-sinc kernel and a
neighbourhood clamp against ringing) followed by RCAS (contrast-adaptive
sharpening whose per-pixel lobe is limited so it cannot clip a neighbourhood
extreme). Quality presets drive the render resolution; Halton(2,3) jitter is
folded into the projection so the culling and draw matrices agree.

This is an implementation of the published algorithm, not a binding of AMD's SDK,
which is not a declared dependency of this project. Switching quality over MCP
rebuilds every offscreen target and was verified changing 853x480 to 640x360
against a 1280x720 swapchain.

**Remaining:** DLSS and XeSS are still enum values — both need vendor SDKs. There
is no FSR 2/3 temporal accumulator: stability comes from the engine's own jitter
and the GI history, not from a motion-vector-driven reconstruction.

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

### 3.2 Editor viewport — done

`Core/Editor/Panels/ViewportPanel.*` displays the renderer's own post-upscale
output as an ImGui image, so the editor shows exactly what the game sees — GI,
upscaling and all — rather than a second, simplified render path that would
drift. It has a fly camera that overrides the frame's view matrices when active,
click-to-select picking that unprojects through the same matrices the frame was
rendered with, a translate/rotate/scale gizmo whose drag is projected onto the
axis as it appears on screen, and play/pause/single-step driving the existing
`SystemPipeline` gating.

**Remaining:** no drag-and-drop asset browser; picking uses the transform's
scaled unit box rather than real mesh bounds, which are not cached anywhere;
prefabs, visual scripting graphs, and timelines are still asset structs with no
editor.

### 3.3 Material graph — done

`Core/Renderer/Material/MaterialGraph.*` is a DAG of ~22 node types compiled to
GLSL and injected into the lit shader template. Compilation detects cycles,
missing outputs, and dangling links; `Connect` refuses an edge that would close a
loop rather than letting codegen discover it later. Permutations are cached by
graph hash, so an unchanged graph never rebuilds its pipeline, and a graph that
fails to compile still gets a pipeline built from the template's defaults so the
object renders flat grey instead of disappearing. `MaterialLibrary` is what
`DrawCommand::MaterialIndex` now actually indexes.

**Remaining:** no texture import path, so `TextureSample` nodes bind a 1x1 white
placeholder — the graph, codegen, and permutation cache all work, but the
sampled value is constant until an asset importer exists. No node editor UI; the
graph is authored over MCP or JSON.

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

Done in this pass:

- ~~Feed lights and materials into the mesh pass~~ — the frame now renders a slim
  G-buffer through per-material pipelines with a full PBR forward shade over
  `LightSystem`'s directional and point lights.
- ~~GPU-driven cluster culling~~ (Tier 2.1)
- ~~Dynamic global illumination~~ (Tier 2.2)
- ~~FSR upscaling~~ (Tier 2.4)
- ~~Material graph~~ (Tier 3.3)
- ~~Editor viewport~~ (Tier 3.2)
- ~~Render-thread split~~ (Tier 1.2)
- **Procedural primitives.** Nothing in the engine could produce renderable
  geometry without a glTF file on disk, so no rendering path could be exercised
  at all. `Mesh::CreatePrimitive` builds box/sphere/plane/cylinder, and MCP
  `SpawnEntity` can request one.
- **A real bug this uncovered:** `VulkanBuffer` ignored `BufferDescriptor::mapped`
  for vertex and index buffers, allocating them device-local, so every
  `Mesh::UploadToGPU` silently failed on `Map()`. No mesh had ever been uploaded
  successfully. Fixed at the shared point rather than in `Mesh`.

Remaining, in order:

1. Asset import: `Mesh::LoadGLTF` is implemented with cgltf but nothing calls it,
   and there is no texture import, which is what caps the material graph at
   placeholder textures.
2. Shadows. `ShadowPass` and `VirtualShadowMapCache` still have no consumer, and
   the new frame draws none — the biggest remaining visual gap.
3. Port passes onto the render graph (`PostProcessManager::Execute` is still not
   called from the frame).
4. Skinned geometry through the GPU-driven path, which needs GPU skinning to
   write into the merged arena.
5. Cluster LOD hierarchy and streaming, so the arena stops being fixed-capacity.

The structural gate is closed and the rendering gate is now closed too: the frame
is GPU-driven, lit, globally illuminated, upscaled, authorable, observable in an
editor viewport, and submitted on its own thread. What remains is content
pipeline (1), shadows (2), and depth on the culling work (4–5).

---

## Sources

- [Unreal Engine 5.7: Nanite Foliage and MegaLights](https://wccftech.com/unreal-engine-5-7-out-now-with-nanite-foliage-and-megalights-powered-stunning-dynamic-shadow-casting-lights/)
- [What's New in Unreal Engine 5.7](https://vagon.io/blog/what-s-new-in-unreal-engine-5-7)
- [Unreal Engine 5.7 Performance Highlights — Tom Looman](https://tomlooman.com/unreal-engine-5-7-performance-highlights/)
- [Unity 6 is here: See what's new](https://unity.com/blog/unity-6-features-announcement)
- [Unity 6 URP E-Book (GPU Resident Drawer, Render Graph, APV)](https://unity.com/blog/biggest-edition-urp-ebook-unity-6)
