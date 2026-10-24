# Compiled corridor rendering

The active mesh path extends the existing `bake_worlds.py` pipeline. It does
not reconstruct distant walls from path samples every frame.

The Reach road and canyon manifests cover Ridge, Climb, and Drop.

A manifest describes six cross-section vertices as `[side, outside, height]`,
LOD distances/strides, a screen-error tolerance, near transition distance,
lookahead, and memory/work limits. The host sampler uses the actual fixed-point
path implementation. The compiler emits world-space rings, shared local
vertex offsets, and a draw-list snapshot for each 256-unit progress interval.
The canyon and road manifests use the same compiler and package format.

Runtime looks up the progress snapshot, transforms each selected ring once,
shares projected vertices across adjoining faces, applies haze, and submits
primitives to the ordering table. Nearby geometry retains the existing clipping
path. The near/far seam uses one exact cross-section from the selected snapshot.
The compiler bounds the camera over every integer progress position in a band.
It merges out-of-view spans only when all fine-mesh vertices share an outside
clipping half-space throughout that band. Visible geometry still uses the
projected chord-error test. No distance-only removal is used.

No event cursor must be reconstructed after restart or backward seeking.

Only tracks with a compiled package receive the extended 64,000-unit view.
Other tracks keep the previous 20,000-depth / 44,000-progress limits. Sineworks
keeps its analytical tunnel and infinite road tip; the finite road stops before
it overwrites that tip. The discarded per-frame geometric-error LOD selector
and its large tables have been removed.

Separately, `bake_path_runtime.py` compiles frames, branch/cut/obstacle indexes,
and curb tapers for all 27 runnable tracks. The resident package is 62,068 bytes.
Track opening selects immutable data instead of rebuilding it. This is path
preparation coverage, **not** a claim that every world's scenery is mesh-baked.

## Validation

- `cmake --build runner/build --target check_worlds`: package budgets, path
  equivalence across routes/seeks, exact world vertices, and independent
  between-sample camera/geometry checks of the emitted LOD chords.
- `cmake --build runner/build --target check_world_visuals`: native mesh A/B
  images, the all-stage shared-renderer sweep, and tunnel, city, countdown,
  visibility and motion checks. Mesh image limits are mean channel delta 0.15
  and at most 0.1% of native pixels with high-contrast changes that cannot be matched symmetrically
  within one pixel of the reference (8 channel units for 15-bit shading). Raw
  high-contrast differences are also recorded. This tests one-pixel silhouette
  agreement rather than penalizing a long edge simply for moving one pixel. The worst pair is saved for review.
- `tools/profile_stage.py`: builds the disc, verifies a unique ROM run ID and
  exact stage/scroll, records disc/source/package hashes and the console report,
  then closes only the emulator it launched. Profiling counts actual presentation
  intervals and missed two-vblank deadlines, not only average CPU work.
  It preserves retail CPU/GPU overlap; the GPU phase measures the existing
  wait before presentation, never an extra wait after submission.

Example full Ridge run (two updates and 40 progress units per rendered frame):

```sh
python3 runner/tools/profile_stage.py --track reach.ridge --level 3 --stage 0 \
  --frames 2700 --timeout 180 --output runner/build/metrics/reach0-qualified.json
```

`tools/check_profile.py` compares matched runs and rejects missed 30 fps
presentation deadlines, less than 5% average CPU improvement, or less than
15% worst-frame level-draw headroom. It writes a machine-readable result;
unmeasured stages are not performance-qualified.

Use `--reference` and a separate build folder for the original procedural
surfaces, path preparation, and shorter distance range. Profile files identify
which mode ran. CPU/GPU timings are from DuckStation's emulated console clocks;
these are not host preview work-count estimates or real-hardware measurements.

## Supported geometry and limits

The current scene compiler accepts continuous, finite, unbranched corridors.
It explicitly rejects loops, alternate chains and cuts in a declared package.
The path-preparation compiler supports those features, but that does not make
the scene scheduler safe for them. Unsupported scenery retains its renderer;
there is no claim of an automatic bake of arbitrary procedural level code.

The camera follows track yaw and three quarters of pitch; lane switching and
jumping do not translate it. Branch camera easing needs separate route-aware
schedules before those tracks can use this scene package. The geometric LOD
checks sample intermediate positions independently of the compiler; they are
strong regression coverage, not a continuous mathematical proof for every
possible camera configuration. Runtime sorting remains necessary on PS1.

## Measured Ridge comparison

Matched 2,700-frame runs with normal CPU/GPU overlap:

| Measurement | Procedural reference | Compiled |
| --- | ---: | ---: |
| Average level CPU cycles | 1,217,369 | 819,656 |
| Worst level CPU cycles | 1,465,512 | 923,208 |
| Measured presentation intervals | 2,698 | 2,698 |
| Missed 30 Hz deadlines | 2,294 | 0 |
| Average presentation rate | 21.05 fps | 30.00 fps |

The reference uses the original shorter distance range. The compiled view is
longer while reducing average level CPU work by 32.7%. These are Ridge results,
not an extrapolation to unmeasured worlds. The paired result is recorded in
`build/metrics/reach0-acceptance.json` beneath the runner directory.

Earlier profiling reports labelled “GPU drawing” forced a wait after command
submission and serialized CPU/GPU work. They are obsolete for presentation
qualification; `check_profile.py` rejects them. Current reports preserve the
retail overlap and measure only the existing GPU wait before presentation.

## Reach traversal results

These timings precede the atmosphere/sightline revision documented in
[Reach rendering](reach-rendering.md). They are retained as the previous
baseline, not performance qualification for the revised routes and backdrop.

Each run renders 2,700 frames and records 2,698 presentation intervals. These
are measured traversals, not a claim about every possible gameplay state.

| Stage | Average level cycles | Worst level cycles | Missed deadlines | Rate |
| --- | ---: | ---: | ---: | ---: |
| Ridge | 819,656 | 923,208 | 0 | 30 fps |
| Climb | 816,295 | 942,576 | 0 | 30 fps |
| Drop | 787,618 | 961,944 | 0 | 30 fps |

Sources are `reach0-retail-pipeline.json`, `reach1-retail-pipeline.json`, and
`reach2-qualified.json` in `build/metrics`. Ridge and Climb precede the final
shared projection and fully-fogged surface reductions; Drop includes them.
Only Ridge has a matched procedural timing reference. Drop meets the measured
30 fps target but **fails** the additional 15% worst-frame CPU-headroom gate
(14.8% available); its acceptance report preserves that failure.

The final mesh sweep compares 3,825 camera poses: worst mean channel delta
0.121580/255 and 39 residual high-contrast pixels beyond one-pixel matching.
Native reference/candidate pairs are in `build/world-visuals/reach-review.html`.
Fully fogged road, shoulders and ground share one opaque face because their
colors have converged, retaining the outer silhouette with fewer primitives.
