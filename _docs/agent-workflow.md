agent workflow - any game in this repo
======================================
"when:" line under each heading = situation it applies to

layout
when: starting a new game, or unsure where files go
- one dir per game: <game>/ (src, tools, docs, assets, CMakeLists.txt)
- disc images only in root build/: <game>.cue + <game>.bin
- per-game build dir: <game>/build (never root build/)
- runner/ = reference game; copy its tooling patterns

loop
when: any feature or fix, any game
- read before edit: grep symbol, read whole fn
- small change -> generate data -> host preview -> look at png -> host tests -> disc
- visual claim -> backed by a still; logic/geometry claim -> backed by a test
- update <game>/docs with each feature
- don't commit unless asked

disc build (PS1, PSn00bSDK)
when: end of every change; user tests in DuckStation from build/<game>.cue
- cmake -S <game> -B <game>/build -G Ninja && cmake --build <game>/build
- grep log: warning|error
- src via file(GLOB ... src/*.c CONFIGURE_DEPENDS) -> new files picked up; reconfigure if not
- generators (python tools) run as cmake custom commands -> data stays in sync

host preview (the key trick: see frames without an emulator)
when: anything visual - rendering, colors, camera, new level, perf; game has no preview yet -> port runner's first
- build game draw code for the host with -DRUNNER_PREVIEW-style define
- tools/preview/host/: stub psn00b headers + fake GTE (rtps, stsxy) + tiny rasterizer (F4, G4, lines, sprites) -> PNG
- preview.sh: compile preview.c + game src, run, write stills to a dir
- shots table in preview.c main(): {tag, time/scroll, speed}; add entries that hit the feature
- also print per frame cost: prim counts, bytes of draw buffer, projections, fill px; plus N-second run avg/max -> perf budget check
- keep host stubs in sync with any new prim type the game emits (else silently missing)
- runner example: sh runner/tools/preview/preview.sh /tmp/pv

inspecting stills
when: after each preview render; user reports a visual glitch
- upscale: sips -z 720 960 in.png --out in_x3.png
- crop detail: sips -c H W --cropOffset Y X in.png --out crop.png, then upscale
- odd pixels: decode png in python (zlib + row filters), print rgb along a row; 1-level steps = not a bug
- read images with the Read tool; embed in reply

host tests
when: math, geometry, game rules, data invariants - anything with a checkable number
- tools/check_*.c: compile game logic files (no GPU) for host, assert with tolerance near(a, b, tol)
- missing symbols at link -> stub them in the test file
- print one ok line, exit 0; nonzero + message on failure
- runner example:
  sdk=${PSN00BSDK:-$HOME/psn00bsdk}
  cd runner && cc -std=gnu99 -O2 -Wall -Wextra -DRUNNER_PREVIEW -I tools/preview/host -idirafter "$sdk/include/libpsn00b" -I src tools/check_track.c src/path.c src/course.c -o /tmp/check_track && /tmp/check_track

data-driven content
when: game has authored content (tracks, levels, waves, maps) that should live outside C
- author in json (catalog), bake with python -> C header + json for tools
- baker validates: units, limits, links, closure; prints one status line per item, want warnings 0
- tune numbers by importing the baker in a throwaway python script and grid-searching, not by hand
- runner example: python3 runner/tools/bake_tracks.py (tracks/catalog.json -> src/track_data.h, tools/trackview/baked.json)

browser tools (three.js viewers etc)
when: content is easier to judge from above / outside the game camera; checking authored data before it runs
- self-contained index.html with data inlined -> opens via file://
- hash selects item (#id) -> deep-linkable screenshots
- headless shot:
  "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new --disable-gpu --use-angle=swiftshader --enable-unsafe-swiftshader --window-size=2000,1000 --virtual-time-budget=4000 --screenshot=/tmp/v.png "file://$PWD/<game>/tools/<viewer>/index.html#<id>"
- wide window so ui panels don't cover content

PS1 rendering notes
when: writing or debugging draw code in any game
- ordering table: bucket 0 draws last; same bucket -> later-linked draws first -> link overlays before what they cover
- GPU drops prims with huge screen spans -> subdivide big quads
- 15-bit color -> tiny color diffs vanish on hardware
- keep shared drawing in one module; worlds/levels pass style structs (materials, light) -> new level = few lines

new level/world pattern
when: adding a level/world/stage to a game that has a levels table
- level file: style structs + clear() + draw() + Level {name, data id, ...}
- register in levels table; preview globs level_*.c
- runner example: copy runner/src/level_hill.c, add to runner/src/levels.c

habits
when: always
- zsh: unmatched glob aborts whole command -> no rm -f with globs
- parallel tool calls for independent reads/renders
- pipe noisy output through rg for warning|error|key lines
- block_until_ms generous for builds/renders
