#!/usr/bin/env python3
"""Checks that filtered facades retain light and never punch transparent holes."""
import unittest
from bake_vesper import bake, PALETTE

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
    def test_mip_tiles_do_not_overlap(self):
        _, tiles, _=bake()
        occupied=set()
        for t in tiles:
            pixels={(t['u']+x,t['v']+y) for y in range(t['size']) for x in range(t['size'])}
            self.assertFalse(occupied & pixels)
            self.assertTrue(all(0<=x<256 and 128<=y<192 for x,y in pixels))
            occupied |= pixels

if __name__=='__main__': unittest.main()
