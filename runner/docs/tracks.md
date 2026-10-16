# Tracks

The road is a catalog of named tracks. The game runs one of them. An HTML
viewer opens all of them. A track the game cannot run yet is still a file,
so a future feature is a track you can look at, not a paragraph you hope
the API will cover.

## How an endless run is a finite track

A run has a start: `s = 0`, which is `scroll = 0` today. It does not have a
finish line. Every track repeats, and `s` keeps increasing.

The viewer never draws that infinite run. Every track has a **span**, one
finite period with a start and an end:

- `s0` is the start of a run.
- `s1` is the end of the period.
- `repeatFrom` is the seam. The last piece links back to it. The game plays
  any lead once, then repeats from the seam forever. The viewer stops at
  `s1`.

The pose at the seam decides the strategy:

- **closes.** The period returns to the seam. The next lap occupies the same
  place. The marker says **start · closes**. `bend.flat` does this.
- **continues.** Heading, pitch, and bank match the seam, and the road is
  further along. The next period repeats ahead. One ghost step shows it
  starting. `straight.run`, the hill, the banked bends, the lane changes,
  the fork, and the gap do this.
- **turns.** Pitch and bank match, and the heading has changed. The next
  period repeats on that new heading. The ghost step follows it.

`straight.run` is the current road, framed:

| Piece | s | What it is |
|---|---|---|
| `start` | 0–2048 | The opening of a run. No obstacles. Bands 0–7. |
| `cycle` | 2048–8192 | The obstacle period the game repeats. Bands of 24 after the lead. |

The first obstacle sits on `cycle`, segment 5 of that piece (band 13), the
same place `has_obstacle` puts it today. The end of the span is the end of
one cycle, not the end of the run.

A period with a lead, such as `straight.run`, marks the seam with the
piece name. The end of the span is the end of one period, not the end of
the run.

## Words

- **Piece.** The named span you edit and talk about. A length and a few
  numbers: heading change, grade, bank, lane count, obstacles.
- **Segment.** One baked step of length `STEP` (256). This is the record
  the game walks.
- **Span.** The segments the viewer frames, from `s0` to `s1`.
- **Address.** Track, piece, segment. "cycle, segment 5" is the first
  obstacle on `straight.run`.

Angles in the bake are the GTE units: 4096 is a full turn. The catalog
writes degrees that land on those units exactly (multiples of 22.5°).
Positive yaw turns right. Positive grade raises the nose. Positive bank
drops the right edge.

## Catalog

| Track | Status | Pieces | What it asks of the game |
|---|---|---|---|
| `straight.run` | runnable | `start`, `cycle` | Continues. The obstacle cycle repeats. |
| `bend.flat` | runnable | `north`, `east-bend`, `south`, `west-bend` | Closes. One lap returns to its start. The FLAT OVAL world runs it. |
| `grade.straight` | view | `flat`, `climb`, `ease`, `crest`, `drop`, `settle`, `runout` | Continues. The hill repeats on the same heading. |
| `bank.bend` | view | `enter`, `in-right`, `right`, `out-right`, `in-left`, `left`, `out-left`, `leave` | Continues. Banks right through one bend, then left through the other. Heading returns. |
| `lanes.count` | view | `one`, `one-to-two`, `two`, `two-to-three`, `three`, `three-to-two`, `two-hold`, `two-to-one`, `one-end` | Continues. The lane changes repeat from one lane. |
| `fork.lane` | view | `three`, `two-off`, `two-to-one`, `two-back`, `one-off`, `one-to-two`, `one-back`, `joined` | Continues. Three lanes split into two and one, those counts swap, and they merge as three. |
| `gap.drop` | view | `approach`, `gap`, `landing` | Continues. The gap repeats. |

The game loads a track only when its status is `runnable` and every
capability it uses is in the implemented set. Today that set is `repeat`
and `yaw`. `path_open` accepts `straight.run` and `bend.flat`. The viewer
draws the rest and lists the capability still missing.

## Order

1. **Now.** The path API, `straight.run` as data, the viewer framing every
   name. City and Lumenwild still run the straight road.
2. **`bend.flat`.** The FLAT OVAL world runs this lap and draws only the
   road. `path_cam` follows yaw, so the camera takes the turns.
3. **`grade.straight`.** Pitch and height.
4. **`bank.bend`.** Roll one way, then the other. The heading returns, so the repeat continues the way it started. The sprite stays upright on screen.
5. **`lanes.count`.** The road widens or narrows. One lane splits into two, two become three, and each of those merges back. One chain.
6. **`fork.lane`.** Three lanes split into a two-lane road and a one-lane road. While they are apart, the two becomes one and the one becomes two. They meet again as three.
7. **`gap.drop`.** No deck. The jump leaves `v` with nothing to land on
   until the next deck.

## Coordinates

Gameplay talks in path space.

- `s` is distance along the active chain, from the start of the run.
- `u` is lateral offset from the centerline. Lanes are centered on it.
  One lane sits at 0. Two sit at ±75. Three sit at -150, 0, and 150
  (`LANE` is 150). The three-lane road is 560 wide, the current
  `ROAD_HALF * 2`. Two lanes are 410 wide and one lane is 260, so the
  side margin stays put while the count changes.
- `v` is height above the deck. A jump changes `v`.

The runner stays a camera-facing upright sprite. The camera's up is the
segment's up, so the figure stays vertical in the frame while the road
banks or climbs.

## API

```c
int path_open(const char *id);                 /* 1 if this build can run it */
void path_begin(int scroll);                   /* once a frame, before draw */
void path_cam(int s, int u, int v, int *x, int *y, int *z);
int path_project(int s, int u, int v, XY *out);
const PathSeg *path_at(int s);                 /* segment containing s */
```

`path_begin` is called by `main` and by the preview before a level draws.
Each world names its track, and `path_open` selects that span first.
`path_cam` follows yaw. On `straight.run` the yaw is zero, so the frame is
still `(u, v, s - scroll)`. Pitch and roll stay level on the tracks the
game runs.

`path_at` walks the linked span. Past the lead, `s` maps into the cycle,
so an endless run still hits the same obstacles. A lane change is a piece
with `lanes` and `lanesTo` on one chain: one line opens into two, or two
lines close into one. A fork is a second chain. On `fork.lane` three lanes
split into a two-lane road and a one-lane road. The two-lane road then
becomes one lane, and the one-lane road becomes two. Both chains meet again
at `joined`, which is three lanes.
`path_at` stays on the main chain until a fork is runnable.

Obstacles and the runner go through this API. City and Lumenwild still
draw their straight ground with `flat()`. FLAT OVAL draws the lap as
cross-sections from `path_cam`, and the world around that road stays empty.

A segment record, as baked:

| Field | Meaning |
|---|---|
| `x, y, z` | Centerline at the start of the step |
| `yaw, pitch, roll` | Frame at the start |
| `lanes, width` | Lane count and road width at the start of the step |
| `edge`, `edge1` | Left and right edges of the pavement. A fork meets edge to edge |
| `markU`, `markU1` | The painted lines between lanes. Three lanes have two, two lanes have one, one lane has none |
| `laneU`, `laneU1` | Lane centers. Gameplay still measures `u` from these |
| `deck` | 0 on a gap |
| `next` | Following segment, the cycle seam, or the join |
| `fork` | Second chain, or -1 |
| `obstacle` | Lane, or none |

## What each later track turns on

The data for these tracks is already in the catalog. The function that
starts reading each field is named here so the gap is the implementation,
not the model.

| Track | Fields that leave zero | Function that starts using them | Viewer check |
|---|---|---|---|
| `straight.run` | none | `path_cam` identity | Continues. Obstacle lanes match the old band formula |
| `bend.flat` | `yaw` | `path_cam` aims by yaw | Closes. Yaw step ≤ 64 |
| `grade.straight` | `pitch`, centerline `y` | `path_cam` aims by pitch | Continues. Net pitch returns to 0. Pitch step ≤ 64 |
| `bank.bend` | `roll` | `path_cam` banks the right vector | Continues. Bank goes right, then left, and returns to 0. Roll step ≤ 64 |
| `lanes.count` | `lanes`, `lanesTo` | road width and lane lines along the chain | Continues from one lane. 1→2, 2→3, 3→2, and 2→1 |
| `fork.lane` | `fork`, `shift`, `lanes`, `lanesTo` | a second chain, then the join where the chains meet | Continues. 3 splits into 2 and 1, those counts swap, then they merge as 3 |
| `gap.drop` | `deck = 0` | landing looks for a deck under `v` | Continues. The gap has a deck on both sides |

A full turn is 4096. The per-segment cap is 64, about 5.6°. A steeper
piece is a longer piece, not a bigger step. The baker rejects a
`runnable` track that breaks a check or uses a capability this build has
not implemented. A `view` track that breaks a check is still drawn, with
the warning on the span.

## Files

- `tracks/catalog.json` — the pieces. This is the source.
- `tools/bake_tracks.py` — writes `tools/trackview/baked.json`, copies
  that file's contents into the viewer page, and writes a C span for each
  runnable track.
- `tools/trackview/` — the viewer. Opening `index.html` draws the included
  bake. Load, or a dropped `.json`, opens another bake file.
- `src/path.h`, `src/path.c` — the API. `course.c` reads obstacles from
  the baked span.

Bake, then open `tools/trackview/index.html`:

```
python3 runner/tools/bake_tracks.py
```

The build and `tools/preview/preview.sh` bake the C span on their own.
