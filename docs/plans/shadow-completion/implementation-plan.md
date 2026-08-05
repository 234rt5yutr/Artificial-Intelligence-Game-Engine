# Shadow Completion — Implementation Plan

Finishes the shadow work started in `Core/Renderer/Shadows/ShadowRenderer.*`:
point-light cube shadows, virtual shadow map paging, and a many-light path.

Written before implementing, so the design can be argued with rather than
reverse-engineered from the diff.

---

## What exists now

| Piece | State |
| --- | --- |
| Directional CSM | Working. 1–4 cascades, practical splits, sphere fit, texel snap, PCF. |
| Spot shadows | Working. Shared atlas, one tile per light, importance-ranked allocation. |
| Point shadows | **None.** Point lights are lit but cast nothing. |
| `VirtualShadowMapCache` | Bookkeeping only — hit/miss/evict accounting over cache keys. No page table, no GPU resources, no consumer. |
| `ForwardPlusLightData` | Declares the right constants (`TILE_SIZE 16`, `NUM_Z_SLICES 32`, `MAX_LIGHTS_PER_CLUSTER 256`, `MAX_LIGHTS 10000`) over RHI buffers, but `VulkanDevice` routes no compute, so nothing culls. |
| Light caps in the lit shader | Fixed uniform arrays: 4 directional, 16 point, 8 spot. |

Both existing classes are on the RHI path the renderer does not use. Neither is
extended here; the page table and the light culling are built where the frame
actually runs, and the dead ones are removed once superseded.

---

## Stage 1 — Point-light cube shadows

**Problem.** A point light casts in every direction, so it needs six views, not
one. The atlas currently allocates a single tile per light.

**Approach.** Six tiles per point light in the same atlas, one per cube face.

- Grid grows from 3×3 to 4×4 (16 tiles). One point light (6) plus up to ten
  spots, or two point lights plus four spots.
- Allocation stays importance-ranked, but becomes a **tile-run allocator**: a
  point light asks for 6 contiguous tiles, a spot for 1. A light that does not
  fit is skipped rather than partially allocated — half a cube is worse than
  none, because the missing faces read as fully lit.
- Face matrices are the standard six: 90° fov, aspect 1, near 0.05, far radius,
  looking down ±X, ±Y, ±Z.

**Shader.** Pick the face from the major axis of `worldPos - lightPos`, then use
that face's matrix and tile. This is a per-pixel branch on a value that is
uniform across most of a triangle, so it costs little.

```
vec3 d = worldPos - lightPos;
vec3 a = abs(d);
int face = a.x >= a.y && a.x >= a.z ? (d.x > 0 ? 0 : 1)
         : a.y >= a.z              ? (d.y > 0 ? 2 : 3)
         :                           (d.z > 0 ? 4 : 5);
```

**Seams.** Tile UVs are already inset by a texel before PCF taps. Cube faces
additionally need the inset applied *before* the face pick is committed,
otherwise a tap near an edge samples the neighbouring face's tile — which is a
different projection entirely and reads as a hard black line along the seam.

**Cost.** Six depth passes per point light. The cap (`kMaxPointShadows = 2`)
exists because the uniform block carries six `mat4` per light; past two lights
that block is larger than the light data itself.

---

## Stage 2 — Clustered light culling (the many-light path)

**Problem.** The lit shader iterates fixed arrays capped at 16 point and 8 spot
lights, and every pixel tests every light. That is both a hard ceiling and
quadratic waste. "MegaLights" in UE 5.7 is fundamentally *many shadow-casting
lights made affordable*; the affordability comes first.

**Approach.** Froxel (clustered) culling, honouring the constants
`ForwardPlusLightData` already declares.

1. **Light SSBO.** Replace the fixed uniform arrays with a storage buffer of up
   to `MAX_LIGHTS` entries. This is the change that removes the cap.
2. **Cluster grid.** `ceil(w/16) × ceil(h/16) × 32` froxels over the render
   target, exponential in Z so slices track perspective rather than screen depth.
3. **Cull compute.** One thread per froxel; test each light's bounding sphere
   (and, for spots, its cone) against the froxel AABB; append surviving indices
   to a shared index list with an atomic cursor, and write `(offset, count)` into
   a light grid.
4. **Shader.** Per pixel, compute the froxel from `gl_FragCoord` and the view
   depth, then iterate only that froxel's list.

**Why this ordering.** Stage 2 lands before Stage 3 because the page table's
invalidation logic needs a stable answer to "which lights affect this region",
and the cluster grid is that answer.

**Kept as-is.** Directional lights stay in the uniform block. There are at most
a handful, they affect every froxel, and culling them would cost more than it
saves.

---

## Stage 3 — Virtual shadow map paging

**Problem.** Every cascade is redrawn in full every frame, even when neither the
camera nor anything in that cascade moved. For a 4×2048² directional setup that
is 16M shadow texels rasterised per frame to produce, usually, the same image.

**Approach.** A page table per cascade with real invalidation.

- Each cascade is divided into a grid of pages (`kShadowPageSize = 128`, so a
  2048² cascade is 16×16 = 256 pages).
- A page is **dirty** when any of these changed since it was last drawn:
  - the cascade's view-projection (camera moved or rotated enough to reshape it),
  - the light direction,
  - any instance whose projected bounds overlap the page moved, appeared, or
    disappeared.
- Instance movement is detected by hashing `(instance id, transform)` per frame
  and diffing against last frame's hashes. Only changed instances are projected
  into page space, so the per-frame cost is proportional to what moved, not to
  scene size.
- Drawing switches from `loadOp = CLEAR` over the whole cascade to
  `loadOp = LOAD` with a scissor per dirty page run. Fully clean cascades are
  skipped entirely.

**Why not sparse binding.** True VSM uses sparse residency so unused pages cost
no memory. That needs `sparseResidencyImage2D`, which is optional and absent on
plenty of hardware, and it would gate shadows on a device feature. Paging the
*work* rather than the *memory* gets the frame-time win — which is the one that
matters here — with no feature dependency. Recorded as the upgrade path.

**Correctness risk to watch.** The failure mode of a page cache is a stale page:
a shadow that should have moved but did not. The invalidation set above is
deliberately conservative — anything that cannot be proven unchanged is redrawn —
and a `SetShadows {invalidateCache: true}` escape hatch forces a full redraw so a
suspected stale page can be confirmed or ruled out from a tool call.

---

## Verification

Each stage ships only when driven live over MCP, as the previous blocks were:

| Stage | Check |
| --- | --- |
| 1 | A point light with `castShadows` reports 6 allocated tiles; occluders cast in all six directions; a second point light either gets 6 tiles or 0, never a partial cube. |
| 2 | More than 16 point lights in a scene all contribute light; `GetRenderStats` reports froxel occupancy and the maximum lights in any froxel. |
| 3 | A static scene reports ~0 dirty pages after the first frame; moving one object dirties only pages near it; `invalidateCache` forces every page dirty for one frame. |

`ctest` stays green, and the cascade-fitting and page-projection maths get unit
tests in `EngineCoreRenderPipelineTests` — both are pure functions of matrices
and bounds, so they are testable without a device.

---

## Removed on completion

- `Core/Renderer/VirtualShadows/VirtualShadowMapCache.*` — superseded by the page
  table; keeping a second, unused shadow-cache implementation beside a working
  one is the exact failure this repository has been unwinding.
- `Core/Renderer/ForwardPlus.*` — superseded by the clustered cull pass, unless
  the render-graph port later wants its pass-registration hook.
