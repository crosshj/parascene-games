# Runner ambient fill

Gameplay lighting previously only multiplied palette colors by the world's
`Light.tint`. Near-black texels therefore stayed stark against pale environments.
`runner_lighting.h` now adds diffuse sky fill to shadow texels in bright,
low-saturation worlds before the existing GPU tint. White highlights stay intact,
zero remains transparent, and the texel's STP bit is preserved. The least-bright
sky channel controls strength, so dark or strongly saturated worlds are unchanged.

`Light.runner_ambient` optionally supplies authored RGB shadow fill instead of
that sky estimate. Meridian uses cool fill in stages 1 and 2; Shatter uses warm
fill in stages 1 and 2. Their darker third stages retain the automatic treatment.
This accounts for bright ground under a dark or saturated sky, where the initial
heuristic was too weak. The palette cache includes both sky and authored fill.

The gameplay runner uses one separate 16-color palette at (912, 0), beside the
source character palettes at (896, 0..). This is clear of sprite pages, screen
art, the font at (960, 0), and Lumenwild's texture beginning at y=64. Menu art
continues using the source palettes. If VRAM packing changes, reserve this slot.
Only character or ambient-color changes transform/upload the 32-byte palette;
there is no extra sprite pass, blending layer, or recurring palette upload.

`--runner-lighting-check` in the host preview exercises all 65,536 palette values
for transparency, STP preservation, and dark/saturated invariance. It renders
before/after pairs with the packed character sprite and current world geometry,
including separate RGB texture modulation, rather than the usual placeholder.
The check covers 12 world/stage pairs and all 65,536 palette words for each
stage. Meridian and Shatter's first two stages change; their third stages and
all Reach/Sineworks stages remain pixel-identical to unmodified palette shading.
The check is part of `check_world_visuals`. The authored-fill review includes
18 scene pairs at three positions per stage, in
`build/runner-world-review/index.html` (host renders, not emulator captures).

# Finish gates

`road_gantry()` is for decoration. `road_finish_gantry()` accepts only the active
track's authored finish. `road_scenery_gate_allowed()` reserves the final FAR
(4,200) units and all positions beyond the finish; it reads the stage catalog,
not a duplicated world-specific finish constant. Endless arcade tracks have no
finish exclusion. Cathedral arches, both Sineworks rib paths, and the independent
arcade City renderer also use this rule. Existing stricter City cutoffs remain.

`--finish-gate-check` sweeps 693 camera positions across all 21 story stages,
checks submitted decorative gates/ribs, attempts forbidden gate allocations,
verifies the actual finish still produces geometry, and checks switching back to
an unrestricted arcade track. It runs in `check_world_visuals`.
