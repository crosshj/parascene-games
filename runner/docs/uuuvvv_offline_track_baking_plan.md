# UUUVVV Runner — Offline Track Baking and LOD Plan

## Goal

Increase the visual and environmental complexity of UUUVVV Runner on PlayStation 1 hardware by exploiting its fixed tracks and predictable camera path. Move expensive *decisions and asset preparation* to an offline build pipeline, leaving the PS1 to perform the necessary gameplay, transformation, sorting, and drawing work.

**Non-goal:** Do not use distance-based object culling as the primary optimization. Distant scenery should remain present, with progressively simpler representations. Hardware clipping and sensible treatment of truly out-of-frustum geometry remain separate concerns.

## Core idea

Compile each authored track into compact, PS1-friendly data organized by **track distance**, not elapsed time or frame number. Generate multiple mesh LODs, baked lighting, track-segment data, and transition events offline. At runtime, use track progress to select precomputed data with little per-object decision-making.

This trades general-purpose engine flexibility for richer, more predictable scenes. It does **not** eliminate the cost of drawing polygons, VRAM limits, ordering-table work, or runtime animation and collision.

## 1. Geometry LOD

Use geometric **Level of Detail (LOD)**, initially discrete mesh variants rather than continuous runtime mesh simplification.

Example levels (tune using real content and profiling):

| LOD | Projected object height | Representation |
| --- | --- | --- |
| High | >= 64 px | Full mesh |
| Medium | 24–63 px | Reduced mesh |
| Low | 8–23 px | Aggressively simplified mesh |
| Minimal | < 8 px | Silhouette-preserving tiny mesh or appropriate impostor |

These are starting thresholds, **not PS1 limits**. Small objects should retain recognizable silhouettes; never assume an arbitrary 1–2-triangle approximation is sufficient.

Estimate screen-space height for a perspective camera:

```text
projectedHeightPx ~= objectHeightWorld * projectionDistance / cameraSpaceDepth
```

On PS1, the GTE's geometry-screen/projection-distance setting supplies the projection scale. Camera-space depth matters: raw Euclidean distance is not an equivalent substitute. For objects with large depth extent, use a bounding sphere/box or a conservative projected bound rather than only the center depth.

For fixed camera paths, compute the projected bounds and LOD switch points **offline**. Store only transitions rather than recalculating size for every object every frame. Add hysteresis or deliberately separated enter/exit thresholds to prevent flickering when the camera or player can move backward or sideways. If traversal is strictly forward, a sorted transition cursor is especially cheap.

## 2. What to bake offline

| Area | Bake-time work | Remaining runtime work |
| --- | --- | --- |
| Geometry | Simplified mesh variants, vertex quantization, batching | GTE transforms, polygon submission and rasterization |
| LOD | Projected-size sampling, transition distances | Apply transitions; handle camera deviations |
| Lighting | Static vertex colors, palette/texture lighting | Dynamic characters/effects and any changing lights |
| Static shadows | Shadow textures or geometry | Draw them; dynamic shadows separately |
| Camera | Sampled spline positions/orientations | Interpolate and accommodate player/camera offsets |
| Scene organization | Segment-local object lists, material/texture grouping | Build/submit draw commands and maintain correct ordering |
| Depth ordering | Static hints or candidate order | Resolve overlaps, transparency and moving geometry as needed |
| Collision | Simplified boundaries, normals, broad-phase indexes | Player/object collision tests and response |
| Animation | Sampled paths, keyframes, repeating effect curves | Interpolation, transforms, draw calls |
| Effects | Spawn schedules, particle paths, palette/color curves | Simulate or play back, render and react to gameplay |
| Gameplay | Trigger coordinates, obstacle schedules | Input, dynamic state, trigger evaluation |
| Audio | Cue locations, scripted synchronization | Playback and state-dependent triggering |
| Streaming | Segment layout, compression, dependencies | CD reads, decompression, memory management |

**Caution:** Static depth order is not generally sufficient on the PS1: the GPU lacks a conventional z-buffer, and dynamic camera/object relationships may require runtime ordering. Texture atlasing can help batching but must respect texture-page, CLUT, VRAM, and primitive constraints.

## 3. Recommended architecture

```text
Authoring assets
  meshes / textures / lights / camera spline / gameplay markers
                |
                v
Offline track compiler (host machine)
  mesh optimization + LOD generation
  lighting bake
  camera-path sampling and LOD transition generation
  segmentation + object/trigger indexing
  texture/geometry packing + optional compression
                |
                v
Versioned PS1 track package
  header / segment directory / mesh variants / transitions
  baked colors / texture references / triggers / dependencies
                |
                v
PS1 runtime
  track-distance lookup
  segment management / optional streaming
  LOD transition processing
  input / gameplay / collision / animation
  GTE transforms + ordering table + GPU rendering
```

**Prefer build-time compilation over calculations during track load.** Track-load-time preprocessing is still possible, but it costs startup time and precious PS1 RAM. Use the host build machine for expensive work whenever assets are known ahead of time.

## 4. Track-coordinate data model

Use a monotonic path coordinate (`trackDistance`) independent of player speed. Avoid baking by frame: acceleration, lane changes, pauses, and camera offsets must still work.

Illustrative format only (not a final binary ABI):

```c
typedef struct {
	int32_t startDistance;
	int32_t endDistance;
	uint16_t geometryBatch;
	uint16_t lightingState;
	uint16_t eventStart;
	uint16_t eventCount;
} TrackSegment;

typedef struct {
	int32_t trackDistance;
	uint16_t objectId;
	uint8_t lod;
} LodTransition;
```

Store LOD transitions sorted by distance. A forward-moving player can process them using a cursor; seeking backward or restarting should rebuild the active state via binary search or a segment snapshot. Separate *scene segment* boundaries from *LOD transition* boundaries: objects may span many segments.

## 5. Constraints to design around

- **PS1 main RAM:** 2 MB; **VRAM:** 1 MB. Keep resident assets and transition tables compact.
- Precomputation reduces CPU decision overhead, but **does not make rendering free**. Geometry transforms, ordering-table management, GPU draw work and fill rate remain real costs.
- A fixed track does not necessarily mean a fixed camera. Document camera follow, lane offsets, camera shake, jump height, and scripted rotations. These determine how exact precomputed LOD transitions can be.
- Maintain distant objects as requested, but do not waste work submitting geometry that cannot physically appear in the camera view. Distinguish frustum clipping from the unwanted distance-fade/disappearance policy.
- CD streaming is possible, but seek latency and throughput demand prefetching, contiguous layout where practical, and fallback behavior. Do not make streaming a prerequisite for the first milestone.
- Aggressive geometry simplification can cause popping and silhouette loss. Test at native output resolution and actual game speed.
- Avoid storing full camera-relative geometry for every sample initially; it may increase asset size dramatically and limit camera freedom.

## 6. Implementation roadmap for coding agent

### Phase 0 — Inspect and measure before changing architecture

1. Locate the current track representation, camera spline/follow logic, mesh pipeline, renderer, and build tools.
2. Document how camera position/orientation depends on track distance, lane, jumping, shake and gameplay.
3. Establish a repeatable benchmark scene and record frame time, visible/submitted polygons, CPU transform/sort time if measurable, GPU pressure, RAM and VRAM use.
4. Identify whether the primary bottleneck is CPU, GPU/fill rate, memory, or I/O. Preserve a baseline build.

**Deliverable:** Short architecture inventory and baseline metrics. Do not assume the current engine already has any particular data format.

### Phase 1 — Minimal offline LOD pipeline

1. Choose one representative static prop (e.g., tree, rock, structure).
2. Generate or author 3–4 simplified meshes, preserving silhouette and material compatibility.
3. Build a host-side utility to sample the actual track camera and estimate each prop's projected bounds along track distance.
4. Produce sorted LOD transition events and a compact asset manifest.
5. Implement runtime LOD selection using transitions, with safe reset/reverse behavior and a fallback for camera offsets.
6. Compare against the original scene at multiple speeds and camera positions. No distance-based disappearance.

**Acceptance:** Same prop remains visible at long range; transitions are stable; measured runtime overhead is lower than per-frame projected-size evaluation; memory cost is reported.

### Phase 2 — Static lighting bake

1. Bake lighting into vertex colors or suitable textures for static environment meshes.
2. Verify output on the actual PS1 renderer, including color precision and texture/CLUT behavior.
3. Keep player, enemies, and reactive effects on a separate dynamic-lighting path as needed.

**Acceptance:** Improved environment appearance without per-frame static-light evaluation; quantified asset-size and runtime tradeoffs.

### Phase 3 — Track segmentation and compiled scene data

1. Divide the track into configurable distance intervals, allowing objects to cross boundaries.
2. Emit segment-local references, object bounds, trigger indexes, geometry/material groups, and asset dependencies.
3. Introduce a versioned binary package with validation and debug dumps.
4. At runtime, use the current segment to locate scene data efficiently, while preserving correct ordering and dynamic objects.

**Acceptance:** Equivalent gameplay/visual output with reduced scene-management work and documented package sizes.

### Phase 4 — Packing and streaming (only if justified)

1. Optimize geometry storage and texture-page/CLUT allocation.
2. Measure resident memory and worst-case segment working set.
3. If necessary, add segment prefetching, optional compression and robust streaming diagnostics.
4. Benchmark actual disc-image behavior, including worst-case seeks and fast player traversal.

**Acceptance:** No visible stalls at target speed; bounded RAM/VRAM use; measured level-size increase.

### Phase 5 — Higher-order baked effects

Evaluate individually: static shadow decals, scripted machinery animation, particle/effect schedules, palette transitions, precomputed collision indexes, and audio cues. Add only where profiling or visible quality justifies complexity.

## 7. Tests and validation

- Unit-test projected-size calculations and LOD transition generation against independently projected sample bounds.
- Test forward progression, reverse/rewind if supported, restart, teleport/seek, varying speed, and camera offsets.
- Validate transitions are ordered, object IDs exist, meshes/materials are available, and segments cover the full track.
- Verify no distance-only culling was introduced.
- Test worst-case native-resolution screenshots/video for LOD popping, silhouette readability, polygon cracks, texture warping, and depth-sort artifacts.
- Measure CPU frame time, polygon counts, RAM/VRAM, package size, and load/stream time before and after each phase.
- Keep an A/B toggle for compiled vs. existing behavior during migration.

## 8. Instructions to the coding agent

> Inspect the existing UUUVVV Runner repository and its PS1 toolchain before proposing code changes. Implement **Phase 0 first**, then propose the smallest viable **Phase 1 offline LOD prototype** that fits the existing build and asset pipeline. Do not rewrite the renderer or introduce a general-purpose scene engine. Preserve distant scenery rather than distance-culling it. Prefer host-side asset compilation, track-distance-indexed transitions, simple PS1 runtime structures, early returns, and focused tests. Explicitly identify camera-motion assumptions and memory/performance tradeoffs. Report baseline measurements and expected improvements before proceeding to subsequent phases.

## Definition of success

The same PS1 hardware displays richer, more densely authored fixed-track worlds at the target frame rate, with persistent distant scenery, stable LOD transitions, manageable memory use, and an asset pipeline that scales to additional levels without extensive runtime complexity.
