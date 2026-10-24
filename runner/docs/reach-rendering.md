# Vesper Reach: far track and scenery

Vesper Reach's compiled road and canyon use the shared
[offline scene compiler](compiled-scene-rendering.md). The manifest supplies
the cross-section shape; runtime consumes distance-indexed draw lists and
shared vertices. Compiled stages use a 64,000-unit distant view and converge
to the same horizon color. Stages without a package retain the procedural path
and its previous view range. Screen-space ruins remain separate scenery.

## Atmosphere and composed sightlines

Reach separates `Light.haze` (the atmospheric tint at intermediate distances)
from `RoadStyle.horizon` (the common extinction color at and beyond 20,000
depth units). The latter is optional so other worlds retain their palette.
Fully distant road, shoulders, ground and canyon use the same horizon accessor,
including the single-face fully fogged optimization and cached color tables.
The sky reaches that color at the horizon. Mountain and ruin bases also reach
it, while their tops retain low contrast through inexpensive Gouraud shading.
There are no extra transparent layers or per-pixel runtime fog calculations.

The old implementation blended Ridge's mountains only 14% toward a bright haze
and its ruins only 7%, while distant surfaces converged completely. It could
pass procedural-versus-compiled comparisons because both paths shared that
art-direction error. The new check tests the intended appearance independently:

```sh
sh runner/tools/preview/preview.sh runner/build/reach-atmosphere --atmosphere-check
python3 runner/tools/preview/atmosphere_report.py runner/build/reach-atmosphere
```

Across 535 poses and forward/reverse stage changes it checks actual submitted
sky/backdrop/ruin bottom colors against the clear horizon, limits distant
landmark contrast, checks road/scenery shaders and cached tables at four far
depths, and checks buffer/draw-list exhaustion. It saves route images every
8,192 units. This runs in `check_world_visuals`, alongside mesh and shared
renderer comparisons. These contracts prevent the known mismatch; image
review remains necessary for composition and any new atmospheric treatment.

Ridge now alternates gentle turning approaches to its two summits and returns
through the valley. Climb counter-turns on the run after its banked ascent.
Drop sweeps along its long approach before the existing banked plunge.
Lengths, grades, banks, obstacle spacing and finish locations are preserved.
The horizon ruins wrap continuously as the heading changes. The scene compiler
rebakes the actual routes and still verifies geometry and conservative
out-of-view merges; a turn is not assumed to provide occlusion by itself.

The revised packages occupy 152,958 bytes for canyon geometry, 152,381 for the
road, and 69,328 for all-track path preparation. All remain within their budgets.
The new Gouraud backdrop uses more primitive-buffer space; the route changes
are not claimed as a measured speedup. Emulator qualification for this revision
is pending because an existing user DuckStation session was left running.

The latest host sweeps passed 3,825 mesh image comparisons (maximum mean channel
delta 0.053299/255, 19 residual high-contrast pixels beyond one-pixel matching),
5,067 exact shared-renderer comparisons, and over 16 million independent
geometric LOD samples. Before/after and route-sweep images are collected in
`build/reach-atmosphere/atmosphere-review.html`.

The generic road-streak overlay was removed from Reach. On its pitched tracks,
some streaks projected above the road into the sky. They added work without
helping the distant road read continuously. Native-size previews now show the
road and canyon meeting at the horizon without those stray lines.

The first Ridge route now forms a real rounded rise and descent. Its old
profile climbed to a 22.5-degree grade, held that uphill pitch through the
piece named `crest`, then eased back to level without ever descending. The
crest now changes pitch from +22.5 to -22.5 degrees, and the following piece
returns it to level. The road reaches a summit and comes back down instead of
forming a distant incline that grows into a wall.

Reach's repeated crossings now use the same `road_gantry()` geometry as the
other levels. That replaces the old custom wall sheets and lintel, which gave
the supports and beam mismatched dimensions and sorting. The standard gantry
uses one warm Reach color for posts and beam. The edge-stone schedule also
starts ahead of the camera, avoiding near/behind-camera projections.

## Visibility sweep

The host preview can sweep a non-default track using `RUNNER_PREVIEW_TRACK`:

```sh
RUNNER_PREVIEW_LEVEL='VESPER REACH' \
  sh runner/tools/preview/preview.sh /tmp/reach-visibility
RUNNER_PREVIEW_TRACK=reach.ridge \
  /tmp/reach-visibility/preview /tmp/reach-visibility --visibility 256
```

The sweep follows each stage's actual track and records visible pixels for
ruins, canyon sheets, gantries, and edge stones. With a 256-unit camera step,
the canyon sheets contributed only 7,112 and 7,525 visible pixels over 433
poses on Ridge, but 83,373 on Climb and 158,466 on Drop. They remain authored
because they materially appear on the latter routes. All eight horizon ruins
contribute visible pixels throughout the sweep. Bridge and edge-stone instances
also show visible contributions, so no scenery was removed based on this
sample.

The sweep is sampled evidence, not proof for every camera pose. It supports
future distance LOD work, but does not yet generate an authoritative visibility
schedule. Native-resolution previews were checked at multiple distances across
the first Ridge route, including the summit, the beam approach, and the descent.
The track baker and all three console builds must still accept any geometry
profile change before it is considered complete.
