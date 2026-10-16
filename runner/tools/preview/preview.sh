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
python3 "$here/../bake_tracks.py"
cc -std=gnu99 -O2 -Wall -Wextra -Wno-unused-parameter -DRUNNER_PREVIEW \
    -I "$here/host" -idirafter "$sdk/include/libpsn00b" -I "$src" -I "$out" \
    "$here/preview.c" "$src/world.c" "$src/course.c" "$src/levels.c" "$src"/level_*.c \
    -o "$out/preview"
"$out/preview" "$out"
