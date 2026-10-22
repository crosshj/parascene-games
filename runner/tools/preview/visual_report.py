"""Native-resolution evidence for the baked-vs-procedural visual checks."""
import csv
import html
import sys
from pathlib import Path

out = Path(sys.argv[1])
if (out / 'tunnel.csv').exists():
    checks = list(csv.DictReader((out / 'tunnel.csv').open()))
    parts = ['<!doctype html><meta charset="utf-8"><title>Sineworks tunnel validation</title>',
        '<style>body{background:#15171b;color:#ddd;font:16px system-ui;margin:24px}'
        'img{image-rendering:pixelated;width:640px;max-width:100%}'
        '.row{display:flex;flex-wrap:wrap;gap:16px}figure{margin:0 0 24px}'
        'p{max-width:960px}a{color:#8de}</style>',
        '<h1>Sineworks: straight tunnel validation</h1>',
        '<p>The active renderer uses six perspective faces extending to one '
        'vanishing point. There is no end-cap polygon. The road has a shaded '
        'infinite continuation ending at exactly the same point and black fog color. '
        'The tunnel width is fixed; lane narrowing changes the road within it.</p>',
        f'<p>{len(checks)} native 15-bit test views: every shell covers all 76,800 '
        'screen pixels with exactly six faces. Tests require a fading wall gradient, '
        'road contribution at the vanishing-point pixel, and distant rib contributions. '
        'The visibility engine separately tests opaque occlusion, transparent '
        'contributions, and off-screen geometry.</p>',
        '<p>The old renderer is retained for visual review. Pixel equality is not '
        'the acceptance criterion for this deliberate tunnel/fog redesign.</p>']
    for stage in range(3):
        parts.append(f'<h2>Stage {stage}, scroll 2,600</h2><div class="row">')
        for name, label in [(f'tunnel_stage{stage}_s2600.png','Complete scene'),
                            (f'shell_stage{stage}_s2600.png','Actual six-face shell, isolated')]:
            parts.append(f'<figure><figcaption>{label}</figcaption><img src="{name}"></figure>')
        parts.append('</div>')
    if (out / 'comparison.csv').exists():
        for stage in range(3):
            parts.append(f'<h2>Stage {stage}: largest change from the old renderer</h2><div class="row">')
            for tag,label in [('legacy','Original'),('baked','New straight tunnel')]:
                parts.append(f'<figure><figcaption>{label}</figcaption><img src="stage{stage}_{tag}.png"></figure>')
            parts.append('</div>')
    parts.append('<p><a href="tunnel.csv">Functional test measurements</a> · '
                 '<a href="comparison.csv">Full matched-frame comparison</a></p>')
    (out / 'index.html').write_text('\n'.join(parts))
    print(f'tunnel report: {out / "index.html"}')
    sys.exit(0)
rows = list(csv.DictReader((out / 'comparison.csv').open()))
distance = list(csv.DictReader((out / 'distance.csv').open()))
body = ['<!doctype html><meta charset="utf-8"><title>Sineworks visual checks</title>',
        '<style>body{background:#15171b;color:#ddd;font:16px system-ui;margin:24px}'
        'img{image-rendering:pixelated;width:480px;max-width:100%}'
        '.row{display:flex;flex-wrap:wrap;gap:16px}figure{margin:0 0 24px}'
        'a{color:#8de}td,th{padding:8px;text-align:left}p{max-width:900px}</style>',
        '<h1>Sineworks visual checks</h1>',
        f'<p>{len(rows):,} identical-state comparisons over all three tracks, at four offsets '
        'in every 256-unit band through the finish. The runner stand-in, effects and animation '
        'clock match in each pair. The images below show the worst mean difference '
        'per stage. Difference images amplify channel differences by eight.</p>',
        '<p>Pass limits: mean channel difference ≤0.02 on a 0–255 scale, and no more '
        'than 16 pixels with a channel difference above 24. This permits small '
        'native-resolution shading/edge differences; it does not require identical '
        'polygon layouts.</p><table><tr><th>Stage</th><th>Frames</th><th>Worst mean delta</th>'
        '<th>Maximum strong pixels</th></tr>']
for stage in range(3):
    subset = [r for r in rows if int(r['stage']) == stage]
    mean = max(float(r['mean_channel_delta']) for r in subset)
    strong = max(int(r['strong_pixels']) for r in subset)
    body.append(f'<tr><td>{stage}</td><td>{len(subset)}</td><td>{mean:.6f}</td><td>{strong}</td></tr>')
body.append('</table>')
if (out / 'before_stage1_baked.png').exists():
    body.append('<h2>Regression found and fixed: stage 1, scroll 62,976</h2>'
                '<p>The original prototype exposed bright conduit bars because its '
                'different wall subdivision changed depth ordering. Keeping the '
                'procedural near region preserves the reference occlusion.</p><div class="row">')
    for image, label in [('before_stage1_legacy.png', 'Procedural reference'),
                         ('before_stage1_baked.png', 'Original baked prototype: regression'),
                         ('fixed_stage1_62976.png', 'Corrected renderer, same frame')]:
        body.append(f'<figure><figcaption>{label}</figcaption><img src="{image}"></figure>')
    body.append('</div>')
for stage in range(3):
    body.append(f'<h2>Stage {stage}: worst mean difference</h2><div class="row">')
    for tag, label in [('legacy', 'Procedural reference'), ('baked', 'Corrected baked continuation'),
                       ('difference_x8', 'Difference ×8')]:
        body.append(f'<figure><figcaption>{label}</figcaption><img src="stage{stage}_{tag}.png"></figure>')
    body.append('</div>')
body += ['<h2>Distant walls surviving the renderer</h2>',
         '<p>Scenery primitives use a diagnostic palette by sorting depth: '
         'red below 1,600; yellow 1,600–5,000; green 5,000–10,000; blue '
         '10,000–20,000. Colors bypass haze for inspection. Road rendering and '
         'occlusion still apply. These are actual final pixels, not projected '
         'bounds. Zoom into the vanishing point to inspect the blue far shell.</p>',
         '<p>This verifies the tunnel shell. Ribs and conduits still have the '
         'existing nearby-only schedules; persistent distant props are not '
         'implemented by this prototype.</p>',
         f'<p>{len(distance)} distance frames: '
         f'{min(int(r["visible_5000_10000_px"]) for r in distance)} minimum green pixels, '
         f'{min(int(r["visible_10000_20000_px"]) for r in distance)} minimum blue pixels.</p>']
for stage in range(3):
    body.append(f'<h2>Stage {stage}: distance view at scroll 13,312</h2><div class="row">')
    for mode in ['legacy', 'baked']:
        body.append(f'<figure><figcaption>{html.escape(mode)}</figcaption>'
                    f'<img src="depth_stage{stage}_s13312_{mode}.png">'
                    f'<figcaption>Vanishing point ×10</figcaption><canvas width="320" height="320" '
                    f'data-src="depth_stage{stage}_s13312_{mode}.png"></canvas></figure>')
    body.append('</div>')
body.append('<p><a href="comparison.csv">Full comparison CSV</a> · '
            '<a href="distance.csv">Distance pixel counts</a></p>')
body.append('<p>These are native-resolution host renders. Console footage remains '
            'the final check for GPU-specific artifacts.</p>')
body.append('<script>document.querySelectorAll("canvas[data-src]").forEach(c=>{'
            'const im=new Image();im.onload=()=>{const x=c.getContext("2d");'
            'x.imageSmoothingEnabled=false;x.drawImage(im,144,56,32,32,0,0,320,320)};'
            'im.src=c.dataset.src});</script>')
(out / 'index.html').write_text('\n'.join(body))
print(f'visual report: {out / "index.html"}')
