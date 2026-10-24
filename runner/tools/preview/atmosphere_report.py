#!/usr/bin/env python3
"""Review the Reach horizon contract sweep and optional captured before images."""
import csv
from pathlib import Path
import sys

out = Path(sys.argv[1])
rows = list(csv.DictReader((out / 'atmosphere.csv').open()))
parts = ['<!doctype html><meta charset="utf-8"><title>Reach atmosphere review</title>',
         '<style>body{background:#16141d;color:#eee;font:16px system-ui;max-width:1060px;margin:32px auto}img{width:320px;height:240px;image-rendering:pixelated}figure{display:inline-block;margin:8px}section{margin:32px 0}a{color:#bbf}</style>',
         '<h1>Reach: atmosphere and sightlines</h1>',
         '<p>Native 320×240 host previews. The block represents the runner; console timing is measured separately.</p>',
         f'<p>{len(rows)} camera poses, {sum(int(r["failures"]) for r in rows)} atmosphere contract failures. '
         '<a href="atmosphere.csv">All results and rendering work</a>.</p>',
         '<p>Road, ground, canyon and backdrop bases converge to a common dark horizon. '
         'The atmospheric tint is separate. Peaks and ruins retain only subtle contrast.</p>']
for stage, name in enumerate(['Ridge', 'Climb', 'Drop']):
    parts.append(f'<h2>{name}</h2>')
    comparisons = sorted(out.glob(f'before-{stage}-*.png'), key=lambda p: int(p.stem.split('-')[-1]))
    if comparisons:
        for before in comparisons:
            scroll = before.stem.split('-')[-1]
            after = out / f'after-{stage}-{scroll}.png'
            if not after.exists():
                continue
            parts.append(f'<section><h3>Progress {scroll}</h3>')
            for path, label in [(before, 'Before'), (after, 'Updated atmosphere and route')]:
                parts.append(f'<figure><figcaption>{label}</figcaption><img src="{path.name}" alt="{name}: {label}"></figure>')
            parts.append('</section>')
    parts.append('<details><summary>Full route: one view every 8,192 units</summary>')
    for path in sorted(out.glob(f'atmosphere-{stage}-*.png'), key=lambda p: int(p.stem.split('-')[-1])):
        parts.append(f'<figure><figcaption>{path.stem.split("-")[-1]}</figcaption><img src="{path.name}" alt="{name} route sweep"></figure>')
    parts.append('</details>')
(out / 'atmosphere-review.html').write_text('\n'.join(parts) + '\n')
print(out / 'atmosphere-review.html')
