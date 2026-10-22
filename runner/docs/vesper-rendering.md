# Vesper City: baked facade lighting

Vesper's buildings now use dense amber/cyan window patterns on opaque masonry.
The skyline has a brighter dusk/purple/blue gradient behind its silhouettes.
Window brightness is baked artwork, not dynamic light sources.

## Build-time assets

`tools/bake_vesper.py` generates `vesper_facades.h` and a JSON atlas manifest.
Two deterministic 64×64 window patterns have box-filtered 32×32, 16×16 and
8×8 versions. Filtering averages light coverage before palette quantization;
small windows become dimmer light coverage instead of randomly disappearing.
The atlas takes 8,192 bytes plus a 32-byte palette. It is generated for both
console builds and the host preview. No runtime generation of individual windows.

VRAM allocation: 4-bit texels at (960,128), 64 halfwords × 64 rows; palette at
(0,483), 16 halfwords. This avoids the framebuffers, character pages/palettes,
font, title art/palettes, shadow, and Lumen's sky. Uploaded once, with DrawSync
before using each transfer's source buffer.

## Runtime

Each skyline building uses one opaque textured quad, replacing its old body
and window strips. Distant skyline buildings are dimmer than the foreground row.
The skyline retains its existing heading parallax and stage composition.

The canyon uses textured wall surfaces, not extra window geometry layered on
walls. Course-space spans are sampled every 160 units to follow curves/banks;
further affine subdivision reuses camera-space vertices and interpolates UVs.
Clipping interpolates UVs too. Offscreen sheets are rejected before projection.
Buildings have a stable 640-unit height instead of changing height with proximity.

Canyon texture detail bands start at 700, 1400 and 2400 course units ahead;
after 3200, an opaque wall uses the baked average-light color and existing fog.
These are distance heuristics, not a visibility bake or a projected-error proof.
The existing canyon draw range remains 4200. This change does not implement an
infinite city, general visibility-baked worlds, or automatic removal of props.

## Validation and cost

`test_bake_vesper.py` checks opacity, texture memory, tile bounds/non-overlap,
and preservation of average light across detail levels. Included in `check_worlds`.
The host renderer now uploads simulated VRAM and rasterizes affine FT4 textures
with indexed palette lookup; `--texture-self-test` exercises packed nibble order,
UV orientation, palette addressing, and transparent zero. Current modulation is
monochrome, matching this material; it is not a cycle-accurate PS1 GPU emulator.

The review in `build/vesper-review/index.html` has same-state before/after views
and six additional positions per stage, rendered at native 15-bit color. The
existing Sineworks tunnel and visibility regression checks also pass.

For the existing 180-pose canyon stage sweep, before → after:

| Counter | Before | After |
|---|---:|---:|
| Mean path samples | 489 | 294 |
| Mean camera transforms | 553 | 358 |
| Mean projections | 1021 | 1100 |
| Peak primitive bytes | 15780 | 16572 |
| Mean rasterized pixels | 107935 | 106692 |

The skyline stages keep their projection counts. Canyon CPU path work drops,
while projections and peak command memory increase modestly for textured-wall
subdivision. Counters do not price textured fill or VRAM cache behavior: no
console frame-time/FPS improvement is claimed. The music build's image+BSS is
1,233,332 bytes, leaving 798,284 bytes above the image; the RAM guard passes.

## Art adjustment

The sky glow is now deliberately dark (dusk 40/24/46, night 24/18/36,
blue 18/26/36). Windows use independently seeded 30% occupancy instead of
an arithmetic pattern with approximately 73% occupancy. Skyline buildings
crop the texture at a uniform scale instead of stretching it vertically;
stable per-building crop offsets vary which rooms are visible. Near/far
heading divisors are 1792/2048 instead of 1024/2048, reducing their relative
scrolling speed sevenfold.

Canyon affine subdivision now limits depth ratios and tall screen spans to
reduce uneven window shearing. In the same 180-pose sweep this increases
mean projections to 1842 and peak command bytes to 20124; mean path samples
and transforms remain 294 and 358. This is a deliberate image-quality cost,
not a performance improvement. The earlier table describes the initial pass.

## Skyline motion correction

Both skyline rows now use the same heading offset (2048 heading units per
320-pixel panorama). Distance does not change rotational camera motion;
the old 1792/2048 scales changed overlaps between fixed buildings during turns.
There is no temporal smoothing state or camera lag. The panorama repeats twice
per full revolution, so the heading wrap also preserves its phase.

`preview --vesper-motion-check` renders the actual isolated skyline for 2,049
consecutive poses through stage one's turn. It checks monotonic heading, a stable
horizon, and rigid screen translation of the rendered skyline (allowing an
8-level channel difference for screen-anchored native dithering). Before this
fix, 996,404 compared pixels deviated from common translation; afterward, zero.
The check is included in `check_world_visuals`. Integer-pixel stepping at native
320×240 resolution remains; this removes relative building motion rather than
blurring or delaying the entire background.
