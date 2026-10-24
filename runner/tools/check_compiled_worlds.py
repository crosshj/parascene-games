#!/usr/bin/env python3
"""Check baked world vertices and LODs through the real runtime camera."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--include',type=Path,required=True)
    args=ap.parse_args()
    packages=[('reach.json','reach_world_data.h'),('reach-road.json','road_world_data.h')]
    with tempfile.TemporaryDirectory(prefix='world-mesh-check-') as tmp:
        for manifest,header in packages:
            cfg=json.loads((ROOT/'worlds'/manifest).read_text())
            prefix=cfg.get('symbol_prefix','world')
            flags=[] if prefix=='world' else [f'-Dworld_meshes={prefix}_meshes',f'-DWORLD_MESH_COUNT={prefix.upper()}_MESH_COUNT']
            exe=Path(tmp)/manifest
            subprocess.run(['cc','-std=gnu99','-O2','-DRUNNER_PREVIEW',f'-DWORLD_MESH_HEADER="{header}"',*flags,
                '-I',str(ROOT/'src'),'-I',str(args.include),str(ROOT/'tools/check_world_mesh.c'),
                str(ROOT/'src/path.c'),'-lm','-o',str(exe)],check=True)
            profile=','.join(str(v) for p in cfg['cross_section'] for v in p)
            subprocess.run([str(exe),str(cfg['lookahead']),str(max(cfg['lod_strides'])),profile],check=True)

if __name__=='__main__':main()
