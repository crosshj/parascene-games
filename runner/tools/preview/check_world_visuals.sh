#!/bin/sh
# Same-state image comparisons plus proof of visible distant scenery pixels.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-"$here/../../build/world-visuals"}
result=0
PSX15=1 RUNNER_PREVIEW_FLAGS= sh "$here/preview.sh" "$out" --compare-review || result=$?
if [ -x "$out/preview" ]; then
    cc -std=gnu99 -DHIT_FEEDBACK_TEST -I "$here/../../src" \
        "$here/../../src/hit_feedback.c" "$here/../check_hit_feedback.c" \
        -o "$out/check_hit_feedback" && "$out/check_hit_feedback" || result=$?
    "$out/preview" "$out" --shake-check || result=$?
    PSX15=1 "$out/preview" "$out" --finish-gate-check || result=$?
    PSX15=1 "$out/preview" "$out" --runner-lighting-check || result=$?
    PSX15=1 "$out/preview" "$out" --atmosphere-check || result=$?
    python3 "$here/atmosphere_report.py" "$out" || result=$?
    PSX15=1 "$out/preview" "$out" --mesh-check || result=$?
    python3 "$here/reach_report.py" "$out" || result=$?
    PSX15=1 "$out/preview" "$out" --pipeline-check || result=$?
    PSX15=1 "$out/preview" "$out" --countdown-check || result=$?
    PSX15=1 "$out/preview" "$out" --city-check || result=$?
    PSX15=1 "$out/preview" "$out" --vesper-motion-check || result=$?
    "$out/preview" "$out" --visibility-self-test || result=$?
    PSX15=1 "$out/preview" "$out" --tunnel-check || result=$?
fi
if [ -f "$out/comparison.csv" ] && [ -f "$out/tunnel.csv" ]; then
    python3 "$here/visual_report.py" "$out"
fi
exit "$result"
