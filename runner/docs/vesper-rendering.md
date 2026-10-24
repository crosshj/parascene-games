# Vesper rendering

## Final stage: open-top street canyon

The canyon uses opaque, shaded building surfaces beside the actual track, with
stepped rooflines and dark sky above them. It has no ceiling or separate
screen-space skyline in this stage. Distant buildings remain on the road.
The nominal tower height is now 960 units (near variation roughly 768–1152),
three times the initial canyon height.
The side surfaces extend 20,000 course units ahead, up from 4,200. Hook keeps
its horizontal bends, but those bends are unbanked so the road-side buildings
stay vertical as the track turns.

Like Sineworks, the renderer separates the enclosure from its decorative detail.
It no longer tessellates large textured facades. Small square window-light sprites
use deterministic sparse occupancy, shrink to single pixels, fade with distance,
and continue to the 20,000-unit scenery limit. They are screen-facing light impostors,
not perspective-correct close-up window frames. Distant sampling uses fewer columns and floors, and native one-pixel GPU tiles
replace quads for small lights. No textured wall fill remains in the final stage.

`bake_vesper.py` emits an end index for each provably constant-frame, constant-width
stretch of `vesper.hook`. Merging rejects heading/bank/grade/width changes, forks,
and discontinuous positions. Runtime walls use longer spans in these baked runs;
other spans remain short and share camera-space cross-sections. Window positions
interpolate those same sections without additional path sampling. The run table
adds 1,824 resident bytes. The build depends on the generated track JSON.

The enclosure clips against the near plane and viewport before projection. This
is necessary: simply submitting larger wall polygons through the older helper
made the GPU-size guard drop near walls. Full-screen coverage must not depend on
fine texture tessellation. Far silhouettes use the world's existing fog.

## Skyline artwork and motion

Two deterministic 64×64 window patterns have box-filtered 32×32, 16×16 and
8×8 variants. The 4-bit atlas occupies 8,192 bytes plus a 32-byte palette.
Skyline buildings crop tiles at uniform scale rather than stretching windows.
The patterns use seeded 30% room occupancy and per-building crop offsets.

VRAM: texels at (960,128), 64 halfwords × 64 rows; palette at (0,483), 16 halfwords.
These avoid framebuffers, sprites, font, title art and Lumen's sky. Uploaded once.
The sky glow remains dark: dusk 40/24/46, night 24/18/36, blue 18/26/36.

Both skyline rows use one heading offset, 2048 heading units per screen width.
Distance does not change camera rotational motion. There is no smoothing lag;
normal integer-pixel stepping at 320×240 remains.

## Validation and measured work

`test_bake_vesper.py` checks texture opacity, memory, mip bounds/light preservation,
and rejection of unsafe straight-span merging. `--texture-self-test` checks the
host preview's indexed VRAM, palette, UV and transparency behavior.

`--city-check` checks command budgets at 457 positions across the entire final
route, including the second tight bend. Peak usage is 18,360 of 32,768 bytes at
these positions. Four straight reference views have zero interior wall coverage
holes and zero covered pixels in the overhead sky opening. These analytical
coverage assertions apply to those straight views; bent views receive visual
review, not a claim of exhaustive occlusion correctness. Eleven review views
cover both bends and intervening straights.

`--vesper-motion-check` verifies rigid skyline translation through 2,049 turn
frames. It reports zero independently moving pixels, allowing one 5-bit step for
screen-anchored dithering. Sineworks' coverage/fade tests also pass. Both checks
and `--city-check` are in `check_world_visuals`.

Same 180-pose stage-three sweep, compared with the previous subdivided textures:

| Counter | Textured walls | Open-top enclosure |
|---|---:|---:|
| Mean path samples | 294 | 137 |
| Mean camera transforms | 358 | 245 |
| Mean GTE projections | 1842 | 1115 |
| Peak primitive bytes | 20124 | 18524 |

The first two stages retain their existing projection counts. The final stage
removes textured facade fill, but polygon counts and host counters do not measure
PS1 GPU cycles or console FPS. No console frame-time improvement is claimed yet.
This remains a specialized Vesper renderer, not a general visibility-baked world
system. The current music build's image+BSS is 1,237,300 bytes; RAM guard passes.

Visual review: `build/vesper-canyon/index.html`.

The taller-tower revision uses 16 window rows and a two-pixel clipping guard
band to avoid clear edge scanlines caused by fixed-point rounding. The city
coverage test now checks the taller wall silhouette and rejects any standalone
skyline asset in the final stage. Earlier cost figures describe the lower towers.

## Mid/far lights

Removed the previous 3,600-unit cutoff. Samples keep fixed world anchors with
128/256/512-unit column spacing and fewer floors at far range. Far emission fades
into the fogged wall color rather than becoming darker than the wall. One-pixel
lights use native 12-byte TILE_1 commands instead of 24-byte quads; these are also
rendered by the host preview. Existing wall sections supply all window positions,
without extra path sampling or wall texture subdivision.

The full-route budget check peaks at 22,544 bytes. Straight reference views at
scroll 0 and 48,000 retain 119/144 visible mid-distance window pixels and 62/39
visible far-distance window pixels, respectively. Visibility tags separate the
three detail bands so these counts represent actually visible far lights.
