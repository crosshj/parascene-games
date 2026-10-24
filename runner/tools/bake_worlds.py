#!/usr/bin/env python3
"""Compile static world geometry and conservative, distance-indexed LOD lists.

The host sampler uses path.c, not a second implementation of the track camera.
Finite continuous tracks with bounded fork routes are supported; other tracks
retain their existing renderer. Cross-sections and LOD policy come from the
manifest. The output is resident C data, not a streaming/file ABI.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from bake_fork_walls import compile_forks

ROOT = Path(__file__).resolve().parents[1]


def project(p, camera):
    d = [p[i] - camera[i] for i in range(3)]
    x, y, z = [sum(d[j] * camera[3 + i * 3 + j] for j in range(3)) / 4096
               for i in range(3)]
    if z < 64:
        return None
    return x * 160 / z, (170 - y) * 160 / z, z


def span_ok(rings, a, b, cameras, error, fine_rings=None, fine_divisions=8):
    """Bound projected chord error at every omitted authored cross-section."""
    # An exactly straight span has zero error for every camera pose. Avoid
    # repeatedly projecting it, and keep this guarantee stronger than sampling.
    if fine_rings is None and all((rings[mid][k][j] - rings[a][k][j]) * (b - a) ==
           (rings[b][k][j] - rings[a][k][j]) * (mid - a)
           for mid in range(a + 1, b) for k in range(6) for j in range(3)):
        return True
    if fine_rings is not None and all(
            (fine_rings[mid][k][j]-rings[a][k][j]) * ((b-a)*fine_divisions) ==
            (rings[b][k][j]-rings[a][k][j]) * (mid-a*fine_divisions)
            for mid in range(a*fine_divisions+1,b*fine_divisions)
            for k in range(6) for j in range(3)):
        return True
    for camera in cameras:
        for k in range(6):
            samples=(range(a*fine_divisions+1,b*fine_divisions) if fine_rings is not None
                     else range((a+1)*fine_divisions,(b)*fine_divisions,fine_divisions))
            for mid in samples:
                p = fine_rings[mid][k] if fine_rings is not None else rings[mid//fine_divisions][k]
                t = (mid - a*fine_divisions) / ((b - a)*fine_divisions)
                chord = [rings[a][k][j] * (1 - t) + rings[b][k][j] * t for j in range(3)]
                actual, approx = project(p, camera), project(chord, camera)
                if actual is None or approx is None:
                    return False
                if max(abs(actual[j] - approx[j]) for j in range(2)) > error:
                    return False
    return True


def camera_interval(point, bounds, axis):
    """Conservative transform over every integer camera pose in a band."""
    lo=hi=0
    for component in range(3):
        d0,d1=point[component]-bounds[12+component],point[component]-bounds[component]
        index=3+axis*3+component
        f0,f1=bounds[index],bounds[12+index]
        products=(d0*f0,d0*f1,d1*f0,d1*f1)
        lo+=min(products);hi+=max(products)
    return lo/4096-32,hi/4096+32


def outside_planes(point,bounds):
    x0,x1=camera_interval(point,bounds,0)
    y0,y1=camera_interval(point,bounds,1)
    z0,z1=camera_interval(point,bounds,2)
    return (int(z1<0) | int(x1*160+256*z1<0)<<1 |
            int(-x0*160+256*z1<0)<<2 | int((170-y0)*160+168*z1<0)<<3 |
            int((y1-170)*160+264*z1<0)<<4)


def schedules_for(rings, cameras, bands, cfg, camera_bounds=None, cuts=None, fine_rings=None):
    schedules, indices, levels = [], [], [0] * 7
    cuts=set(cuts or ())
    step = cfg['step']
    ahead = cfg['lookahead'] // step
    for band in range(bands):
        poses = cameras[band * 4:band * 4 + 5]
        a, end = max(0, band - 1), band + ahead
        sequence = [a]
        hidden=[0]*len(rings)
        if camera_bounds is not None:
            for index in range(a,end+1):
                mask=31
                for p in rings[index]: mask &= outside_planes(p,camera_bounds[band])
                hidden[index]=mask
        while a < end:
            # A common outside half-space contains the entire fine mesh
            # and its chord for every camera pose, so merging cannot expose it.
            if hidden[a]:
                mask=hidden[a];hidden_end=a
                for index in range(a+1,min(a+max(cfg['lod_strides']),end)+1):
                    if index in cuts: break
                    mask &= hidden[index]
                    if not mask: break
                    hidden_end=index
                if hidden_end>a+1:
                    levels[(hidden_end-a).bit_length()-1]+=1
                    sequence.append(hidden_end);a=hidden_end;continue
            depths = [project(p, c) for c in poses for p in rings[a]]
            depth = min((p[2] if p else 0) for p in depths)
            lod = sum(depth >= d for d in cfg['lod_depths'])
            stride = cfg['lod_strides'][lod]
            b = min(a + stride, end)
            next_cut=min((cut for cut in cuts if a < cut <= b),default=None)
            if next_cut is not None:
                b=min(b,next_cut-1)
                if b==a:
                    b=next_cut
                    sequence.append(b);a=b;continue
            if b in cuts:
                sequence.append(b);a=b;continue
            while b > a + 1 and not span_ok(rings, a, b, poses, cfg['max_error_px'],fine_rings):
                b = a + max(1, (b - a) // 2)
            levels[(b - a).bit_length() - 1] += 1
            sequence.append(b)
            a = b
        if len(sequence) > cfg['max_schedule_rings']:
            raise ValueError(f'band {band}: {len(sequence)} rings exceeds schedule budget')
        if sequence != sorted(set(sequence)) or sequence[-1] >= len(rings):
            raise ValueError('invalid schedule coverage or indices')
        first_world=band+(cfg.get('near_distance',2200)+step-1)//step
        sequence=[ring for ring in sequence if ring>=first_world]
        if len(sequence)<2:
            raise ValueError(f'band {band}: no distant geometry remains after the near region')
        relative = [r - max(0, band - 1) for r in sequence]
        if max(relative) > 255:
            raise ValueError('schedule offsets overflow uint8')
        schedules.append((len(indices),len(sequence),relative[0]))
        indices.extend(relative)
    return schedules, indices, levels


def compile_world(cfg, baked, sampler):
    if cfg['version'] != 1 or cfg['step'] != 256:
        raise ValueError('unsupported world version or sample step')
    if len(cfg['lod_strides']) != 4 or any(v < 1 or v > 64 or v & (v-1) for v in cfg['lod_strides']) or cfg['lod_depths'] != sorted(cfg['lod_depths']):
        raise ValueError('expected ordered four-level LOD hierarchy')
    if cfg['lookahead'] % 256 or cfg['finish_allowance'] % 256 or cfg['max_error_px'] <= 0:
        raise ValueError('invalid coverage or error tolerance')
    profile=cfg.get('cross_section')
    if profile is not None and (len(profile)!=6 or any(len(p)!=3 or p[0] not in (-1,1) or any(not isinstance(v,int) for v in p) for p in profile)):
        raise ValueError('cross_section requires six integer [side, outside, height] points')
    groups, aliases = [], []
    for track in cfg['tracks']:
        authored = next(t for t in baked['tracks'] if t['id'] == track)
        if authored['span']['repeat'] != 'ends':
            raise ValueError(f'{track}: scene meshes require a finite, non-looping track')
        masks=route_masks(authored)
        if cfg.get('requires_deck') and any(not seg['deck'] for seg in authored['segments']):
            raise ValueError(f'{track}: road mesh requires continuous deck; gaps need separate mesh sections')
        length = authored['span']['s1']
        bands = (length + cfg['finish_allowance']) // 256 + 1
        limit = (bands + cfg['lookahead'] // 256) * 256
        for route_mask in masks:
            command = ([
                str(sampler), track, '256', str(limit), str(cfg['outside']),
                str(cfg['wall_height']), str(cfg['ceiling_height'])] +
                ([','.join(str(v) for point in cfg['cross_section'] for v in point)] if 'cross_section' in cfg else []))
            if cfg.get('height_mode'):
                if 'cross_section' not in cfg:
                    raise ValueError('height_mode requires a custom cross_section')
                command.append(cfg['height_mode'])
            env=os.environ.copy()
            env['RUNNER_BAKE_ROUTE']=str(route_mask)
            if cfg.get('fork_boundaries'):
                env['RUNNER_BAKE_FORK_BOUNDARIES']='1'
            sampled = json.loads(subprocess.check_output(command,env=env))
            if sampled['length'] != length:
                raise ValueError(f'{track}: track length mismatch')
            rings = sampled['rings']
            fine_rings = sampled.get('fine_rings')
            fork_walls = compile_forks(sampled.get('fork_sections', []))
            if (len(rings) > 65535 or
                any(abs(v) > 0x3fffffff for r in rings for p in r for v in p) or
                any(abs(v) > 0x3fffffff for w in fork_walls for p in w[2] for v in p)):
                raise ValueError('world coordinates or ring IDs overflow')
            cuts=[i for i,value in enumerate(sampled['cuts']) if value]
            # Share identical geometry across stages with different obstacles/colors.
            group = next((i for i, g in enumerate(groups)
                          if g['rings'] == rings and g['cuts'] == cuts and
                          g['fork_walls'] == fork_walls and g['bands'] == bands), None)
            if group is None:
                schedules, indices, levels = schedules_for(rings, sampled['cameras'], bands, cfg,
                                                           sampled['camera_bounds'],cuts,fine_rings)
                group = len(groups)
                axis_aligned = all(
                    r[1] == [r[0][0], r[0][1] + cfg['wall_height'], r[0][2]] and
                    r[2] == [r[0][0], r[0][1] + cfg['ceiling_height'], r[0][2]] and
                    r[5][1:] == r[0][1:] and
                    r[3] == [r[5][0], r[5][1] + cfg['ceiling_height'], r[5][2]] and
                    r[4] == [r[5][0], r[5][1] + cfg['wall_height'], r[5][2]] for r in rings)
                groups.append(dict(rings=rings,cuts=cuts,schedules=schedules,indices=indices,bands=bands,
                                   fork_walls=fork_walls,levels=levels, axis_aligned=axis_aligned))
            aliases.append((track, route_mask, group))
    return groups, aliases


def pack_indices(schedules, indices, stride_bits=6):
    """Pack schedule gaps using the bit width required by the LOD strides."""
    packed=bytearray()
    encoded=[]
    bit=0
    for first,count,initial in schedules:
        encoded.append((bit,count,initial))
        start=indices[first] if count else 0
        if start != initial:
            raise ValueError('schedule initial offset changed during packing')
        for i in range(1,count):
            stride=indices[first+i]-indices[first+i-1]
            if stride < 1 or stride > (1<<stride_bits):
                raise ValueError(f'schedule stride {stride} does not fit {stride_bits} bits')
            while len(packed) < (bit+stride_bits+7)//8:
                packed.append(0)
            code=stride-1
            for n in range(stride_bits):
                if code & (1<<n):
                    packed[(bit+n)//8] |= 1 << ((bit+n)%8)
            bit += stride_bits
    if bit:
        packed.append(0)  # Allows the runtime's two-byte unaligned read at EOF.
    return encoded, packed


def route_masks(authored):
    """Enumerate the bounded alternate-chain selections for one finite track."""
    branches = {s['chain'] for s in authored['segments'] if s['chain'] != 'main'}
    if len(branches) > 3:
        raise ValueError(f'{authored["id"]}: {len(branches)} forks exceed the eight-route package limit')
    return range(1 << len(branches))


def emit(groups, aliases, cfg):
    lines = ['/* Generated by tools/bake_worlds.py. Include world_mesh.h first. */']
    resident = 0
    fork_groups = []
    fork_ids = []
    # Both routes share most of their exact world vertices. Keep the route
    # schedules independent, but store identical rings (including cut flags)
    # once. Use indirection only when the pool actually saves resident RAM.
    pool, lookup, ring_maps = [], {}, []
    for group in groups:
        mapping=[]
        cuts=set(group.get('cuts',()))
        for index,ring in enumerate(group['rings']):
            key=(tuple(tuple(p) for p in ring),int(index in cuts))
            if key not in lookup:
                lookup[key]=len(pool)
                pool.append(key)
            mapping.append(lookup[key])
        ring_maps.append(mapping)
    total_rings=sum(len(g['rings']) for g in groups)
    shared=len(pool)<65536 and len(pool)*44+total_rings*2<total_rings*44
    def emit_rings(name, records):
        lines.append(f'static const WorldRing {name}[] = {{')
        for ring,cut in records:
            offsets=[[p[j]-ring[0][j] for j in range(3)] for p in ring[1:]]
            if any(v < -32768 or v > 32767 for p in offsets for v in p):
                raise ValueError('ring-local coordinates overflow int16')
            lines.append('    {{'+','.join(map(str,ring[0]))+'},{'+
                         ','.join('{'+','.join(map(str,p))+'}' for p in offsets)+'},'+str(cut)+'},')
        lines.append('};')
    if shared:
        emit_rings('world_rings_shared',pool)
        resident+=len(pool)*44+total_rings*2
    for group in groups:
        walls = group['fork_walls']
        if walls and walls not in fork_groups:
            fork_groups.append(walls)
        fork_ids.append(fork_groups.index(walls) if walls else None)
    stride_bits=max(1,(max(cfg['lod_strides'])-1).bit_length())
    for i, group in enumerate(groups):
        rings, schedules, indices, fork_walls = (group[k] for k in ('rings', 'schedules', 'indices', 'fork_walls'))
        encoded,packed=pack_indices(schedules,indices,stride_bits)
        resident += (0 if shared else len(rings)*44) + len(encoded) * 8 + len(packed)
        cuts=set(group.get('cuts',()))
        if shared:
            lines.append(f'static const uint16_t world_ring_ids_{i}[] = {{')
            lines.extend('    '+','.join(map(str,ring_maps[i][j:j+24]))+',' for j in range(0,len(rings),24))
            lines.append('};')
        else:
            emit_rings(f'world_rings_{i}',[(r,int(j in cuts)) for j,r in enumerate(rings)])
        lines += [f'static const WorldSchedule world_schedules_{i}[] = {{']
        lines.extend(f'    {{{start},{count},{initial}}},' for start,count,initial in encoded)
        lines += ['};', f'static const uint8_t world_indices_{i}[] = {{']
        lines.extend('    ' + ','.join(map(str, packed[j:j + 24])) + ',' for j in range(0, len(packed), 24))
        lines.append('};')
    for i, walls in enumerate(fork_groups):
        lines.append(f'static const WorldForkWall world_fork_walls_{i}[] = {{')
        for start,end,points,flags in walls:
            offsets=[[p[k]-points[0][k] for k in range(3)] for p in points[1:]]
            if any(abs(v)>32767 for p in offsets for v in p):
                raise ValueError('fork-local coordinates overflow int16')
            origin=','.join(map(str,points[0]))
            packed_points=','.join('{'+','.join(map(str,p))+'}' for p in offsets)
            lines.append(f'    {{{start},{end},{{{origin}}},{{{packed_points}}},{flags}}},')
        lines.append('};')
    resident += len(aliases) * 40  # 32-bit console layout; excludes strings/alignment.
    resident += sum(len(walls)*64 for walls in fork_groups)
    if 'cross_section' in cfg: resident += 36
    if resident > cfg['max_resident_bytes']:
        raise ValueError(f'{resident} resident bytes exceeds {cfg["max_resident_bytes"]}')
    lines.append('static const WorldMesh world_meshes[] = {')
    for track, route_mask, i in aliases:
        aligned = int(groups[i].get('axis_aligned', False))
        fork_walls=f'world_fork_walls_{fork_ids[i]}' if groups[i]['fork_walls'] else '0'
        rings='world_rings_shared' if shared else f'world_rings_{i}'
        ring_ids=f'world_ring_ids_{i}' if shared else '0'
        lines.append(f'    {{"{track}",{rings},world_schedules_{i},world_indices_{i},{fork_walls},{len(groups[i]["fork_walls"])},{groups[i]["bands"]},1,{aligned},{cfg.get("near_distance",2200)},{route_mask},{stride_bits},{cfg.get("lookahead",44000)},{ring_ids}}},')
    lines += ['};', '#define WORLD_MESH_COUNT (sizeof(world_meshes) / sizeof(world_meshes[0]))', '']
    header='\n'.join(lines)
    prefix=cfg.get('symbol_prefix','world')
    if prefix != 'world':
        for stem in ('rings','ring_ids','schedules','indices','meshes','fork_walls'):
            header=header.replace('world_'+stem,prefix+'_'+stem)
        header=header.replace('WORLD_MESH_COUNT',prefix.upper()+'_MESH_COUNT')
    if 'cross_section' in cfg:
        header += '\nstatic const int16_t '+prefix+'_profile[6][3] = {'+','.join('{'+','.join(map(str,p))+'}' for p in cfg['cross_section'])+'};\n'
    return header, resident


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--header', type=Path, required=True)
    ap.add_argument('--report', type=Path, required=True)
    ap.add_argument('--manifest', type=Path, default=ROOT / 'worlds/sine.json')
    ap.add_argument('--tracks', type=Path, default=ROOT / 'tools/trackview/baked.json')
    ap.add_argument('--cc', default=os.environ.get('HOST_CC', 'cc'))
    args = ap.parse_args()
    cfg = json.loads(args.manifest.read_text())
    sources = [args.manifest, args.tracks, Path(__file__), ROOT / 'tools/sample_world.c',
               ROOT / 'tools/bake_fork_walls.py',
               ROOT / 'src/path.c', ROOT / 'src/path.h', ROOT / 'src/path_runtime.h',
               ROOT / 'src/course.h', ROOT / 'src/world.h', ROOT / 'src/world_mesh.h']
    source_hash=hashlib.sha256(b''.join(p.read_bytes() for p in sources)).hexdigest()
    if args.header.exists() and args.report.exists():
        previous=json.loads(args.report.read_text())
        if (previous.get('source_sha256')==source_hash and
            previous.get('header_sha256')==hashlib.sha256(args.header.read_bytes()).hexdigest()):
            print(f'baked worlds: current ({previous["resident_bytes"]} resident bytes)')
            return
    with tempfile.TemporaryDirectory(prefix='runner-world-') as temp:
        sampler = Path(temp) / 'sample-world'
        subprocess.run([args.cc, '-std=gnu99', '-O2', '-DRUNNER_PREVIEW', '-I', str(ROOT / 'src'),
                        str(ROOT / 'tools/sample_world.c'), '-o', str(sampler)], check=True)
        groups, aliases = compile_world(cfg, json.loads(args.tracks.read_text()), sampler)
    header, resident = emit(groups, aliases, cfg)
    report = dict(version=1, tracks=sorted({t for t, _, _ in aliases}),
                  routes=[dict(track=t,mask=route) for t,route,_ in aliases],resident_bytes=resident,
                  geometry_groups=len(groups), source_sha256=source_hash,header_sha256=hashlib.sha256(header.encode()).hexdigest(),
                  max_error_px=cfg['max_error_px'], camera_samples_per_band=5,
                  lookahead=cfg['lookahead'], groups=[dict(rings=len(g['rings']), schedules=len(g['schedules']),
                  max_schedule_rings=max(n for _, n, _ in g['schedules']), lod_span_counts=g['levels'],
                  axis_aligned=g['axis_aligned']) for g in groups])
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text(header)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(f'baked worlds: {resident} resident bytes, {len(groups)} shared geometry groups')


if __name__ == '__main__':
    main()
