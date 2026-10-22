"""Summarize stable scenery IDs after actual painter-order rasterization.

Never-observed assets are review candidates, not proof that sparse sampling
permits deleting them. Transparent draws conservatively retain contributors.
"""
import argparse
import csv
import html
import json
from collections import Counter
from pathlib import Path


def ranges(values, step):
    result = []
    for value in sorted(set(values)):
        if result and value <= result[-1][1] + step:
            result[-1][1] = value
        else:
            result.append([value, value])
    return result


def summarize(rows):
    objects, poses = {}, set()
    for row in rows:
        stage, scroll, instance = (int(row[k]) for k in ('stage', 'scroll', 'instance'))
        key = (stage, row['kind'], instance)
        obj = objects.setdefault(key, dict(stage=stage, kind=row['kind'], instance=instance,
            considered=0, submitted=0, observed=0, occluded=0, outside_view=0,
            max_visible_pixels=0, max_raster_pixels=0, lod_frames=Counter(),
            visible_at=[], pixel_candidate_at=[], invisible_at=[]))
        if scroll < 0:
            continue
        poses.add((stage, scroll))
        raster, visible, primitives = (int(row[k]) for k in ('raster_pixels', 'visible_pixels', 'primitives'))
        obj['considered'] += 1
        obj['submitted'] += primitives > 0
        obj['observed'] += visible > 0
        obj['occluded'] += raster > 0 and visible == 0
        obj['outside_view'] += raster == 0
        obj['max_visible_pixels'] = max(visible, obj['max_visible_pixels'])
        obj['max_raster_pixels'] = max(raster, obj['max_raster_pixels'])
        obj['lod_frames'][row['representation']] += 1
        obj['visible_at' if visible else 'invisible_at'].append(scroll)
        if raster == 1 and visible:
            obj['pixel_candidate_at'].append(scroll)
    scrolls = sorted(s for st, s in poses if st == 0)
    step = min((b-a for a, b in zip(scrolls, scrolls[1:])), default=1)
    for obj in objects.values():
        for key in ('visible_at', 'invisible_at', 'pixel_candidate_at'):
            obj[key.replace('_at', '_ranges')] = ranges(obj.pop(key), step)
        obj['never_observed'] = obj['observed'] == 0
        obj['lod_frames'] = dict(obj['lod_frames'])
    return dict(version=1, camera_step=step, camera_poses=len(poses),
                scope='Sineworks scenery, all three finite unbranched routes',
                evidence='Final raster contributors with conservative transparency; PSX15 host renderer',
                automatic_removal_safe=False,
                caveat='Sampled visibility is not a proof over every camera position or animation state. '
                       'Run --visibility 1 for every integer track position at the selected animation schedule. '
                       'Do not delete sparse-sweep candidates without conservative bounds or exhaustive validation.',
                assets=sorted(objects.values(), key=lambda o: (o['stage'], o['kind'], o['instance'])))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    report = summarize(csv.DictReader((args.directory / 'visibility.csv').open()))
    (args.directory / 'visibility.json').write_text(json.dumps(report, indent=2) + '\n')
    assets = report['assets']
    never = sum(a['never_observed'] for a in assets)
    pixel = sum(bool(a['pixel_candidate_ranges']) for a in assets)
    content = ['<!doctype html><meta charset="utf-8"><title>Scenery visibility simulation</title>',
        '<style>body{background:#15171b;color:#ddd;font:15px system-ui;margin:24px}'
        'td,th{padding:7px;border-bottom:1px solid #444;text-align:left}'
        'p{max-width:960px}a{color:#8de}.never{background:#473520}</style>',
        '<h1>Scenery visibility simulation</h1>',
        f'<p>{report["camera_poses"]:,} camera poses; {len(assets)} stable assets; '
        f'{never} never-observed review candidates; {pixel} assets with one-pixel intervals.</p>',
        '<p>This first instrumented sweep covers Sineworks scenery on all three routes. '
        'It tracks per-pixel object contributions through the real submission order, '
        'so it distinguishes objects outside the view from objects covered by opaque '
        'geometry. Transparent effects retain underlying contributors conservatively.</p>',
        f'<p>Camera spacing: {report["camera_step"]} world units. {html.escape(report["caveat"])}</p>',
        '<p>Ribs already use solid geometry nearby and silhouettes farther away. '
        'Whole arches outside the view are rejected before primitive submission. '
        'One-pixel intervals identify where a single pixel could replace the whole '
        'visible object; they are suggestions for further baking, not blanket removal rules.</p>',
        '<p><a href="visibility.json">Detailed ranges and LOD counts (JSON)</a> · '
        '<a href="visibility.csv">Every object/pose measurement (CSV)</a></p>',
        '<table><tr><th>Stage</th><th>Object</th><th>Observed / considered poses</th>'
        '<th>Occluded</th><th>Off-screen / degenerate</th><th>Max visible px</th><th>LOD poses</th></tr>']
    for obj in assets:
        cls = ' class="never"' if obj['never_observed'] else ''
        lod = ', '.join(f'{name}: {n}' for name, n in obj['lod_frames'].items())
        content.append(f'<tr{cls}><td>{obj["stage"]}</td><td>{obj["kind"]}:{obj["instance"]}</td>'
            f'<td>{obj["observed"]} / {obj["considered"]}</td><td>{obj["occluded"]}</td>'
            f'<td>{obj["outside_view"]}</td><td>{obj["max_visible_pixels"]}</td>'
            f'<td>{html.escape(lod)}</td></tr>')
    content.append('</table>')
    (args.directory / 'index.html').write_text('\n'.join(content))
    print(f'visibility: {len(assets)} assets, {never} never observed, {pixel} with one-pixel intervals')
    print(f'report: {args.directory / "index.html"}')


if __name__ == '__main__':
    main()
