# Story world dressing and frame cost

Lumenwild was the only story world with a full environment. The six worlds
after it mostly had a palette, a backdrop, and a few pieces beside the road.
This change dresses those six worlds without treating scenery as free. The
rule used throughout is:

- A world's horizon is drawn in screen space and slid by `path_yaw()`. It
  adds flat primitives and fill, but no GTE projections.
- Pieces that stand in the world are few, and they stay near the camera.
- A road sheet that cannot cross the screen is not split or projected.
- A wall sheet gets longer as it recedes, so distance does not mean one
  sheet per near-sized step. Bends keep the short step, so the sheet still
  follows the road and stays closed.

Lumenwild itself is unchanged.

## What each world got

**Vesper City** (`level_vesper.c`). The skyline towers now carry a neon
sign band as well as their windows. Stages 1 and 2 gained a low lit rail
just outside the curb, beside the runner only; this replaced the far
projected markers that used to run thousands of units ahead. The stage 3
canyon no longer uses the grown road span: it draws fixed 640-unit sheets
out to 4200, the near wall is only as tall as it needs to be to leave the
top of the screen, and the lit window band is drawn on the near wall only.
That last point matters: an early version of this dressing put windows on
every canyon sheet and reached 2,010 projections in the worst preview
frame. The fixed sheets brought stage 3 to 1,117 projections on average,
1,368 at worst, in the stage sweep below.

**Shatterglass** (`level_shatter.c`). Twelve glass shards now stand on the
horizon in screen space, drawn before the ridge so they sit on it. The
projected desert shrank from markers every 1,408 units out to 5,200, plus
ground sheets out to 6,200, to two crystals within 2,600 of the camera and
one low rise.

**Vesper Reach** (`level_reach.c`). Eight broken towers, some with a lit
window, stand on the horizon in screen space. One bridge — two posts and a
beam — crosses ahead of the runner; only the nearest crossing is drawn.
The standing stones beside the road are capped at two near the camera
instead of running out to 5,600.

**The Sineworks** (`level_sine.c`). The tube walls and ceiling are as they
were. The ribs used to be drawn along 7,040 units of tunnel whenever the
pulse matched; now one pulsing rib and one conduit stripe sit beside the
runner, and both take their colour from the animation clock.

**Pale Meridian** (`level_meridian.c`). Four aurora veils hang in screen
space above the crack, averaged with the sky (`BLEND_AVG`) so they read as
light rather than as towers. The ice walls are unchanged apart from the
shared sheet work below.

**Null Cathedral** (`level_cathedral.c`). A line of violet shards stands
on the horizon in screen space — seven of them, ten on the last stage.
The hanging shards beside the road are taller and closer together than
before but capped at four near the camera. The rare arch is unchanged.

## The shared saving, in `road.c`

Two changes pay for most of the above, and they apply to every world that
draws sheets beside the road:

- `sheet_hidden()` runs in `road_cloth()` and `road_lintel()` after the
  four corners are known but before `cloth_coarse()` can split the sheet.
  It places the corners with the same arithmetic `project()` uses and
  drops the sheet when all four sit beyond the same side of the screen,
  with a 96-pixel margin. Corners nearer than z 48 are never trusted, so a
  sheet that might still cut the view is still drawn. These sheets used to
  be split and projected in full before being thrown away.
- In `road_span()`, the near branch used to return the caller's step at
  every depth. Past z 900 the step now grows toward `z / 2`, capped at four
  times the step, and `too_bent()` still halves it back through a bend.
  Beside the camera nothing changes; a sheet that starts just behind the
  camera is still never skipped.

## Measuring it

`tools/preview/preview.c` learned to set a stage. Every existing still is
stage 0; a new `late` still (scroll 24000) runs stage 2, and after the
still shots each story world now gets a 180-frame sweep per stage that
prints projection average and worst, primitive bytes worst, and fill
average and worst. That sweep exists because Vesper City and Null
Cathedral change shape on their last stage, and the old preview only ever
showed stage 0.

Host preview numbers, 60-second run before and after this change. These
are the host renderer's counts, not PlayStation cycles: projections are
the CPU proxy, primitive bytes are measured against the frame's 32 KB
buffer (`BUFFER_LENGTH`), and fill is pixels drawn.

| World | Projections avg, before → after | Fill avg, before → after | Prim bytes worst, after (of 32768) |
|---|---|---|---|
| Vesper City | 988 → 1013 | 119,405 → 122,526 | 16,768 |
| Shatterglass | 965 → 794 | 146,386 → 149,652 | 15,016 |
| Vesper Reach | 972 → 909 | 158,429 → 164,776 | 12,412 |
| The Sineworks | 1093 → 991 | 111,961 → 112,480 | 11,652 |
| Pale Meridian | 1111 → 1048 | 95,964 → 99,820 | 14,828 |
| Null Cathedral | 222 → 224 | 68,608 → 69,612 | 5,956 |

Four of the six worlds project less than they did before they were
dressed; the other two are within a few percent. Fill rises a little where
a horizon was added. No world comes near the primitive buffer limit: the
worst frame in any of them uses about half of it.

## Checks

- The host preview was rendered for all thirteen levels before and after,
  and the stills for the six dressed worlds were inspected, including the
  stage 2 `late` still. The Sineworks tube, the Meridian ice walls, and the
  Vesper canyon still meet with no holes.
- `tools/check_track.c` and `tools/check_web_row.c` both pass
  (`track span ok`, `web row ok`).
- A full build from the repository root writes all three discs to `build/`:
  `runner`, `runner-nomusic`, and `runner-web`.

## What this does not prove

The preview counts work; it does not count PlayStation CPU cycles, and it
cannot show a dropped frame. The numbers above say the dressed worlds ask
for no more projections, fill, or primitive memory than they did before,
within a few percent either way. The hardware check is still the profile
disc (`-DRUNNER_PROFILE=ON`, DuckStation with TTY logging) or simply
playing `build/runner.cue` and watching the frame hold.

The short version of the rule is also in `docs/story-worlds.md`, where the
next world will be described.
