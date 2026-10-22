# Sineworks: straight tunnel, infinite road and visibility analysis

The three Sineworks tracks are finite, centered, flat, unbranched roads. Their
lane widths change, but the camera travels straight ahead. The track compiler
now verifies those facts and emits a `straight` trait. Curves, hills, banks,
branches, lateral displacement and loops reject this fast path and retain the
procedural renderer.

## Active renderer

The tunnel is a fixed-width rectangular enclosure. Road narrowing happens
inside it, with floor outside the deck. Six screen-space Gouraud faces cover
the whole screen and meet at `(160,72)`. They require no path sampling, GTE
projection or camera transformation. There is no finite end-cap polygon.
Walls, ceiling, floor, clear color and road fog converge to black.

The detailed road retains its gameplay geometry. An additional shaded
continuation reaches its vanishing point and finishes with one native pixel
of width. This avoids a zero-area tip disappearing during rasterization.
It uses the same fog color as the enclosure.

Ribs repeat out to 20,000 units instead of appearing only within 2,200. Nearby
ribs use three surfaces; farther ribs use three outline lines once their
depth thickness is below a pixel. Their brightness fades continuously with
depth. A one-pixel representation is available when the entire projected
silhouette fits one pixel; the current rib size/range does not reach that case.
Whole near arches proven outside the viewport are skipped before submission.

The previous sampled-wall mesh and its approximately 150 KB of resident data
are no longer linked. Track-derived eligibility plus this specialized renderer
cost about 2.7 KB above the original executable's loadable image/BSS.

## Measurements

Same 3,600-frame Sineworks stage-0 sweep, identical native-color host renderer:

| Work per frame | Original | Current |
|---|---:|---:|
| Path samples, average | 675 | 75 |
| Camera transforms, average | 723 | 122 |
| GTE projections, average | 991 | 754 |
| Primitive bytes, average | 11,146 | 9,528 |
| Primitive bytes, worst | 11,724 | 10,152 |
| Software fill pixels, average | 107,319 | 122,068 |

This reduces samples by 89%, transforms by 83%, projections by 24% and command
bytes by 15%. Filling the complete enclosure increases pixel work by 14%.
These are measured work counts, not measured PlayStation frame times. The
profiling disc includes a `straight tunnel` phase for console timing.

The current music executable's loadable image plus BSS is 1,221,692 bytes,
leaving 809,924 bytes above it. Every executable is checked against retail
2 MB RAM with an upper 256 KB stack/heap reserve.

## Host validation

```sh
cmake --build runner/build --target check_worlds
cmake --build runner/build --target check_world_visuals
cmake --build runner/build --target world_visibility
cmake --build runner/build
```

The unit checks cover eligibility rejection for curves/branches/hills,
projection scale, geometric simplification error, visibility report intervals
and memory limits. The renderer self-tests check opaque occlusion, transparent
contributions, off-screen objects, exactly one pixel for a 1×1 primitive, and
single blending along shared triangle edges. The host rasterizer now applies
the top-left edge rule instead of drawing shared edges twice.

Thirty native 15-bit scene checks across all three routes verify:

- Six enclosure faces cover all 76,800 pixels, without uncovered end caps.
- Wall intensity decreases toward the common black vanishing point.
- The road continuation contributes at the vanishing-point pixel.
- Distant ribs survive normal sorting and rasterization.

An additional 5,196 identical-state frame pairs provide original/current
images for review. The new fixed-width enclosure and black fog intentionally
change the scene, so pixel equality with the old design is not the criterion.
The report is `runner/build/world-visuals/index.html`.

## Visibility simulation

`world_asset(kind, instance, representation)` supplies stable scenery IDs to
the host renderer and compiles away on the console. Per-pixel contributor
masks track which objects remain after painter-order rasterization. Opaque
draws replace hidden contributors; transparent draws conservatively retain
them. Reports distinguish off-screen/degenerate geometry, opaque occlusion,
visible pixel counts, LOD choices and distance intervals suitable for review.

The first native sweep covers all three Sineworks routes at 128-unit spacing:
2,595 camera poses and 186 stable assets. It found six never-observed rib
instances for review. It also caught the conduits being submitted below the
walls; fixing their layer made them visible. No current rib qualifies for a
one-pixel replacement over the tested range; the report says so explicitly.

The output is `runner/build/visibility/index.html`, with JSON intervals and
the complete object/pose CSV. Other worlds can use the same tagging API, but
their individual props have not yet been instrumented. This report covers
Sineworks, not every authored asset in the game.

Sparse sampling is evidence, not proof of invisibility at every possible
camera pose. Never-observed candidates are not automatically deleted. After
building the host preview, `preview output-dir --visibility 1` checks every
integer distance along these routes at the chosen animation schedule. Routes,
camera envelopes and animation states must be accounted for before applying
sampled schedules as authoritative culling data.
