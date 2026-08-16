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

The GLSL mirror of `GpuInstance` lives in one place (`kGpuInstanceGLSL`) and is
substituted into both the culler and the geometry vertex shader. Two hand-written
copies is exactly how this broke once: adding a field for skinning updated the
culler and not the vertex shader, so every instance after the first read its
transform at the wrong offset and objects collapsed onto one another. Nothing
caught it because the checks up to that point had one visible object.

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

Each primitive of a mesh is clusterised separately and drawn as its own
instance with its own material and its own bounds, so an imported model shades
its submeshes correctly instead of painting all of them with the first material.
`assets/meshes/two_materials.gltf` is the two-triangle, two-material asset that
pins this down: it imports as one mesh, two sections, two instances, two
indirect draws.

**Arena eviction**. The merged arena was a bump allocator with no eviction: once
full, every further mesh fell off the GPU-driven path and stayed off, because a
capacity failure was recorded the same way as unusable geometry. Those are
different verdicts - one is permanent, the other means "not right now" - and
conflating them meant a mesh that arrived while the arena was busy never became
resident even after room appeared.

Meshes now carry the frame they were last drawn. When an upload does not fit, the
arena drops everything untouched for two seconds and rebuilds from the survivors,
most recently drawn first. Residency is settled for every command before a single
section pointer is taken, because compaction rebuilds every record and a pointer
grabbed earlier in the loop would dangle. Verified by overfilling: 36 large
meshes fit, hiding them and spawning 70 more triggers one compaction that
reclaims all 36, and nothing is rejected.

**Level of detail** (`GPUScene`, via meshoptimizer). Every mesh drew at full
density however small it was on screen. Each section now keeps up to three
levels: level 0 as authored, then halvings produced by `meshopt_simplify`, which
rewrites only indices and leaves the vertex buffer alone - exactly what a shared
arena wants, since switching level costs a different cluster range and nothing
else. Selection is per instance from projected radius in pixels
(`projection[1][1] * renderHeight * 0.5 / distance`, scaled by the instance's own
scale), and bounds always come from level 0, or culling would depend on which
level was picked. Shadow views inherit the choice for free, because they cull the
same cluster ranges. Measured on seven spheres spread from the camera to 95 units
out: 2,368 triangles against 6,400 with selection off, and 1,600 when every
instance is forced to the coarsest level.

This is per-object LOD, not Nanite's per-cluster DAG: a single instance switches
as a unit, so a large object crossing a threshold pops rather than refining the
half of itself that is far away.

**Rigged glTF import**. `LoadSkeletalGLTF` read joints, weights, inverse binds,
and animation clips, and nothing could reach it: `LoadGLTF` was the only entry
point anyone called and it dropped all of that on the floor, so a character
imported as a frozen pile of triangles. `LoadGLTF` now delegates when the file
has a skin, which fixes every caller rather than the one that happened to
complain. It also mirrors the bind pose into the static vertex stream, because
that is what the GPU scene clusterises and what the skinning pass writes back
into - a mesh carrying only skinned vertices was rejected outright.

Clips had no runtime either. `AnimatorSystem` only runs for entities with an
animator graph, and `SkeletalRenderSystem` - which does sample clips onto a pose -
was never constructed. Its animation half now runs in `SystemPipeline`, after the
animator and before IK. Its draw half stays dead; `RenderSystem` collects skeletal
draws. `assets/meshes/rigged_strip.gltf` is a two-joint strip with a bend clip:
importing it starts the clip, and across three captures 18,947 and 12,875 pixels
move, against exactly 0 for the same mesh with nothing playing.

**Reflection probe** (`Core/Renderer/EnvironmentProbe.*`, MCP
`BakeEnvironmentProbe`). Reflections could only fall back to a two-colour
analytic sky, so anything off screen reflected a gradient rather than the room it
was standing in. A probe is exactly what the screen-space trace misses.

Its mips are a GGX prefilter, not a plain blur: each level convolves the captured
environment with the specular lobe for the roughness that level stands for, by
importance sampling the distribution with a Hammersley sequence. A box downsample
is cheaper and looks approximately right, but it spreads energy the way a camera
defocus does rather than the way a rough surface does. Measured on one sphere
with only its roughness changed, the reflection's variation across the surface
falls from 6.93 to 5.74 to 1.08 as roughness goes 0.05, 0.40, 0.85.

It is baked from the renderer's own output rather than a second render path: six
frames render with the camera pointed down each cube face and are blitted into
the cube, which costs six frames once instead of a duplicate pipeline forever. It
also means the probe reflects precisely what the engine draws, lighting and post
chain included. Sampling is parallax corrected: a cube captured at one point is only right at
that point, and reading it by direction alone makes a reflection slide with the
camera instead of staying pinned to the surroundings. The reflected ray is
intersected with a proxy sphere around the probe and sampled toward that hit.
Measured by moving a mirrored sphere three units: the reflected highlight drifts
180 pixels across it uncorrected and 134 corrected.

Both the geometry pass and the reflection pass sample it through
the same shared function, because a traced hit replaces the ambient specular term
and can only subtract what it can reproduce - one pass reading a probe while the
other read a sky would put a seam at the edge of every reflection. Verified in a
red room: baking makes 2,580 pixels of a chrome sphere change, 1,369 of them
toward red.

**Environment specular** (`Core/Renderer/EnvironmentBRDF.h`). Ambient was applied
as pure diffuse, which a metal does not have: metals came out darker than the
dielectrics beside them and reflected nothing at all. Ambient diffuse is now
scaled by `1 - metallic`, and every surface gains a specular term - a two-colour
environment, sky above and ground below, weighted by Karis' analytic fit to the
split-sum BRDF. Not a captured cubemap, but the difference between a metal that
shows its surroundings and one that shows nothing. The sky comes from the GI
settings, so the frame has one sky rather than two that disagree.

The formula lives in one shared GLSL snippet because two passes must agree on it
exactly: the geometry pass adds it, and the reflection pass *replaces* it
wherever a ray hit, which it can only do by reproducing what it subtracts.
Adding both would make a mirror twice as bright as the thing it reflects.
Verified with GI, reflections, bloom and the temporal pass all off, leaving sky
colour the only path to a pixel: changing it alone moves 173,440 pixels on a
chrome sphere, 99.9% of them brighter and 99.9% gaining more blue than red.

**Screen-space reflections** (`Core/Renderer/PostProcess/ComputeSSR.*`). Every
surface was purely diffuse plus a direct highlight: a polished floor showed the
light, never the room. The G-buffer already carried what a reflection needs -
depth, a world normal with metallic in its alpha, and albedo with roughness in
its alpha - so this is a march over buffers that already exist rather than a
second view of the scene. It runs after the resolve, so what it reflects is fully
lit including indirect light, and before the temporal pass, so its per-pixel
jitter is what TAA settles. A hit is refined by five halvings, then faded at the
screen edges, with ray distance, and with roughness up to a cutoff where a single
mirror ray stops meaning anything. Verified by capture on eight chrome spheres:
135,565 pixels change when it is switched on, 96.2% of them brighter.

**Order-independent transparency** (weighted blended). Blended surfaces used to
composite in draw order, sorted per instance, so two intersecting panes resolved
by whichever centre happened to be nearer and the wrong one won across half the
overlap. They now write to two extra targets instead of the lit image: a
weighted sum of every fragment, and the product of what each let through.
Addition and multiplication do not care what order the terms arrive in, which is
the whole trick. A compute pass divides one by the other to recover an average
colour and mixes it over the lit image by revealage.

The weight falls off with depth so a near surface still dominates a far one -
that is what stands in for sorting - and is clamped, because the useful range
spans several orders of magnitude and the top of it overflows a half float.
Measured on two half-opacity spheres offset only in depth: the near and far
colours come out at a ratio of 0.99, where sorted over-compositing would give
about 2.00.

The back-to-front sort in `GPUScene` is no longer load-bearing for correctness.
It still groups batches, so it stays.

**Alpha modes**. The engine drew every surface as if it were opaque, so a leaf
texture rendered as a rectangle and glass as a wall. Materials now carry glTF's
three modes. *Masked* bakes a `discard` below its cutoff into the material's own
shader, which costs nothing and needs no sorting, so foliage stays in the
ordinary depth-writing pass. *Blend* is a pipeline variant: blending on, depth
writes off, and colour writes disabled on the albedo and normal targets so the
surface behind keeps its own indirect lighting. Blended instances sort to the end
of the frame back to front, never merge into a neighbouring batch (merging would
undo the sort), skip the HZB occlusion test since they write no depth, draw only
in the early phase because the late phase would blend them twice, and cast no
shadow - the shadow pass has no alpha, so a window would throw the silhouette of
a wall.

Verified by capture: with a sphere at opacity 0.5, every one of the 179,253
pixels it covers lands between the fully opaque render and the background.
Masked, an opacity of 0.35 gives 179,281 lit pixels at cutoff 0.2 and exactly 0
at cutoff 0.9. `assets/meshes/alpha_modes.gltf` carries one primitive per mode
and imports as opaque, masked with its 0.35 cutoff, and blend.

Finding this took fixing a second bug: the material pipeline cache was keyed on
the graph hash alone, so alpha mode, its cutoff, and double-sidedness could all
change without the pipeline ever being rebuilt. Double-sidedness had been
silently broken the same way since it was added.

**Temporal antialiasing** (`Core/Renderer/PostProcess/ComputeTAA.*`). The frame
was already being jittered every frame for the upscaler and nothing resolved it:
each frame sampled a different sub-pixel offset and went straight to the screen,
so the image crawled and the jitter was pure cost. History is reprojected from
depth against the previous *unjittered* view-projection - reprojecting with the
jittered pair would chase the very offset the pass exists to resolve - then
constrained to the current 3x3 neighbourhood in YCoCg before blending. The clamp
is what stops it smearing. Measured over captured frames of a static scene, the
mean absolute difference between consecutive frames falls from 0.220 with TAA off
to 0.157 at feedback 0.5 and 0.149 at 0.97.

**Verification harness** (`tools/verify_renderer.py`). Start the engine, run the
script: it places the camera, builds a scene, and checks eight renderer features
end to end from captured frames rather than from a pass reporting itself active -
multi-instance placement, level of detail, per-submesh materials, skinning,
blended transparency, reflections, environment specular, and depth of field.

Every check frames at least two objects deliberately. The instance-layout bug
above survived eight rounds of hand checks because each had a single visible
object, and with one object there is nothing for a wrong transform to disagree
with. Two of the checks also had to be rewritten to be about the right thing:
whole-frame mean gradient measures temporal noise rather than defocus, and
"reflections make things brighter" stopped being true once reflections replaced
the environment term instead of stacking on it.

**Camera placement** (MCP `SetEditorViewport`, `cameraPosition`/`cameraTarget`).
The editor camera could only be flown by hand with the mouse, so nothing outside
the window could decide what the frame looks at. Every headless check therefore
depended on whatever the camera happened to be pointing at, which in practice
meant putting the subject at the origin and hoping. Placing it explicitly turns
`CaptureFrame` from a screenshot into a measurement.

**Velocity buffer and motion blur** (`ComputeMotionBlur.*`, plus a fourth colour
attachment on the scene pass). The geometry pass now writes how far each pixel
moved, from the instance's previous transform and the previous unjittered
view-projection. Both matrices are unjittered on purpose: jitter is a property of
how the frame was rendered and velocity is a property of the scene, and mixing
them makes a still object report motion every frame. Verified exactly that way -
a still scene writes zero velocity everywhere, and camera motion writes non-zero.

Motion blur gathers along the largest velocity in a pixel's neighbourhood rather
than the pixel's own. A background pixel beside a fast object has no velocity, so
gathering along it left the object blurring strictly inside its outline against a
sharp background, which is not what a shutter does. Two reductions - a maximum
per 16-pixel tile, then a maximum over each tile's neighbours - give the blur
something to follow one tile past the geometry that caused it, and taps are
weighted by whether either end of the pair is actually moving so a still region
beside a fast one stays sharp. Both reductions are capturable
(`CaptureFrame` targets `velocityTiles` and `velocityNeighbours`), because the
dilation is the mechanism and it is invisible in the final image: measured at
2.67x, 16 tiles covered against 6.

Temporal antialiasing reads it and falls back to depth reprojection only where
nothing was written, which is the sky. Before this it could only ever describe
the camera, so anything moving in the world ghosted. Motion blur gathers along
the vector, last in the chain: blurring before the temporal pass would push a
smeared frame into the history, and before defocus it would smear across a depth
boundary defocus had not yet softened.

Adding the attachment moved depth from index 3 to index 4, and the late-phase
render pass still assumed 3 - so it cleared depth every late pass and handed the
velocity target a depth layout. That is why velocity read back as empty until it
was fixed.

**Depth of field** (`Core/Renderer/PostProcess/ComputeDepthOfField.*`).
`PostProcessComponent` has carried dofEnabled, dofFocalDistance, dofFocalRange
and dofMaxBlur from the start and nothing ever read them: the old
`DepthOfFieldPass` could not run, because `PostProcessPass` had no parameter
through which to receive a depth buffer. One gather over a 16-tap sunflower disc,
radius scaled by circle of confusion, each tap weighted by its *own* CoC so a
sharp foreground object does not smear onto the background behind it. It runs
after the temporal resolve - blurring first would put a blurred frame into the
history and pull the next sharp one toward it, which reads as smearing rather
than defocus. Measured as mean horizontal gradient over the region a distant
sphere covers: 0.021 with it off, 0.012 focused near, and 0.025 focused on the
sphere itself.

**Frame capture** (`SceneRenderer::CaptureToFile`, MCP `CaptureFrame`). Nothing
outside the window could see what the renderer produced, so no visual change was
checkable: `GetRenderStats` reports that a pass ran, not what it drew. This blits
whatever image the frame ended on - upscaler output, bloom output, or the
resolved scene - into an 8-bit staging image and writes a PNG. Every measurement
quoted for TAA above came out of it.

**GPU skinning** (`Core/Renderer/GPUDriven/GPUSkinningPass.*`). Skeletal meshes
used to be rejected outright: the merged arena stores one vertex layout, and a
skinned vertex is wider, so the engine could animate a skeleton and never draw
it. Rather than add a second draw path, each skinned instance gets its own slice
of the same arena and a compute pass writes posed vertices into it before
anything reads it. Downstream the result is ordinary static geometry - same
clusters, same indirect draws, same shadow views - which is why skinned
characters cast cascade shadows without a line of shadow-side code. Verified
live over MCP: two rigged primitives produced exactly 234 skinned vertices and
384 resident triangles through the indirect path, with the cascades redrawing
once and then settling.

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
reachable over MCP, feeds the same GPU-driven path as procedural primitives, and
imports the file's materials and images as well as its geometry. A glTF material
becomes a material graph — base colour factor multiplied by its texture,
roughness and metallic split out of the packed channels glTF specifies, normal
and emissive — and because factors multiply their textures, an image the importer
could not resolve is harmless rather than fatal.

**Post-processing** (`Core/Renderer/PostProcess/Compute*.h`). Bloom (soft-knee
threshold, 13-tap downsample, tent upsample), SSAO over the G-buffer depth and
normal with a depth-aware blur, and colour grading folded into the composite
alongside the tonemap. The engine's previous post stack could not run at all -
four of its five passes never wrote their descriptor sets, and the pass interface
had no way to hand SSAO the depth buffer it needs - so it was replaced rather
than repaired.

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
- Texture compression is BC3 only. BC7 would look better at the same size but
  needs a different encoder than the one already vendored.
- Animation clips play one at a time per mesh, sampled straight onto the pose.
  Blending between clips is what `AnimatorSystem` and its graph are for, and that
  path still needs an `AnimatorComponent`.
- A submesh's material is resolved as the component's index plus the primitive's
  own slot in its file. That holds because the importer registers a file's
  materials contiguously; hand-assigning material indices to individual
  submeshes is not expressible.
- Images embedded in a glTF as data URIs are skipped; GLB buffer-view images and
  external files both work.
- The arena evicts by least-recently-drawn, and reclaiming means rebuilding: a
  bump allocator cannot free in the middle. Compaction is therefore a full
  re-upload of the survivors, paid only when something did not fit. A scene that
  compacts every frame is a scene whose working set does not fit, and
  `meshesAwaitingSpace` in the stats says so.
- A skinned instance owns arena space per *instance*, not per mesh, because two
  copies of a character hold different poses. The skinning region is capped at
  256K vertices; past that an instance renders in its bind pose rather than
  disappearing.
- Animation blending fills local poses only. Global poses and skinning matrices
  are resolved in `RenderSystem` at draw-collection time, which is after
  `AnimatorSystem` and `IKSystem` have had their say.
- Skinned geometry contributes no velocity of its own. The instance transform is
  in the buffer, but the previous *posed* vertex is not, so a character that
  animates in place reports no motion.
- Depth of field is one gather with a single field. A sharp foreground object
  has no near-field dilate, so it does not spill over the blur behind it the way
  a real lens makes it. The upgrade is a two-field split.
- There is one probe, baked on demand. Parallax correction keeps its reflections
  pinned to the world, but a single proxy sphere only approximates the space it
  was captured in; a room with a strong shape needs a box proxy, and several
  rooms need several probes.
- TAA reprojects from depth alone. There is no velocity target, so a moving
  object leans entirely on the neighbourhood clamp: correct for static geometry,
  slightly soft on fast movers. The upgrade is a velocity attachment on the scene
  pass plus a previous transform per instance.
- `CaptureFrame` takes a lock the render thread also takes, so a capture stalls
  one frame. Recording the copy inside the frame would avoid it; captures are far
  too rare to be worth the complexity.
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
