runner
======

A PlayStation disc: a 3D road, a flat animated runner, a menu, and
audio/runner1.mp3 as a CD audio track.

The disc opens on the Parascene Games logo for about three seconds, then
the title screen with the game logo and PRESS START. The music starts on
the title screen. Next comes the runner select screen, then the run.

screens/parascene.png and screens/title.png are the copies the game
shows. They are cropped and scaled from sprites/parascene_logo.png and
sprites/game logo.png, at most 256 pixels wide, and the build turns each
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
The base sheet is on the select screen too, as RUNNER. Left and right
switch characters, and the build reads the cells from runner.txt. Empty pixels
are magenta, #FF00FF, and so is any pixel with alpha below 128. Each character is reduced to 15 colors.

Regenerate the runner guide with:

  python3 runner/tools/make_sprites.py

That rewrites runner.png and runner.txt and leaves character PNGs in
place. Paint over the guide, keep the same cells, and save it under the
character name. Feet stay on the bottom edge of the cell. The tuck is
the frame whose feet pull up. Bright magenta inside a character is
treated as empty.

The road, shoulders, buildings, gantries, and obstacles are flat
polygons.

HOW TO BUILD
------------
Needs PSn00bSDK, mipsel-none-elf-gcc, cmake, and ninja. macOS afconvert
turns audio/runner1.mp3 into the 44100 Hz stereo track the disc format requires.

From the repository root:

  export PSN00BSDK=$HOME/psn00bsdk
  export PATH="$PSN00BSDK/bin:$PATH"
  cmake -S runner -B runner/build -G Ninja
  cmake --build runner/build

The compile stays in runner/build. Only runner.cue and runner.bin are
copied to the repository root build/ folder. Open build/runner.cue in
DuckStation (runner.bin sits beside it).

CONTROLS
--------
  Start or X     skip the studio logo, leave the title screen
  Left / right   on the select screen, pick a runner
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
