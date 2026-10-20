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
  place. The marker says **start · closes**. `bend.flat`, `hill.loop`,
  `bank.bend`, `lanes.count`, and `fork.lane` do this.
- **continues.** Heading, pitch, and bank match the seam, and the road is
  further along. The next period repeats ahead. One ghost step shows it
  starting. `straight.run` and the gap do this.
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
| `lumenwild.way` | runnable | `trail` through `home` | Continues. An S of bends, a hill, then a narrowing to one lane and back. Lumenwild runs it. |
| `bend.flat` | runnable | `north`, `east-bend`, `south`, `west-bend` | Closes. One lap returns to its start. The FLAT OVAL world runs it. |
| `hill.loop` | runnable | `start`, one big hill, `turn-back`, two smaller hills, `turn-home`, `home` | Closes. The HILL world runs it. Turns like the oval, with grades on both straights. Both sides lose the same ground to slope: four 4096 ramps out, eight 2048 ramps back, all at 22.5°, with 8192 of flat on each side. |
| `bank.bend` | runnable | `start`, `turn-in`, `turn`, `turn-out`, `link`, `lean-left`, `carve-left`, `flip`, `carve-right`, `lean-right`, `settle`, `home-in`, `home-turn`, `home-out`, `home` | Closes. The BANK world runs it. Both turn-arounds bank right at 33.75°. On the way back, an S banks left at 22.5°, rolls through level into a 22.5° right bank in `flip`, and settles. The S moves the road outward, so the home turn-around is wider (12288) to meet the start. |
| `lanes.count` | runnable | 23 pieces, `three` around to `bend-home` | Closes. The ZIPPER world runs it. A test lap for every lane change. Four 90° bends hold 1, 3, 2, and 3 lanes; no lane change happens inside a bend. Straights: 3→2 left-anchored, 2→1 right-anchored north; 1→2 left, 2→3 right back to back east; 3→1, 1→3 right-anchored, 3→2 centered south; 2→3 centered west. Edge anchors come in pairs on one straight so their slides cancel. A crate on every hold and bend, in a lane valid for its count. |
| `fork.lane` | runnable | fork A (`three` … `joined`), `turn`, fork B (`three-b` … `joined-b`), `home-turn` | Closes. The MAXI-P world runs it. Two forks and two three-lane turn-arounds. At each fork three lanes split into two and one, each branch bends out, runs straight, and bends back, and they merge as three. Fork A, heading out, swaps counts on the straights: the two drops its left lane (`anchor: right`) and the one adds a lane on its right (`anchor: left`), so the swapped lanes meet a centered road. Fork B, heading home, is a plain fork with no lane change. Both forks use the same mirrored 45° bends. Turn-arounds are 12288 so the inner branches stay clear of each other. |
| `gap.drop` | view | `approach`, `gap`, `landing` | Continues. The gap repeats. |

The game loads a track only when its status is `runnable` and every
capability it uses is in the implemented set. Today that set is `repeat`,
`yaw`, `grade`, `bank`, `lanes`, `fork`, and `join`. `path_open` accepts
every track but `gap.drop`. The viewer draws it and lists the capability
still missing.

## Order

1. **Now.** The path API, `straight.run` as data, the viewer framing every
   name. City still runs the straight road. Lumenwild runs `lumenwild.way`.
2. **`bend.flat`.** The FLAT OVAL world runs this lap and draws only the
   road. `path_cam` follows yaw, so the camera takes the turns.
3. **`hill.loop`.** Pitch and height on a closed lap. The HILL world runs
   it. The camera takes 3/4 of the grade under it, and the backdrop sits
   on `path_horizon`.
4. **`bank.bend`.** Roll on a closed lap: banked turn-arounds, and an S that rolls from a left bank straight into a right bank. The BANK world runs it. The camera takes none of the roll, so the deck tilts on a level horizon and the backdrop still sits on `path_horizon`. The runner leans square to the deck.
5. **`lanes.count`.** The road widens or narrows. A closed lap that runs all six lane changes on straights, with bends at every lane count. The ZIPPER world runs it. The runner's lanes are the baked lane centers under it: left and right step to the next lane over, a lane that closes slides the runner onto its neighbor, and an edge anchor holds the runner still while the road moves beside it. One chain.
6. **`fork.lane`.** Three lanes split into a two-lane road and a one-lane road, and meet again as three. A closed lap: heading out, the two becomes one and the one becomes two before they merge; heading home, a plain fork and merge. A turn-around follows each. The MAXI-P world runs it. The runner's lane at the fork picks the road; the other road draws beside it.
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
const PathSeg *path_at(int s);                 /* segment containing s, on the route */
void path_edges(int s, int *ul, int *ur);      /* deck edges at s */
int path_lanes(int s, int *u);                 /* lane centers at s */
int path_marks(int s, int *u);                 /* divider lines at s */
int path_lane_near(int s, int u);              /* the lane u belongs to */
int path_follow(int s, int u);                 /* runner: pick forks, report jumps */
int path_branch(int k, int s, int *s0, int *s1); /* next run of fork k */
void path_swap(int k);                         /* draw fork k's other road */
```

`path_begin` is called by `main` and by the preview before a level draws.
Each world names its track, and `path_open` selects that span first.
`path_cam` follows yaw, pitch, and roll. On `straight.run` all are zero,
so the frame is still `(u, v, s - scroll)`. The camera takes 3/4 of the
pitch and none of the roll.

`path_at` walks the linked span. Past the lead, `s` maps into the cycle,
so an endless run still hits the same obstacles. A lane change is a piece
with `lanes` and `lanesTo` on one chain: one line opens into two, or two
lines close into one. Any path runs 1 to 3 lanes. Lane changes run
straight: a bend may hold any count, but the bake refuses a piece that
turns and changes lanes at once. Every lane change must name an `anchor`,
and a piece that keeps its lanes must not:

| `anchor` | Held in place | Lanes come and go | Centerline |
| --- | --- | --- | --- |
| `left` | left edge and the lanes beside it | on the right | half a lane per lane: away from the anchor when adding, toward it when dropping |
| `right` | right edge and the lanes beside it | on the left | half a lane per lane: away from the anchor when adding, toward it when dropping |
| `center` | the middle | equally on both sides | stays put |

The slide runs along the piece, so the next piece starts on the moved
centerline. A removal is the matching add, same anchor, run backwards. The
bake refuses to run if any pair breaks that mirror. A fork is a second
chain. On `fork.lane` three lanes split into a two-lane road and a one-lane
road. The two drops its left lane (`right`) and the one adds a lane on its
right (`left`), so both slide half a lane right and the swapped lanes meet a
centered three-lane road at `joined`. Where a chain joins a piece already
baked, every arriving lane must land on a lane of that piece, or the bake
stops. Where two roads at the same distance overlap, the bake puts a seam
halfway between them: each inner edge stops a few units past the seam,
never wider than its normal edge, and a segment that overlaps at both
ends drops its inner curb. The decks meet edge to edge, and the inner
curbs close in a point at a fork or a join: where a curb starts or stops
and the road runs on, the drawer narrows it to nothing across that
segment, so it ends as a triangle instead of a square block. The bake
stops on an overlap that is banked.

The road drawer never runs a strip across a band edge where the road jumps
sideways or changes width. It samples that edge twice, once as the end of
the old segment and once as the start of the new one, so no strip wedges
across the jump.

In the game a fork is a branch: a second run of segments over the same
bands as the main chain, so both roads share `s`. A runnable fork must
rejoin the main chain exactly as many bands after it left, and may not
fork again inside; the bake stops otherwise. The route is which road each
fork follows, and `path_at` and every sample follow it. `path_follow`
picks the road when the runner enters a fork, by the road with a lane
nearest the runner's lane. Where a road starts beside another (a fork's
`shift`, a join) the centerline jumps sideways; `path_follow` returns the
jump so the runner stays put, and the camera eases across it. `road_draw`
draws the route, then swaps each fork in view onto its other road and
draws that too, crates included.

Each segment carries its deck edges, lane centers, and divider lines at
both ends, so the deck, dividers, and runner lanes follow a lane change
and a seam exactly as baked. Obstacles are stored in half lanes, so a
two-lane road can hold a crate at ±75.

Obstacles and the runner go through this API. City still
draws its straight ground with `flat()`. Lumenwild, FLAT OVAL, HILL, BANK, ZIPPER, and MAXI-P draw their
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
| `lanes.count` | `lanes`, `lanesTo`, `anchor` | road width and lane lines along the chain | Closes. All six changes (1↔2, 2↔3, 1↔3) on straights, one pair back to back, under all three anchors. Bends at 1, 2, and 3 lanes, constant width through each. Edges continuous at every seam |
| `fork.lane` | `fork`, `shift`, `lanes`, `lanesTo`, `anchor` | a second chain, then the join where the chains meet | Closes. Twice per lap, 3 splits into 2 and 1 and merges as 3. The outbound fork swaps 2→1 and 1→2 on straights; the return fork changes nothing. Every arriving lane lands on a lane of the joined road |
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
`index.html#hill.loop` opens that track. Clicking a piece in the sidebar
frames it, paints it a saturated pink, and dims the rest of the deck; the
hash becomes `#track/piece`, so `index.html#bank.bend/flip` opens with that
piece picked:

```
python3 runner/tools/bake_tracks.py
```

The build and `tools/preview/preview.sh` bake the C span on their own.
