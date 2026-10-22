#!/usr/bin/env python3
"""Compile static world geometry and conservative, distance-indexed LOD lists.

The host sampler uses path.c, not a second implementation of the track camera.
Only this finite, unbranched prototype is supported; other tracks retain their
existing renderer. The output is resident C data, not a streaming/file ABI.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def project(p, camera):
    d = [p[i] - camera[i] for i in range(3)]
    x, y, z = [sum(d[j] * camera[3 + i * 3 + j] for j in range(3)) / 4096
               for i in range(3)]
    if z < 64:
        return None
    return x * 160 / z, (170 - y) * 160 / z, z


def span_ok(rings, a, b, cameras, error):
    """Bound projected chord error at every omitted authored cross-section."""
    # An exactly straight span has zero error for every camera pose. Avoid
    # repeatedly projecting it, and keep this guarantee stronger than sampling.
    if all((rings[mid][k][j] - rings[a][k][j]) * (b - a) ==
           (rings[b][k][j] - rings[a][k][j]) * (mid - a)
           for mid in range(a + 1, b) for k in range(6) for j in range(3)):
        return True
    for camera in cameras:
        for k in range(6):
            for mid in range(a + 1, b):
                p = rings[mid][k]
                t = (mid - a) / (b - a)
                chord = [rings[a][k][j] * (1 - t) + rings[b][k][j] * t for j in range(3)]
                actual, approx = project(p, camera), project(chord, camera)
                if actual is None or approx is None:
                    return False
                if max(abs(actual[j] - approx[j]) for j in range(2)) > error:
                    return False
    return True


def schedules_for(rings, cameras, bands, cfg):
    schedules, indices, levels = [], [], [0] * 4
    step = cfg['step']
    ahead = cfg['lookahead'] // step
    for band in range(bands):
        poses = cameras[band * 2:band * 2 + 3]
        a, end = max(0, band - 1), band + ahead
        sequence = [a]
        while a < end:
            depths = [project(p, c) for c in poses for p in rings[a]]
            depth = min((p[2] if p else 0) for p in depths)
            lod = sum(depth >= d for d in cfg['lod_depths'])
            stride = cfg['lod_strides'][lod]
            b = min(a + stride, end)
            while b > a + 1 and not span_ok(rings, a, b, poses, cfg['max_error_px']):
                b = a + max(1, (b - a) // 2)
            levels[(b - a).bit_length() - 1] += 1
            sequence.append(b)
            a = b
        if len(sequence) > cfg['max_schedule_rings']:
            raise ValueError(f'band {band}: {len(sequence)} rings exceeds schedule budget')
        if sequence != sorted(set(sequence)) or sequence[-1] >= len(rings):
            raise ValueError('invalid schedule coverage or indices')
        schedules.append((len(indices), len(sequence)))
        relative = [r - max(0, band - 1) for r in sequence]
        if max(relative) > 255:
            raise ValueError('schedule offsets overflow uint8')
        indices.extend(relative)
    return schedules, indices, levels


def compile_world(cfg, baked, sampler):
    if cfg['version'] != 1 or cfg['step'] != 256:
        raise ValueError('unsupported world version or sample step')
    if cfg['lod_strides'] != [1, 2, 4, 8] or cfg['lod_depths'] != sorted(cfg['lod_depths']):
        raise ValueError('expected ordered four-level LOD hierarchy')
    if cfg['lookahead'] % 256 or cfg['finish_allowance'] % 256 or cfg['max_error_px'] <= 0:
        raise ValueError('invalid coverage or error tolerance')
    groups, aliases = [], []
    for track in cfg['tracks']:
        authored = next(t for t in baked['tracks'] if t['id'] == track)
        if authored['span']['repeat'] != 'ends' or any(s['chain'] != 'main' for s in authored['segments']):
            raise ValueError(f'{track}: prototype requires a finite, unbranched track')
        length = authored['span']['s1']
        bands = (length + cfg['finish_allowance']) // 256 + 1
        limit = (bands + cfg['lookahead'] // 256) * 256
        sampled = json.loads(subprocess.check_output([
            str(sampler), track, '256', str(limit), str(cfg['outside']),
            str(cfg['wall_height']), str(cfg['ceiling_height'])]))
        if sampled['length'] != length:
            raise ValueError(f'{track}: track length mismatch')
        rings = sampled['rings']
        if len(rings) > 65535 or any(abs(v) > 0x3fffffff for r in rings for p in r for v in p):
            raise ValueError('world coordinates or ring IDs overflow')
        # Share identical geometry across stages with different obstacles/colors.
        group = next((i for i, g in enumerate(groups) if g['rings'] == rings and g['bands'] == bands), None)
        if group is None:
            schedules, indices, levels = schedules_for(rings, sampled['cameras'], bands, cfg)
            group = len(groups)
            axis_aligned = all(
                r[1] == [r[0][0], r[0][1] + cfg['wall_height'], r[0][2]] and
                r[2] == [r[0][0], r[0][1] + cfg['ceiling_height'], r[0][2]] and
                r[5][1:] == r[0][1:] and
                r[3] == [r[5][0], r[5][1] + cfg['ceiling_height'], r[5][2]] and
                r[4] == [r[5][0], r[5][1] + cfg['wall_height'], r[5][2]] for r in rings)
            groups.append(dict(rings=rings, schedules=schedules, indices=indices, bands=bands,
                               levels=levels, axis_aligned=axis_aligned))
        aliases.append((track, group))
    return groups, aliases


def emit(groups, aliases, cfg):
    lines = ['/* Generated by tools/bake_worlds.py. Include world_mesh.h first. */']
    resident = 0
    for i, group in enumerate(groups):
        rings, schedules, indices = (group[k] for k in ('rings', 'schedules', 'indices'))
        resident += len(rings) * 44 + len(schedules) * 8 + len(indices)
        lines.append(f'static const WorldRing world_rings_{i}[] = {{')
        for ring in rings:
            offsets = [[p[j] - ring[0][j] for j in range(3)] for p in ring[1:]]
            if any(v < -32768 or v > 32767 for p in offsets for v in p):
                raise ValueError('ring-local coordinates overflow int16')
            lines.append('    {{' + ','.join(map(str, ring[0])) + '},{' +
                         ','.join('{' + ','.join(map(str, p)) + '}' for p in offsets) + '}},')
        lines += ['};', f'static const WorldSchedule world_schedules_{i}[] = {{']
        lines.extend(f'    {{{start},{count},0}},' for start, count in schedules)
        lines += ['};', f'static const uint8_t world_indices_{i}[] = {{']
        lines.extend('    ' + ','.join(map(str, indices[j:j + 24])) + ',' for j in range(0, len(indices), 24))
        lines.append('};')
    resident += len(aliases) * 24  # 32-bit console pointers; excludes tiny strings/alignment.
    if resident > cfg['max_resident_bytes']:
        raise ValueError(f'{resident} resident bytes exceeds {cfg["max_resident_bytes"]}')
    lines.append('static const WorldMesh world_meshes[] = {')
    for track, i in aliases:
        aligned = int(groups[i].get('axis_aligned', False))
        lines.append(f'    {{"{track}",world_rings_{i},world_schedules_{i},world_indices_{i},{groups[i]["bands"]},1,{aligned},0}},')
    lines += ['};', '#define WORLD_MESH_COUNT (sizeof(world_meshes) / sizeof(world_meshes[0]))', '']
    return '\n'.join(lines), resident


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--header', type=Path, required=True)
    ap.add_argument('--report', type=Path, required=True)
    ap.add_argument('--manifest', type=Path, default=ROOT / 'worlds/sine.json')
    ap.add_argument('--tracks', type=Path, default=ROOT / 'tools/trackview/baked.json')
    ap.add_argument('--cc', default=os.environ.get('HOST_CC', 'cc'))
    args = ap.parse_args()
    cfg = json.loads(args.manifest.read_text())
    with tempfile.TemporaryDirectory(prefix='runner-world-') as temp:
        sampler = Path(temp) / 'sample-world'
        subprocess.run([args.cc, '-std=gnu99', '-O2', '-DRUNNER_PREVIEW', '-I', str(ROOT / 'src'),
                        str(ROOT / 'tools/sample_world.c'), '-o', str(sampler)], check=True)
        groups, aliases = compile_world(cfg, json.loads(args.tracks.read_text()), sampler)
    header, resident = emit(groups, aliases, cfg)
    sources = [args.manifest, args.tracks, Path(__file__), ROOT / 'tools/sample_world.c',
               ROOT / 'src/path.c', ROOT / 'src/path.h', ROOT / 'src/course.h', ROOT / 'src/world.h']
    report = dict(version=1, tracks=[t for t, _ in aliases], resident_bytes=resident,
                  geometry_groups=len(groups), source_sha256=hashlib.sha256(b''.join(p.read_bytes() for p in sources)).hexdigest(),
                  max_error_px=cfg['max_error_px'], camera_samples_per_band=3,
                  lookahead=cfg['lookahead'], groups=[dict(rings=len(g['rings']), schedules=len(g['schedules']),
                  max_schedule_rings=max(n for _, n in g['schedules']), lod_span_counts=g['levels'],
                  axis_aligned=g['axis_aligned']) for g in groups])
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text(header)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(f'baked worlds: {resident} resident bytes, {len(groups)} shared geometry groups')


if __name__ == '__main__':
    main()
