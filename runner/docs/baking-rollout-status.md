# Baking coverage and remaining work

Status as of October 23, 2026. The foundation is implemented, but automatic
scenery baking with verified visual and performance improvements across every
world is not complete.

## Current coverage

| Scope | Implemented |
| --- | --- |
| All 27 runnable tracks | Baked path preparation: frames, branch/cut/obstacle indexes, and curb tapers. Track opening selects immutable data instead of rebuilding it. |
| Vesper Reach: Ridge, Climb, Drop | Shared compiled road/canyon geometry, visibility and LOD schedules, and an extended distance range. |
| Sineworks | Specialized analytical tunnel renderer and infinite road tip. |
| Vesper City | Specialized optimized building and window renderer. |
| Remaining scenery | Existing renderers; shared path preparation does not mean scenery is mesh-baked. |

Sineworks and Vesper City already benefit from optimization. Migrating them to
the shared compiler is not inherently an improvement; retain their specialized
paths unless a replacement demonstrates better results.

## Remaining work

1. **Represent the remaining worlds' static scenery for the compiler.**
   Supply geometry/material descriptions or adapters that let build time replace
   repeated runtime construction. The compiler does not automatically convert
   arbitrary procedural level code into baked scenery.
2. **Support the remaining track topologies.** The scene compiler currently
   accepts continuous, finite, unbranched corridors. Loops, alternate chains,
   and cuts need appropriate scheduling, including route-aware camera bounds
   for branch easing. The path-preparation compiler already supports these
   features; the scene compiler does not.
3. **Make distance rendering visually coherent in each migrated world.**
   Validate road/scenery continuity, near/far seams, silhouettes, visibility,
   LOD changes, and atmosphere convergence with the backdrop. Geometry merely
   existing in a distant draw list is not proof that it renders correctly.
4. **Qualify every migrated stage.** Compare native-resolution images and
   motion across the track, then measure matched emulator runs with the exact
   stage and build identified. Verify presentation deadlines and CPU headroom,
   not just host work counts or average frame rate. Include load-time evidence
   when claiming faster stage loading.
5. **Requalify Reach after its later atmosphere and sightline changes.**
   Earlier traversals reached 30 fps, but those measurements precede the revised
   routes/backdrop. Only Ridge has a matched procedural timing reference; Drop's
   earlier result also missed the additional 15% worst-frame headroom gate.
   Do not treat these historical timings as qualification of the current build.

## Completion criterion

Each stage must have an explicit rendering strategy and recorded evidence that
its optimized build preserves the intended appearance, handles distant geometry
coherently, fits memory/work budgets, and meets the performance gates. Unsupported
or unmeasured stages must remain visibly identified in coverage reports rather
than being counted as fully baked because their path data is compiled.

The remaining task is to generalize and roll out the working scenery pipeline,
not rebuild the foundation or force every world through the same renderer.

## References

- [Shared scene compiler, limits, validation, and historical measurements](compiled-scene-rendering.md)
- [Original offline baking plan](uuuvvv_offline_track_baking_plan.md)
- [Sineworks rendering](sineworks-rendering.md)
- [Vesper City rendering](vesper-rendering.md)
- [Reach atmosphere and sightlines](reach-rendering.md)
