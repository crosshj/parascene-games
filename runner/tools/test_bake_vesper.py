#!/usr/bin/env python3
"""Checks that filtered facades retain light and never punch transparent holes."""
import unittest
from bake_vesper import bake, PALETTE, straight_runs
import copy

class Facades(unittest.TestCase):
    def test_opaque_budget_and_filtered_energy(self):
        packed, tiles, means = bake()
        self.assertEqual(len(packed)+32, 8224)
        self.assertTrue(all(any(c for c in color) for color in PALETTE))
        def pixel(x,y):
            b=packed[y*128+x//2]
            return PALETTE[(b >> (4*(x%2))) & 15]
        for t in tiles:
            pixels=[pixel(t['u']+x,y) for y in range(t['size']) for x in range(t['size'])]
            for c in range(3):
                avg=sum(p[c] for p in pixels)/len(pixels)
                self.assertLess(abs(avg-means[t['variant']][c]), 9,
                                f"variant {t['variant']} LOD {t['lod']} loses light")
    def test_straight_runs_stop_at_changes(self):
        base=dict(yaw=0,pitch=0,roll=0,edge=[-280,280],edge1=[-280,280],
                  right=[1,0,0],right1=[1,0,0],up=[0,1,0],fwd=[0,0,1],chain='main')
        segments=[dict(base,s=i*256,p=[0,0,i*256],p1=[0,0,(i+1)*256],next=i+1) for i in range(4)]
        self.assertEqual(straight_runs(segments),[1024]*4)
        for field,value in [('yaw',16),('roll',16),('edge',[-200,200]),('chain','fork'),('p',[20,0,512])]:
            changed=copy.deepcopy(segments); changed[2][field]=value
            self.assertLessEqual(straight_runs(changed)[0],512)

    def test_mip_tiles_do_not_overlap(self):
        _, tiles, _=bake()
        occupied=set()
        for t in tiles:
            pixels={(t['u']+x,t['v']+y) for y in range(t['size']) for x in range(t['size'])}
            self.assertFalse(occupied & pixels)
            self.assertTrue(all(0<=x<256 and 128<=y<192 for x,y in pixels))
            occupied |= pixels

if __name__=='__main__': unittest.main()
