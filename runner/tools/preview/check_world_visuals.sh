#!/bin/sh
# Same-state image comparisons plus proof of visible distant scenery pixels.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-"$here/../../build/world-visuals"}
result=0
PSX15=1 RUNNER_PREVIEW_FLAGS= sh "$here/preview.sh" "$out" --compare-review || result=$?
if [ -x "$out/preview" ]; then
    "$out/preview" "$out" --visibility-self-test || result=$?
    PSX15=1 "$out/preview" "$out" --tunnel-check || result=$?
fi
if [ -f "$out/comparison.csv" ] && [ -f "$out/tunnel.csv" ]; then
    python3 "$here/visual_report.py" "$out"
fi
exit "$result"
