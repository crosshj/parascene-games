# Ports

Another system is a new backend for the same calls. The levels, the road,
the course, and the menus stay the files they are. A port draws this
320×240 frame. It does not grow a second way of describing the road.

The host preview already does this for stills. It compiles `world.c`, the
course, and the levels on the computer, replaces the GTE and the GPU, and
paints the quads. A playable port is that, plus the game loop, buttons,
and the menu samples.

## The contract

The shared code talks to `world.h`:

- `project` turns a camera-space point into a screen point.
- `poly_f4`, `poly_f4v`, and `poly_g4` draw one quad into a bucket.
- `line_f2` draws one line. `gradient` fills a screen rectangle.
- `text` draws the font. `dither_mode` sets dithering for later prims in
  that bucket.
- `world_link` inserts a primitive the caller owns. `world_flip` shows
  the finished frame. `world_set_clear` sets the empty color.

The frame is 320×240. Bucket 0 draws last. Blend modes are the ones
`world.h` names. A backend implements these calls. The PlayStation
backend is `world.c`. The preview's software painter is the other one
that exists today, and it stops at a PNG.

## What stays shared

`road.c`, `course.c`, `path.c`, and `level_*.c` already speak only those
calls. A new stalk, a new track, a lighting tweak, or a menu change is
one edit, and both builds compile it.

The split shows up when the contract grows. A textured quad, a new blend,
or a different projection is engine work: once in `world.c` for the disc,
and once in each other backend. Until the shared code needs something
those calls cannot say, the builds are the same C.

## Two leaks

Two places still talk to the PlayStation GPU on their own. Fold them
behind the same calls before the next port, or that port has to go
looking through content files for hardware.

- `level_lumenwild.c` uploads the sky with `LoadImage`.
- `main.c` uploads and draws the runner sprite and the menu art the same
  way, and it plays the menu samples through the SPU.

Buttons and those samples are the rest of a backend. Music is already
outside this contract: the CD build plays a disc track, and the web build
sends the command row in `web.md`.
