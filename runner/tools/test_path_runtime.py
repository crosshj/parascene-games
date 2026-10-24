#!/usr/bin/env python3
"""Compare compiled path behavior against the original runtime preparation."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from bake_path_runtime import ROOT, emit


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--include', type=Path, required=True)
    args=ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='path-check-') as tmp:
        observations=[]
        for name,flags in [('compiled',[]),('reference',['-DRUNNER_LEGACY_PATH'])]:
            exe=Path(tmp)/name
            subprocess.run(['cc','-std=gnu99','-O2','-DRUNNER_PREVIEW',*flags,
                '-I',str(ROOT/'src'),'-I',str(args.include),
                str(ROOT/'tools/check_path_runtime.c'),'-o',str(exe)],check=True)
            observations.append(subprocess.check_output([str(exe)]))
        if observations[0]!=observations[1]:
            raise AssertionError('compiled path changes route/geometry behavior:\n'+
                observations[0].decode()+'\nREFERENCE\n'+observations[1].decode())
        print(f'Compiled path equivalence: {len(observations[0].splitlines())} tracks, all route masks, reverse seeks and band edges agree exactly')
    for invalid in [dict(version=2,tracks=[]),dict(version=1,tracks=[])]:
        try: emit(invalid,320000)
        except ValueError: pass
        else: raise AssertionError('invalid package accepted')


if __name__=='__main__': main()
