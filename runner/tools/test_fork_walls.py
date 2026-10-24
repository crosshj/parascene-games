"""Fork boundary regression: no wall may intersect either drivable corridor."""
import copy
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from bake_fork_walls import compile_forks

ROOT = Path(__file__).resolve().parents[1]


def enters_polygon(a, b, poly, margin=0):
    """Clip the segment against a convex road footprint, independent of bake."""
    area = sum(p[0]*q[2]-p[2]*q[0] for p,q in zip(poly,poly[1:]+poly[:1]))
    sign = 1 if area > 0 else -1
    lo, hi = 0., 1.
    for p,q in zip(poly,poly[1:]+poly[:1]):
        def side(v):
            return sign*((q[0]-p[0])*(v[2]-p[2])-(q[2]-p[2])*(v[0]-p[0])) - margin*((q[0]-p[0])**2+(q[2]-p[2])**2)**0.5
        fa, fb = side(a), side(b)
        if max(fa,fb) <= 1e-6:
            return False
        if fa < 0:
            lo = max(lo, -fa/(fb-fa))
        elif fb < 0:
            hi = min(hi, fa/(fa-fb))
    return hi-lo > 1e-6


class ForkBoundaries(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='fork-boundary-')
        cls.sampler = Path(cls.tmp.name)/'sample'
        subprocess.run(['cc','-std=gnu99','-O2','-DRUNNER_PREVIEW','-I',str(ROOT/'src'),
                        str(ROOT/'tools/sample_world.c'),'-o',str(cls.sampler)],check=True)
        cls.tracks = json.loads((ROOT/'tools/trackview/baked.json').read_text())['tracks']

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def sample(self, track, route=0):
        env = dict(os.environ, RUNNER_BAKE_FORK_BOUNDARIES='1', RUNNER_BAKE_ROUTE=str(route))
        profile='-1,108,0,-1,108,960,-1,108,960,1,108,960,1,108,960,1,108,0'
        return json.loads(subprocess.check_output([str(self.sampler),track,'256','0',
                         '480','960','0',profile,'crack'],env=env))['fork_sections']

    def check_track(self, track):
        sections = self.sample(track)
        walls = compile_forks(sections)
        # One physical mesh, identical regardless of the runner's chosen route.
        self.assertEqual(walls, compile_forks(self.sample(track,1)))
        authored=next(t for t in self.tracks if t['id']==track)
        roads=[]
        for seg in authored['segments']:
            if not any(s['start']-256 <= seg['s'] < s['end'] for s in sections):
                continue
            poly=[]
            for end,edge in [(False,0),(False,1),(True,1),(True,0)]:
                pos=seg['p1' if end else 'p']; right=seg['right1' if end else 'right']
                offset=seg['edge1' if end else 'edge'][edge]
                poly.append([pos[k]+right[k]*offset for k in range(3)])
            roads.append(poly)
        for index,(s0,s1,p,flags) in enumerate(walls):
            self.assertGreater(s1,s0)
            for side in (0,2):
                for road in roads:
                    self.assertFalse(enters_polygon(p[side],p[side+1],road),
                                     f'{track} wall {index} crosses a road: {s0}..{s1}')
                if not flags&2:
                    self.assertGreater(p[side+4][1]-p[side][1],210)
                    self.assertGreater(p[side+5][1]-p[side+1][1],210)
            if index and walls[index-1][3]==flags:
                previous=walls[index-1]
                self.assertEqual(previous[1],s0)
                for start,end in [(0,1),(2,3),(4,5),(6,7)]:
                    self.assertEqual(previous[2][end],p[start],f'{track} open seam {s0}')
        for branch,section in enumerate(sections):
            outer=[w for w in walls if w[3]==4*branch]
            inner=[w for w in walls if w[3]==4*branch+1]
            self.assertEqual(outer[0][0],section['start']-256)
            self.assertEqual(outer[-1][1],section['end'])
            self.assertGreater(inner[0][0],section['start'])
            self.assertLess(inner[-1][1],section['end'])
            for p,ids in [(inner[0][2],(0,2,4,6)),(inner[-1][2],(1,3,5,7))]:
                self.assertEqual(p[ids[0]],p[ids[1]])
                self.assertEqual(p[ids[2]],p[ids[3]])
        # Mirror the track. Edge selection must come from geometry, not a
        # hard-coded assumption that the alternate branch turns left.
        mirrored=copy.deepcopy(sections)
        for section in mirrored:
            for key in ('entry','exit'):
                p=section[key]
                section[key]=[[-v[0],v[1],v[2]] for v in p[2:]+p[:2]]
            for key in ('main','alt'):
                for sample in section[key]:
                    p=sample[1]
                    sample[1]=[[-v[0],v[1],v[2]] for v in p[2:]+p[:2]]
        expected=copy.deepcopy(walls)
        for w in expected:
            for p in w[2]: p[0]=-p[0]
        self.assertEqual(compile_forks(mirrored),expected)

    def test_meridian_fork(self):
        self.check_track('meridian.fork')

    def test_meridian_verge(self):
        self.check_track('meridian.verge')


if __name__=='__main__':
    unittest.main()
