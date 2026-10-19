runner
======

A PlayStation disc: a 3D road, a flat animated runner, a menu, and
audio/runner1.mp3 as a CD audio track.

The disc opens on the Parascene Games logo for about three seconds, then
the title screen with the game logo and PRESS START. The music starts on
the title screen. Next comes the runner select screen, then the run.

screens/parascene.png and screens/title.png are the copies the game
shows. They are cropped and scaled from bak/parascene_logo.png and
bak/game logo.png, at most 256 pixels wide, and the build turns each
into a 256-color texture. Pixels with alpha below 128 are empty. Resize
a new copy before building; the build does not resize them.

tools/make_sounds.py writes the menu sounds during the build: move,
select, back, start, and pause.

The character is a flat sprite on a camera-facing quad. A base sheet
and its map share a name: sprites/runner.png and sprites/runner.txt.
The text file sets the cell size and names each animation as a list of
cell numbers, counted left to right, then down. The runner sheet is
1024x1024: 8 columns of 128x160 cells. A character is another
PNG named name.base.png, such as blazer.runner.png, at that same size.
The base sheet is on the select screen too, as RUNNER, last in the
list so painted characters are the default. Left and right
switch characters, and the build reads the cells from runner.txt. Empty pixels
are magenta, #FF00FF, and so is any pixel with alpha below 128. Each character is reduced to 15 colors.

Regenerate the runner guide with:

  python3 runner/tools/make_sprites.py

That rewrites runner.png and runner.txt and leaves character PNGs in
place. Paint over the guide, keep the same cells, and save it under the
character name. Feet stay on the bottom edge of the cell. The tuck is
the frame whose feet pull up. Bright magenta inside a character is
treated as empty.

Look at a sheet with the sprite viewer. Serve the runner folder so the
page can read sprites/:

  python3 -m http.server 8766 -d runner

Then open http://127.0.0.1:8766/tools/spriteview/ . It loads
sprites/runner.txt, sprites/runner.png, and sprites/blazer.runner.png.

The road, shoulders, buildings, gantries, and obstacles are flat
polygons.

LEVELS
------
Every level runs the same course: three lanes, the same obstacles in
the same places, and the same collisions. A level only decides how the
world around the course looks. The select screen offers each level
under WORLD.

  LUMENWILD   a glowing alien forest (default); far growths are
              silhouettes, near ones smear into streaks as they rush
              past. Its sky is a soft texture (tools/make_lumen_sky.py)
              with a slow CLUT pulse
  CITY RUN    the road between blocks of buildings

The source is split the same way:

  src/world.c        ordering tables, GTE projection, polygon helpers
  src/path.c         where a point on the road sits
  src/course.c       lanes, obstacle layout, the obstacle box
  src/levels.c       the list of levels, in select-screen order
  src/level_*.c      one file per level
  src/main.c         screens, input, the runner, audio

TRACKS
------
The road is a named track. straight.run is the city and Lumenwild road: a
lead, then an obstacle cycle that repeats. The run has a start and no
finish. docs/tracks.md is the catalog and the path API.

FLAT OVAL is the other world. It runs bend.flat, the closed lap from the
viewer, and draws only that road so the turns are the thing on screen.
The camera follows the road's yaw. Hills, banks, lane changes, the fork,
and the gap are viewable and not runnable yet. Bake, then open
tools/trackview/index.html. The page includes the contents of baked.json.
Load opens another bake file.

  python3 runner/tools/bake_tracks.py

The viewer shows one period of each track. Every track repeats: the lap
closes, the road continues on the same heading, or the next pass follows
the turn it finished on.

To add a level, copy level_city.c to level_<name>.c, rename the Level
it defines, and add it to levels.c. CMake picks up the new file. The
draw function gets the scroll distance, this frame's speed, and an
animation clock that stops while the game is paused. It must draw the
ground and the obstacles from course.h along with its scenery.

To look at the levels without a console, run

  sh runner/tools/preview/preview.sh

It draws each level on the computer, writes PNG stills into
runner/build/preview, and prints what each frame costs: primitive
buffer bytes, GTE projections, and pixels filled. The game has 32 KB of
primitives per frame, shared with the HUD.

To measure real frame time, build the profiling disc:

  cmake -S runner -B runner/build/profile -G Ninja -DRUNNER_PROFILE=ON
  cmake --build runner/build/profile

Open runner/build/profile/runner.cue in DuckStation with TTY logging on.
It skips the menus, runs each level for 900 frames with collisions off,
and prints CPU cycles for each part of the level, and the GPU's drawing
time, as a share of a 60 Hz frame. The disc stays in that folder.

HOW TO BUILD
------------
Needs PSn00bSDK, mipsel-none-elf-gcc, cmake, and ninja. macOS afconvert
turns audio/runner1.mp3 into the 44100 Hz stereo track the disc format requires.

From the repository root:

  export PSN00BSDK=$HOME/psn00bsdk
  export PATH="$PSN00BSDK/bin:$PATH"
  cmake -S runner -B runner/build -G Ninja
  cmake --build runner/build

The compile stays in runner/build. The build copies runner.cue,
runner.bin, runner-nomusic.cue, runner-nomusic.bin, runner-web.cue, and
runner-web.bin to the repository root build/ folder. Open
build/runner.cue in DuckStation for the disc with music. Open
build/runner-nomusic.cue for the same game without music tracks; menu
sounds are still there. build/runner-web.cue is that small disc again,
and it paints a command row so a browser can stream the mp3s.
docs/web.md is the row. All three are written every time this build runs.

XA is the smaller music encoding. Configure with -DRUNNER_XA_MUSIC=ON
before building. That still writes all three discs.

To play the web disc in a browser the way the GitHub Pages site will,
from the web folder:

  npm start

Open http://127.0.0.1:8643/?game=runner . The picker is
http://127.0.0.1:8643/ . The site files live in web/. GitHub Pages
publishes that folder, plus build/runner-web.bin and runner/audio.

CONTROLS
--------
  Start or X     skip the studio logo, leave the title screen
  Up / down      on the select screen, choose RUNNER or WORLD
  Left / right   on the select screen, change the chosen row
  X or Start     on the select screen, start the run
  Triangle       on the select screen, back to the title
  Left / right   during a run, change lane
  X              jump
  Start          pause; Start again continues
  Up / down, X   choose a pause or wipeout menu item

The pause menu offers CONTINUE and QUIT TO TITLE. The game stays frozen
behind it and the music keeps playing at a lower volume. After a wipeout
the menu offers RUN AGAIN, CHANGE RUNNER, and QUIT TO TITLE.

CD audio plays from the title screen on and starts again when the track
ends.
