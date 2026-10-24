# Meridian split and join walls

The October 24 recording (`runner 2026-10-24-13-34-29.mp4`, starting at
00:58) exposed misplaced and disappearing walls while approaching The Fork
and taking its left branch. Sparse captures on the right branch and comparisons
against the old renderer did not establish correctness.

The replacement is one baked world-space boundary, shared by both routes:

- Outer walls connect the common road to both branches and back through the join.
- The inner island starts and ends at intersections of the actual offset wall
  contours. It retains full wall height and stays beyond both curbs. Extending
  inner walls into the overlap between the two roads is invalid geometry.
- The outer boundary supplies a continuous ground underlay beneath both roads.
  It fills the split/join seams without relying on separately clipped shoulders
  meeting perfectly. Ordinary ground overlaps the foot of the walls by 32 units
  and draws behind the enclosing walls. The fork floor has a 16-unit underlap
  at its edges; its visible wall boundaries retain their exact positions.
- Ordinary wall sheets skip this replacement interval. The baked boundary is
  transformed once through the active camera; it is not redrawn in the other
  route's camera space.
- Faces are opaque. Polygon clipping retains walls crossing the near plane or
  viewport even when their original corners are all outside it. Back-face
  culling removes hidden island faces. Only visible walls need subdivision for
  ordering; the ground underlay always stays behind roads and walls.

`fork_boundaries` in a wall manifest enables the shared sampler/compiler path.
The compiler requires a single non-self-intersecting island with two boundary
intersections per fork and rejects unsupported topology. Mirrored forks are
covered; their inner edges are selected from geometry rather than route names.

## Regression checks

`python3 runner/tools/test_fork_walls.py` checks the actual Fork and Verge paths,
both route masks, and mirrored geometry. It independently clips each wall
against the authored drivable polygons, checks full-height tips and exact shared
seams, and rejects walls crossing either road.

The native preview's `--fork-review 1 40` and `--fork-review 2 40` follow both
routes, preserving camera easing, from before the split until after the join.
These checks are included in `check_world_visuals`. They reject scenery-budget
cutoffs and exposed foreground sky, starting 32 pixels below the camera horizon.
The horizon band is reviewed visually: the road descends after the join, so a
flat-floor assumption there is invalid. Checking only
the 32 KB primitive buffer misses the earlier scenery cutoff that reserves HUD
space. The older broken native capture at progress 47616 contains 341 exposed
sky pixels below the check's horizon margin.

`check_worlds` also verifies the decoded packages against the runtime camera,
including exact vertex sharing across route packages. Identical rings share a
pool only when the saved vertex storage exceeds the added index storage; cut
flags and route-specific schedules remain independent.

Native images establish geometry and raster coverage. They do not establish
DuckStation presentation rate or real-hardware performance.

## October 24 validation

- Both stages passed 1,070 native frames each, covering both branches with
  camera easing: zero foreground sky leaks and zero scenery-budget omissions.
  Worst primitive use was 22,196 bytes for Fork and 22,100 for Verge.
- `check_worlds` passed, including independent road-intersection checks and
  decoded shared-vertex checks. The all-stage pipeline passed 5,067 frame pairs.
- Music, no-music and web discs built successfully. The largest program plus
  BSS is 1,725,580 bytes, leaving 306,036 bytes above the image with the 256 KB
  reserve intact. Meridian's resident mesh package is 155,588 bytes.
- Native videos, before/after images and build hashes are in
  `runner/build/fork-review/review.html` and `validation.json` (generated artifacts).
  No new emulator capture or frame-rate measurement was obtained for this build.
