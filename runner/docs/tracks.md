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
  place. The marker says **start · closes**. `bend.flat`, `hill.loop`, and
  `bank.bend` do this.
- **continues.** Heading, pitch, and bank match the seam, and the road is
  further along. The next period repeats ahead. One ghost step shows it
  starting. `straight.run`, the lane changes, the fork, and the gap do this.
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
drops the right edge. Bank pivots on the low edge, the way a banked corner
is built: the low edge stays at grade and the deck rises from it. The bake
folds that rise into the centerline, so anything placed by `p` and `right`
rides the bank without knowing about it.

## Catalog

| Track | Status | Pieces | What it asks of the game |
|---|---|---|---|
| `straight.run` | runnable | `start`, `cycle` | Continues. The obstacle cycle repeats. |
| `bend.flat` | runnable | `north`, `east-bend`, `south`, `west-bend` | Closes. One lap returns to its start. The FLAT OVAL world runs it. |
| `hill.loop` | runnable | `start`, one big hill, `turn-back`, two smaller hills, `turn-home`, `home` | Closes. The HILL world runs it. Turns like the oval, with grades on both straights. Both sides lose the same ground to slope: four 4096 ramps out, eight 2048 ramps back, all at 22.5°, with 8192 of flat on each side. |
| `bank.bend` | runnable | `start`, `turn-in`, `turn`, `turn-out`, `link`, `lean-left`, `carve-left`, `flip`, `carve-right`, `lean-right`, `settle`, `home-in`, `home-turn`, `home-out`, `home` | Closes. The BANK world runs it. Both turn-arounds bank right at 33.75°. On the way back, an S banks left at 22.5°, rolls through level into a 22.5° right bank in `flip`, and settles. The S moves the road outward, so the home turn-around is wider (12288) to meet the start. |
| `lanes.count` | view | `one`, `one-to-two`, `two`, `two-to-three`, `three`, `three-to-two`, `two-hold`, `two-to-one`, `one-end` | Continues. The lane changes repeat from one lane. |
| `fork.lane` | view | `three`, `two-off`, `two-to-one`, `two-back`, `one-off`, `one-to-two`, `one-back`, `joined` | Continues. Three lanes split into two and one, those counts swap, and they merge as three. |
| `gap.drop` | view | `approach`, `gap`, `landing` | Continues. The gap repeats. |

The game loads a track only when its status is `runnable` and every
capability it uses is in the implemented set. Today that set is `repeat`,
`yaw`, `grade`, and `bank`. `path_open` accepts `straight.run`,
`bend.flat`, `hill.loop`, and `bank.bend`. The viewer draws the rest and
lists the capability still missing.

## Order

1. **Now.** The path API, `straight.run` as data, the viewer framing every
   name. City and Lumenwild still run the straight road.
2. **`bend.flat`.** The FLAT OVAL world runs this lap and draws only the
   road. `path_cam` follows yaw, so the camera takes the turns.
3. **`hill.loop`.** Pitch and height on a closed lap. The HILL world runs
   it. The camera takes 3/4 of the grade under it, and the backdrop sits
   on `path_horizon`.
4. **`bank.bend`.** Roll on a closed lap: banked turn-arounds, and an S that rolls from a left bank straight into a right bank. The BANK world runs it. The camera takes none of the roll, so the deck tilts on a level horizon and the backdrop still sits on `path_horizon`. The runner leans square to the deck.
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

The runner is a camera-facing sprite. The camera pitches with the road but
never rolls, so the figure stays vertical while the road climbs. On a bank
it stands square to the deck: `path_stand` gives the feet and the deck's
up vector on screen, and the sprite quad is built along it, so the runner
leans with the bank and both feet sit on the tilted road. Flat turns give
no lean.

## API

```c
int path_open(const char *id);                 /* 1 if this build can run it */
void path_begin(int scroll);                   /* once a frame, before draw */
int path_horizon(void);                        /* screen y of the level horizon */
void path_cam(int s, int u, int v, int *x, int *y, int *z);
int path_project(int s, int u, int v, XY *out);
const PathSeg *path_at(int s);                 /* segment containing s */
```

`path_begin` is called by `main` and by the preview before a level draws.
Each world names its track, and `path_open` selects that span first.
`path_cam` follows yaw, pitch, and roll. On `straight.run` all are zero,
so the frame is still `(u, v, s - scroll)`. The camera takes 3/4 of the
pitch and none of the roll.

`path_at` walks the linked span. Past the lead, `s` maps into the cycle,
so an endless run still hits the same obstacles. A lane change is a piece
with `lanes` and `lanesTo` on one chain: one line opens into two, or two
lines close into one. A fork is a second chain. On `fork.lane` three lanes
split into a two-lane road and a one-lane road. The two-lane road then
becomes one lane, and the one-lane road becomes two. Both chains meet again
at `joined`, which is three lanes.
`path_at` stays on the main chain until a fork is runnable.

Obstacles and the runner go through this API. City and Lumenwild still
draw their straight ground with `flat()`. FLAT OVAL, HILL, and BANK draw their
road with `road.c`: cross-sections from `path_cam`, with width, lane
dividers, and deck read from each segment. A world gives `road_draw` a
`RoadStyle` of materials and adds its own scenery, so a new runnable track
needs a level file of a few lines, not a new road renderer.

Each world also declares one `Light` (`level.h`), and the road and the
runner both read it:

| Field | Kind | Reaches |
|---|---|---|
| `sky` | color | The clear color, and the backdrop's foot |
| `haze` | color | What distance fades toward: curbs, gantry, the far deck, the air under the ridge |
| `tint` | multiplier, 128 = none | The runner sprite and the road paint |
| `shadow` | multiplier, 128 = none | The runner's shadow, channel by channel |
| `density` | 0..256 | How thick the air is: the far asphalt, the crates, and the ridges give way to `haze` |

`sky` and `haze` are separate, so a red haze can hang under a blue sky.
At `density` 0 the materials in `RoadStyle` keep the colors the world
gives them and only the curbs, gantry, and far deck fade to `haze`. Raise
it and the whole distance goes into the air; the near road stays its own
color. A world with no light of its own uses `light_neutral`.

`road.c` draws three tiers by camera depth:

| Tier | Depth | What draws |
|---|---|---|
| Near | to 1900 | Deck, pulsing curbs, dividers, lit crates |
| Mid | to 2800 for dividers, to `FAR` (4200) | Deck, crates, and a one-pixel line on each edge that starts as the curb and fades to the far line by `FAR`; crates go one flat color past 1450 |
| Far | `FAR` to 20000 | Deck shaded from near edge to far edge, the same edge lines, hazed toward the sky. A crate is its front face |

The far tier walks up to 44000 of track ahead, and on a closed track it
stops one step short of a lap so it never draws the road behind the
camera. That keeps a turn and the road coming back in view, such as the
return leg of `hill.loop` seen from its big crest. Spacing follows depth
(256 to 768) and halves where a chord would turn more than 192 or pitch
more than 48, so bends stay round and crests keep their outline. Samples
off screen or out of depth advance 1024 and draw nothing. At most 48
samples a frame.

Its depth is squeezed into the ordering table buckets between the near
road and the backdrop, so a crest still covers the road behind it by
painter's order. Nothing tests visibility: a hidden far piece is a few
pixels, and the projection is spent either way.

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
| `hill.loop` | `pitch`, centerline `y` | `path_cam` aims by pitch | Closes. Net pitch returns to 0. Pitch step ≤ 64 |
| `bank.bend` | `roll` | `path_cam` banks the right vector | Closes. Bank goes right in each turn-around, and left then right through the S, returning to 0 each time. Roll step ≤ 64 |
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
- `src/road.h`, `src/road.c` — the shared road for any runnable track.
- `tools/check_track.c` — host checks for the runnable spans.

Bake, then open `tools/trackview/index.html`. A hash such as
`index.html#hill.loop` opens that track:

```
python3 runner/tools/bake_tracks.py
```

The build and `tools/preview/preview.sh` bake the C span on their own.
