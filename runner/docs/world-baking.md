# World baking: inventory, baseline and first prototype

**Historical prototype.** The active shared mesh pipeline is documented in
[compiled-scene-rendering.md](compiled-scene-rendering.md).

**Original prototype notes.** The active Sineworks implementation and current
measurements are documented in [sineworks-rendering.md](sineworks-rendering.md).
The original Sineworks sampled package below is no longer linked into the game. Its host
compiler remains available for experimentation with genuinely complex geometry.

This implements the first milestone of
[the offline baking plan](uuuvvv_offline_track_baking_plan.md): an architecture
inventory, repeatable baseline, and a resident Sineworks far-wall LOD prototype.
Other worlds retain their procedural scenery. Animated ribs and conduits remain
on their existing path; this is not a general mesh importer or streaming system.

## Architecture and camera assumptions

- `tracks/catalog.json` is compiled by `tools/bake_tracks.py` through CMake into
  `src/track_data.h`. Track coordinates are distance-based, in 256-unit bands.
- `path.c` interpolates baked positions and deck frames. It still constructs
  segment frames and route/cut indexes when opening a track. Moving these
  track-wide structures offline is a separate optimization.
- `path_begin()` follows track position and yaw, takes three quarters of pitch,
  and ignores roll. Player lane changes and jumping do not move the camera.
  Branch joins can move the centerline sideways; a stateful `cam_u` eases the
  camera back. Distance alone therefore does not describe every track camera.
- `road.c` draws the near deck to depth 4,200 and a simpler far deck to camera
  depth 20,000, walking at most about 44,000 units of track. Procedural world
  scenery uses repeated path samples and recursive sheet subdivision.
- `world.c` submits flat/Gouraud primitives through a 512-entry ordering table
  and two 32 KB primitive buffers. There is no conventional depth buffer.
  Static world depth order cannot simply replace runtime sorting/clipping.
- `RUNNER_PROFILE` already measures console CPU phases and GPU synchronization.
  The preview counts projections, command bytes and software-rasterized fill;
  it now also counts path samples and camera transforms. These counts are not
  console cycle or frame-time measurements.

The prototype accepts only finite, continuous, unbranched tracks. Its camera
assumptions match all three Sineworks tracks. New branches/discontinuities fail
the bake instead of silently producing an invalid package. Runtime track IDs
without a package, negative progress, or progress beyond the compiled schedules
use the old renderer. Pause, restart, speed changes and backward seeking use
direct schedule lookup; no transition cursor needs reconstruction.

## Baseline and expected improvement

The baseline was captured before enabling compiled walls, preserving the old
executable at `/tmp/runner-bake-baseline.elf` during development. Its loadable
image plus BSS uses 1,218,968 bytes. No additional world textures or VRAM are
required by this prototype; character and existing scenery VRAM are unchanged.

The expected saving is fewer repeated path samples, camera rotations and
recursive geometry decisions. GPU work is not expected to fall proportionally:
the same walls still cover the screen. Additional resident geometry/schedules
trade RAM for CPU work. No console timing baseline was captured, so the
primary hardware bottleneck and final frame-rate improvement remain unproven.

Measured host preview, identical 3,600-frame stage-0 sweep, speed 20:

| Per-frame measure | Procedural | Compiled |
|---|---:|---:|
| Path samples, average | 675 | 549 |
| Camera transforms, average | 723 | 626 |
| GTE projections, average | 991 | 985 |
| GTE projections, worst | 1,048 | 1,044 |
| Primitive bytes, average | 11,146 | 11,843 |
| Primitive bytes, worst | 11,724 | 12,432 |
| Software fill pixels, average | 112,764 | 113,017 |

After the visual consistency correction, path samples fall 19%, transforms
13%, and projections less than 1%. Primitive bytes increase about 6%, while
fill increases about 0.2%. The original 72%/35%/6% figures described a version
with visible occlusion regressions and do not describe the corrected build.
These are work counts, not frame-rate measurements. Worst primitive usage
remains below half the frame buffer budget.

The preview previously changed stage palettes while continuing to render the
stage-0 track. It now opens the actual stage track. Corrected 180-frame sweeps:

| Stage | Samples avg, old → baked | Transforms avg, old → baked | Projections avg, old → baked |
|---|---:|---:|---:|
| Gate | 688 → 562 | 736 → 639 | 992 → 986 |
| Beat | 685 → 559 | 731 → 635 | 989 → 982 |
| Crush | 684 → 558 | 731 → 635 | 985 → 979 |

This correction also makes later-stage costs in other worlds visible; for
example, Meridian stage 2 reaches 2,226 projections in this sweep. It should
be profiled before selecting the next world to migrate.

## Build-time data and runtime work

`worlds/sine.json` defines wall shape, camera-distance LOD thresholds, lookahead,
projected-error tolerance, and memory/work budgets. `tools/bake_worlds.py`
compiles a small host sampler that uses the game's actual `path.c` arithmetic.
It emits `world_mesh_data.h` and `world_mesh_report.json` into the build folder.
The header is a versioned resident C representation, not a portable binary ABI.

Each cross-section stores a 32-bit world origin and five signed 16-bit local
offsets: 44 bytes. Each schedule stores byte offsets into nearby cross-sections.
There are four LOD step sizes: 256, 512, 1,024 and 2,048 units. Camera-depth
thresholds are 1,600, 5,000 and 10,000 units; spans are shortened when projected
chord error exceeds one pixel at the tested poses. Straight spans are checked
exactly; curved spans test every omitted cross-section at the start, middle
and end of each camera band. This sampled test is not a mathematical error
bound for every intermediate pose of a future curved asset.

The current bake contains three geometry groups, 638 cross-sections and 465
schedules per group, with at most 39 cross-sections per schedule. Tables cost
149,850 bytes, excluding small track strings/alignment. Unsupported versions,
bad coverage, overflowing coordinates, excessive schedule counts and resident
budget violations fail the build. CMake tracks the compiler, manifest, track
data and path/camera dependencies.

Schedules cover 44,032 units ahead, including geometry beyond the track end,
and the existing 20,000-unit camera-depth horizon is retained. They do not
remove scenery at LOD boundaries. A shared cross-section sequence controls
both walls and the ceiling, avoiding independent LOD seams. Runtime transforms
each distant cross-section once and shares unmodified projections between adjoining
spans. Near-plane clipping, screen subdivision, haze and ordering-table
placement still use the existing renderer. Static colors are resident; haze,
animated rib color and conduit pulses remain dynamic.

The near region is deliberately procedural through the end of the sheet
containing the 2,200-unit animated-detail range. The compiled continuation
starts on that exact shared boundary. Keeping the old near subdivision is
necessary to preserve ordering-table occlusion of ribs and conduits. For
axis-aligned cross-sections, the compiler verifies a shared basis: runtime
rotates two basis vectors per frame and only one origin per distant ring.

The corrected music build uses 1,371,680 bytes for its loadable image plus BSS,
leaving 659,936 bytes above it; the build prints current usage after each link.
`tools/check_ram.py` checks all three
executables after linking, including BSS, and rejects images that enter the
upper 256 KB reserved for stack/heap headroom. This matters because the SDK's
default linker script permits an 8 MB region. The guard does not measure
actual peak stack or heap use.

## Reproduce and validate

Build and compiler/integration checks:

```sh
cmake --build runner/build --target check_worlds
cmake --build runner/build --target check_world_visuals
cmake --build runner/build
```

The focused checks cover independently known projection scale, curvature
rejection, long-range schedule coverage, work/memory overflow, retail RAM/BSS
limits, and baked vertices transformed against the runtime path. The geometry
check visits every Sineworks schedule in reverse order and seeks among three
camera offsets, checking all six corners of every scheduled cross-section.

### Image consistency and actual distant pixels

The initial few stills and vertex checks missed a visible regression. Changing
the near wall tessellation exposed large bright conduit bands and changed rib
occlusion. At stage 1, scroll 62,976, the original prototype differed from the
reference by 2,828 high-contrast pixels. Preserving procedural near walls fixes
this rather than accepting the difference for a lower CPU-work count.

`check_world_visuals` now compares 5,196 identical-state frame pairs: all three
tracks through the finish, at offsets 0, 1, 127 and 255 of every 256-unit band.
The image gate allows mean channel difference up to 0.02 on a 0–255 scale and
at most 16 pixels with a channel difference above 24. Current worst mean drift
is 0.005425, with at most six high-contrast edge pixels in any frame. Large
conduit/rib differences fail this check. This covers still-state consistency
at schedule boundaries, not all possible temporal or console GPU behavior.

A separate 60-frame diagnostic false-colors actual scenery primitives by
sorting depth and counts their pixels after normal clipping, ordering and
software rasterization. Every sampled frame has visible 5,000–10,000 and
10,000–20,000 depth bands. This proves that the far tunnel shell reaches the
image, rather than merely existing in a table. It does not prove the broader
persistent-distant-prop concept: ribs and conduits are still nearby-only, as
before. There is no authored distant prop LOD demonstration yet.

The report at `runner/build/world-visuals/index.html` contains native-resolution
reference/baked/difference frames, depth diagnostics, vanishing-point zooms,
and links to the full comparison CSVs. Host rasterization approximates the
console; real console/emulator footage is still required for GPU-specific
artifacts and frame timing.

Generate native-resolution comparison stills and count sweeps:

```sh
RUNNER_PREVIEW_LEVEL=SINEWORKS sh runner/tools/preview/preview.sh /tmp/world-baked
RUNNER_PREVIEW_LEVEL=SINEWORKS RUNNER_PREVIEW_FLAGS=-DRUNNER_LEGACY_WORLDS \
  sh runner/tools/preview/preview.sh /tmp/world-procedural
```

Omit the level filter to sweep all worlds. For a console A/B build configure
separate build folders with `-DRUNNER_PROFILE=ON`, using
`-DRUNNER_LEGACY_WORLDS=ON` for the procedural side. The profiler includes a
`baked world mesh` phase and worst-frame transform/ring/span counters. Compare
CPU phase time, GPU wait and worst frames in the same emulator configuration
or on hardware before claiming the target frame rate.

Next steps are conditional on those results: reduce command overhead if GPU
cost dominates, migrate another measured expensive static structure, and only
then consider static lighting, a broader mesh format or streaming. Neither
streaming nor migration of every world is required to use this first bake.
