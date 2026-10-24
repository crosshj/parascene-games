#!/bin/sh
# Builds the level preview for the host and writes PNG stills of every
# level to the given folder (default runner/build/preview).
set -e
here=$(cd "$(dirname "$0")" && pwd)
src="$here/../../src"
out=${1:-"$here/../../build/preview"}
sdk=${PSN00BSDK:-$HOME/psn00bsdk}
mkdir -p "$out"
python3 "$here/../make_lumen_sky.py" "$out/lumen_sky.h" "$out/lumen_sky.png"
python3 "$here/../pack_sprites.py" "$src/../sprites" "$out/sprite_sheet.h"
python3 "$here/../bake_tracks.py"
python3 "$here/../bake_path_runtime.py" --header "$out/path_runtime_data.h" --report "$out/path_runtime_report.json"
python3 "$here/../bake_vesper.py" "$out/vesper_facades.h"
python3 "$here/../bake_worlds.py" --manifest "$here/../../worlds/reach.json" --header "$out/reach_world_data.h" --report "$out/reach_world_report.json"
python3 "$here/../bake_worlds.py" --manifest "$here/../../worlds/reach-road.json" --header "$out/road_world_data.h" --report "$out/road_world_report.json"
cc -std=gnu99 -O2 -Wall -Wextra -Wno-unused-parameter -DRUNNER_PREVIEW \
    ${RUNNER_PREVIEW_FLAGS:-} \
    -I "$here/host" -idirafter "$sdk/include/libpsn00b" -I "$src" -I "$out" \
    "$here/preview.c" "$src/countdown.c" "$src/world.c" "$src/course.c" "$src/path.c" "$src/road.c" "$src/levels.c" "$src"/level_*.c \
    -o "$out/preview"
if [ "$#" -gt 1 ]; then
    "$out/preview" "$out" "$2"
else
    "$out/preview" "$out"
fi
