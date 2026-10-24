# Baking coverage and remaining work

Status as of October 24, 2026. The shared scene compiler now covers two corridor
worlds and supports the route topology used by Meridian. Full rollout and matched
emulator qualification across every world are still in progress.

## Current coverage

| Scope | Implemented |
| --- | --- |
| All 27 runnable tracks | Baked path preparation: frames, branch/cut/obstacle indexes, and curb tapers. Track opening selects immutable data instead of rebuilding it. |
| Vesper Reach: Ridge, Climb, Drop | Shared compiled road/canyon geometry, visibility and LOD schedules, and an extended distance range. |
| Pale Meridian: Drift, Fork, Verge | Shared compiled ice-wall geometry, route-specific schedules, continuous fork boundaries and ground underlays, and inner islands located from actual wall-contour intersections. |
| Sineworks | Specialized analytical tunnel renderer and infinite road tip. |
| Vesper City | Specialized optimized building and window renderer. |
| Lumenwild, Shatter, Cathedral, arcade scenery | Existing world-specific renderers; shared path preparation does not mean scenery is mesh-baked. |

Sineworks and Vesper City already benefit from optimization. Migrating them to
the shared compiler is not inherently an improvement; retain their specialized
paths unless a replacement demonstrates better results.

## Completed in this rollout pass

- The compiler emits branch-specific, finite route schedules and marks cuts so
  LOD spans never bridge a disconnected path section. It supports up to three
  branch decisions per track; loops and unbounded tracks remain unsupported.
- Meridian uses the shared package on all three stages, with both Fork and
  Verge route masks. The package is 155,588 resident bytes (220,000-byte cap),
  with a 44,032-unit lookahead and a 1.4 px sampled compiler error target.
- Independent runtime-camera checks covered 9.33 million Meridian visible
  chord samples; worst error was 1.446 px against the 1.5 px verifier limit.
  Baked path data matches all 27 tracks, branch masks, reverse seeks, and band
  edges exactly.
- Both fork stages now have continuous native traversal checks through both
  branches, splits and joins: 2,140 frames, zero foreground sky holes and zero
  scenery-budget omissions. These are separate from the older sparse A/B tests,
  which could agree with a broken baseline. See [fork validation](meridian-fork-rendering.md).
- Current mesh A/B checks cover 25 Meridian poses and 3,825 Reach poses. Meridian's
  worst mean channel delta is 0.019340/255, with at most 8 unmatched pixels;
  Reach's worst mean is 0.053194/255, with at most 19 unmatched pixels.
- The all-stage host pipeline checked 5,067 frame pairs with no remaining
  failures. Reach/Meridian mesh deltas are counted and reviewed by their native
  image checks instead of being incorrectly required to be pixel-identical to
  the old tessellation. Path equivalence remains exact for the other tracks.
- The console ISO build passed for music, no-music, and web variants. The
  latest largest image+BSS was 1,725,580 bytes, leaving 306,036 bytes free
  above the image; the existing 256 KB remains reserved.

After removing banking from Vesper Hook's two bends, the compiled path package
is 68,980 resident bytes. That track edit retains yaw curves and sets roll to
zero on all 456 generated segments; it is not additional scenery-bake coverage.

The host work counts show the practical CPU-side potential, not a frame-rate
claim: Reach sampled surfaces fall from 512–583 to 64–66 per frame; Meridian
falls from 480–902 to 127–348, depending on stage. Matched DuckStation timing
has not been collected for this build.

## Remaining work

1. **Represent the remaining worlds' static scenery for the compiler or an
   explicit build-time renderer.**
   Supply geometry/material descriptions or adapters that let build time replace
   repeated runtime construction. Lumenwild growth, Shatter fragments,
   Cathedral slivers, and Vesper's animated skyline need world-specific bake
   formats or retained specialized renderers with measured build-time assets.
   Do not force these assets into a corridor mesh that cannot represent them.
2. **Finish topology qualification.** Finite branches and cut boundaries are
   implemented and Meridian exercises them. Fixtures cover consecutive cuts
   and the full eight-route cap. Branch-easing bounds still need a dedicated
   adversarial fixture. Looped or unbounded tracks need a separate
   bounded-window schedule design.
3. **Make distance rendering visually coherent in each migrated world.**
   Validate road/scenery continuity, near/far seams, silhouettes, visibility,
   LOD changes, and atmosphere convergence with the backdrop. Geometry merely
   existing in a distant draw list is not proof that it renders correctly.
4. **Qualify every migrated stage.** Compare native-resolution images and
   motion across the track, then measure matched emulator runs with the exact
   stage and build identified. Verify presentation deadlines and CPU headroom,
   not just host work counts or average frame rate. Reach and Meridian have
   host visual checks but still need matched emulator timing. Sineworks and
   Vesper City need current per-stage performance and loading records before
   their specialized optimizations count as qualified. Include load-time
   evidence when claiming faster stage loading.
5. **Requalify Reach after its later atmosphere and sightline changes.**
   Its current native-size host image suite passes after the atmosphere changes.
   Current matched emulator performance data remains unrecorded. Earlier 30 fps
   traversals predate the revised routes/backdrop; Drop also missed the 15%
   worst-frame headroom gate in that earlier run.

## Completion criterion

Each stage must have an explicit rendering strategy and recorded evidence that
its optimized build preserves the intended appearance, handles distant geometry
coherently, fits memory/work budgets, and meets the performance gates. Unsupported
or unmeasured stages must remain visibly identified in coverage reports rather
than being counted as fully baked because their path data is compiled.

The next engineering work is to create explicit build-time asset adapters for
the remaining world-specific scenery, then qualify each renderer with the same
image, work-budget, and emulator evidence. Path data being compiled alone is
not scenery bake coverage.

## References

- [Shared scene compiler, limits, validation, and historical measurements](compiled-scene-rendering.md)
- [Original offline baking plan](uuuvvv_offline_track_baking_plan.md)
- [Sineworks rendering](sineworks-rendering.md)
- [Vesper City rendering](vesper-rendering.md)
- [Reach atmosphere and sightlines](reach-rendering.md)
