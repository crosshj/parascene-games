#!/usr/bin/env python3
"""Build the DISC, run one identified stage, save its report, close the emulator.

Fails on stale output, a mismatched stage, an already running DuckStation,
or timeout. Only the subprocess created here is ever terminated.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--level',type=int,default=3)
    ap.add_argument('--stage',type=int,default=0)
    ap.add_argument('--track',required=True)
    ap.add_argument('--frames',type=int,default=300)
    ap.add_argument('--scroll',type=int,default=0)
    ap.add_argument('--reference',action='store_true')
    ap.add_argument('--timeout',type=int,default=100)
    ap.add_argument('--emulator',default='/Applications/DuckStation.app/Contents/MacOS/DuckStation')
    ap.add_argument('--build-dir',type=Path,default=ROOT/'build/profile-stage')
    ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args()
    if args.stage<0 or args.frames<1: ap.error('select a stage and positive frame count')
    running=subprocess.check_output(['ps','-Ao','command'],text=True)
    if any(line.strip().startswith(args.emulator) for line in running.splitlines()):
        raise RuntimeError('DuckStation is already running; an exclusive, identifiable profile requires it to be closed')
    identity=hashlib.sha256((args.track+str(time.time_ns())).encode()).hexdigest()[:16]
    build=args.build_dir.resolve()
    subprocess.run(['cmake','-S',str(ROOT),'-B',str(build),'-DRUNNER_PROFILE=ON',
        '-DCMAKE_C_FLAGS=',f'-DRUNNER_PROFILE_LEVEL={args.level}',
        f'-DRUNNER_PROFILE_STAGE={args.stage}',f'-DRUNNER_PROFILE_FRAMES={args.frames}',
        f'-DRUNNER_PROFILE_SCROLL={args.scroll}',f'-DRUNNER_PROFILE_TAG={identity}',
        f'-DRUNNER_REFERENCE_RENDERER={"ON" if args.reference else "OFF"}'],check=True)
    subprocess.run(['cmake','--build',str(build),'--target','runner_iso','-j4'],check=True)
    disc=build/'runner.bin'
    disc_hash=hashlib.sha256(disc.read_bytes()).hexdigest()
    sources=hashlib.sha256()
    for path in sorted((ROOT/'src').glob('*')):
        if path.is_file():
            sources.update(path.name.encode()+b'\0'+path.read_bytes())
    package=json.loads((build/'path_runtime_report.json').read_text())
    args.output.parent.mkdir(parents=True,exist_ok=True)
    log=Path.home()/'Library/Application Support/DuckStation/duckstation.log'
    start=time.monotonic()
    with args.output.with_suffix('.emulator.log').open('w') as output:
        proc=subprocess.Popen([args.emulator,str(build/'runner.cue')],stdout=output,stderr=subprocess.STDOUT,start_new_session=True)
        try:
            while time.monotonic()-start<args.timeout:
                if proc.poll() is not None: raise RuntimeError('emulator exited before producing the requested report')
                text=log.read_text(errors='replace').replace('\0','') if log.exists() else ''
                marker=f'PROF run {identity} '
                if marker in text:
                    report=text[text.index(marker):]
                    first=report.splitlines()[0]
                    if f'track={args.track} frames={args.frames} scroll={args.scroll}' not in first:
                        raise RuntimeError('emulator loaded the wrong stage: '+first)
                    if 'PROF repeat ' in report:
                        report=report[:report.index('PROF repeat ')]
                        args.output.with_suffix('.txt').write_text(report)
                        args.output.write_text(json.dumps(dict(run=identity,track=args.track,
                            level=args.level,stage=args.stage,frames=args.frames,scroll=args.scroll,
                            reference=args.reference,disc_sha256=disc_hash,
                            renderer_sources_sha256=sources.hexdigest(),path_package=package,
                            world_packages={p.name:json.loads(p.read_text()) for p in build.glob("*_world_report.json")},
                            elapsed_seconds=round(time.monotonic()-start,2),
                            report=report),indent=2)+'\n')
                        print(report,flush=True)
                        break
                time.sleep(0.25)
            else: raise TimeoutError('requested stage did not finish its measurement before timeout')
        finally:
            if proc.poll() is None:
                proc.send_signal(signal.SIGINT)
                try: proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.terminate()
                    try: proc.wait(timeout=5)
                    except subprocess.TimeoutExpired: proc.kill();proc.wait()
            print('Profile emulator closed.',flush=True)


if __name__=='__main__': main()
