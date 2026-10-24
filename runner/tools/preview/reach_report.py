#!/usr/bin/env python3
"""Native-size review of compiled Reach meshes against their procedural reference."""
import csv
from html import escape
from pathlib import Path
import sys

out=Path(sys.argv[1])
rows=list(csv.DictReader((out/'compiled-mesh.csv').open()))
parts=['<!doctype html><meta charset="utf-8"><title>Reach mesh review</title>',
 '<style>body{background:#181820;color:#eee;font:16px system-ui;max-width:1000px;margin:40px auto}section{margin:40px 0}figure{display:inline-block;margin:12px 20px 12px 0}img{image-rendering:pixelated;width:320px;height:240px}a{color:#aaf}small{color:#bbc}</style>',
 '<h1>Vesper Reach: compiled mesh review</h1>',
 '<p>These are 320×240 software-rendered reference/candidate pairs at the worst mean-difference position for each stage. Console timing is measured separately in DuckStation.</p>',
 '<p><a href="compiled-mesh.csv">All camera comparisons</a> · <a href="pipeline.csv">All-stage exact renderer checks</a></p>']
for stage,name in enumerate(['Ridge','Climb','Drop']):
    selected=[r for r in rows if int(r['stage'])==stage]
    worst=max(selected,key=lambda r:float(r['mean_delta']))
    parts.append(f'<section><h2>{escape(name)}</h2><p>{len(selected)} comparisons; displayed scroll {worst["scroll"]}. Worst mean channel difference: {float(worst["mean_delta"]):.4f}/255. Maximum residual high-contrast pixels beyond a one-pixel match: {max(int(r["unmatched_pixels"]) for r in selected)}.</p>')
    for mode,label in [('reference','Procedural reference'),('candidate','Compiled mesh')]:
        parts.append(f'<figure><figcaption>{label}</figcaption><img src="mesh-stage{stage}-{mode}.png" alt="{name} {label}"></figure>')
    parts.append('</section>')
parts.append('<small>Geometric LOD is checked independently against the actual path at intermediate camera and surface positions. The reference retains its old tessellation, so one-pixel outline and fixed-point shading differences are expected.</small>')
if (out/'meridian-mesh.csv').exists():
    rows=list(csv.DictReader((out/'meridian-mesh.csv').open()))
    parts += ['<section><h1>Pale Meridian: branch-aware wall bake review</h1>',
        f'<p>{len(rows)} matched native-size poses across all three stages compare the original procedural ice walls with the compiled walls. Worst mean channel difference: {max(float(r["mean_delta"]) for r in rows):.4f}/255; maximum high-contrast pixels without a one-pixel match: {max(int(r["unmatched_pixels"]) for r in rows)}. The separate route verifier exercises both fork selections.</p>',
        '<p><a href="meridian-mesh.csv">All Meridian comparisons</a></p>']
    for stage,name in enumerate(['The Drift','The Fork','The Verge']):
        route_count=2 if stage else 1
        for route in range(route_count):
            parts.append(f'<h2>{name} — route {route}</h2><figure><figcaption>Procedural walls</figcaption><img src="meridian-mesh-stage{stage}-route{route}-reference.png"></figure><figure><figcaption>Compiled walls</figcaption><img src="meridian-mesh-stage{stage}-route{route}-candidate.png"></figure>')
    parts.append('<small>The displayed images show the worst mean-difference pose across all stages. Meridian geometry LOD is checked independently at millions of intermediate visible samples; route-specific cuts and branch schedules are validated by the runtime mesh checker.</small></section>')
(out/'reach-review.html').write_text('\n'.join(parts)+'\n')
print(out/'reach-review.html')
