#!/usr/bin/env python3
"""Reject a claimed performance win unless matched console runs demonstrate it."""
import argparse
import json
from pathlib import Path
import re


def metrics(path):
    data=json.loads(path.read_text())
    report=data['report']
    if 'GPU drawing' in report:
        raise ValueError(f'{path}: obsolete profiler serialized CPU/GPU work; rerun with retail overlap')
    average=re.search(r'level total\s+(\d+) cycles',report)
    worst=re.search(r'worst level frame (\d+) cycles',report)
    cadence=re.search(r'presentation: (\d+) intervals, (\d+) missed 30Hz deadlines, worst (\d+) vblanks, total (\d+) vblanks',report)
    if not average or not worst or not cadence:
        raise ValueError(f'{path}: missing CPU or presentation measurements')
    intervals,missed,peak,total=map(int,cadence.groups())
    if intervals<data['frames']-2 or not data.get('disc_sha256') or not data.get('run'):
        raise ValueError(f'{path}: incomplete or unidentified run')
    return data,dict(average_cycles=int(average[1]),worst_cycles=int(worst[1]),
        intervals=intervals,missed_deadlines=missed,worst_vblanks=peak,
        average_fps=60*intervals/total)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--reference',type=Path)
    ap.add_argument('--candidate',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args()
    candidate,b=metrics(args.candidate)
    ref=a=None
    if args.reference:
        ref,a=metrics(args.reference)
        for key in ('track','level','stage','frames','scroll'):
            if ref[key]!=candidate[key]: raise ValueError(f'unmatched {key}')
        if not ref['reference']: raise ValueError('reference run used compiled mode')
    if candidate['reference']: raise ValueError('candidate run used reference mode')
    failures=[]
    if b['missed_deadlines']: failures.append('missed 30 fps presentation deadlines')
    if a and b['average_cycles']>a['average_cycles']*0.95: failures.append('less than 5% CPU improvement')
    if b['worst_cycles']>1128960*0.85: failures.append('less than 15% worst-frame CPU headroom')
    result=dict(track=candidate['track'],passed=not failures,failures=failures,reference=a,candidate=b,
        cpu_reduction_percent=100*(1-b['average_cycles']/a['average_cycles']) if a else None,
        reference_run=ref['run'] if ref else None,candidate_run=candidate['run'],
        candidate_disc_sha256=candidate['disc_sha256'])
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
    raise SystemExit(bool(failures))

if __name__=='__main__':main()
