"""Focused checks for the compiler's geometric error and budget rejection."""
import copy
import unittest
import struct
import tempfile
from pathlib import Path
from bake_worlds import emit, project, schedules_for, span_ok
from check_ram import check
from bake_tracks import analytic_straight
from preview.visibility_report import summarize


class WorldCompilerTests(unittest.TestCase):
    def setUp(self):
        self.camera = [0, 0, 0, 4096, 0, 0, 0, 4096, 0, 0, 0, 4096]

    def test_projection_scale_and_near_guard(self):
        self.assertEqual(project([100, 170, 1000], self.camera), (16, 0, 1000))
        self.assertIsNone(project([100, 170, 63], self.camera))

    def test_analytic_tunnel_requires_a_centered_flat_finite_track(self):
        segment = dict(s=0,p=[0,0,0],p1=[0,0,256],yaw=0,pitch=0,roll=0)
        self.assertTrue(analytic_straight([segment],0,[],256))
        self.assertFalse(analytic_straight([segment],1,[],256))
        self.assertFalse(analytic_straight([segment],0,[1],256))
        for field in ('yaw','pitch','roll'):
            changed = dict(segment,**{field:1})
            self.assertFalse(analytic_straight([changed],0,[],256))
        self.assertFalse(analytic_straight([dict(segment,p1=[1,0,256])],0,[],256))

    def test_visibility_distinguishes_occlusion_and_pixel_intervals(self):
        def row(instance,s,raster,visible,lod='lines'):
            return dict(stage='0',scroll=str(s),kind='rib',instance=str(instance),
                        representation=lod,primitives='3',raster_pixels=str(raster),visible_pixels=str(visible))
        report = summarize([row(0,-1,0,0,'catalog'),row(1,0,1,1),
                            row(1,128,1,1),row(1,256,5,0)])
        missing, seen = report['assets']
        self.assertTrue(missing['never_observed'])
        self.assertEqual(seen['observed'],2)
        self.assertEqual(seen['occluded'],1)
        self.assertEqual(seen['pixel_candidate_ranges'],[[0,128]])
        self.assertFalse(report['automatic_removal_safe'])

    def test_bend_is_rejected_but_straight_chord_is_kept(self):
        rings = [[[0, 170, z]] * 6 for z in [1000, 1256, 1512]]
        self.assertTrue(span_ok(rings, 0, 2, [self.camera], 1))
        bent = copy.deepcopy(rings)
        bent[1] = [[100, 170, 1256]] * 6
        self.assertFalse(span_ok(bent, 0, 2, [self.camera], 1))

    def test_schedule_preserves_coverage_and_rejects_excess_work(self):
        rings = [[[0, 170, 1000 + i * 256]] * 6 for i in range(10)]
        cfg = dict(step=256, lookahead=2048, lod_depths=[1600, 5000, 10000],
                   lod_strides=[1, 2, 4, 8], max_error_px=1, max_schedule_rings=10)
        schedules, indices, _ = schedules_for(rings, [self.camera] * 3, 1, cfg)
        self.assertEqual(indices[0], 0)
        self.assertEqual(indices[-1], 8)
        cfg['max_schedule_rings'] = 2
        with self.assertRaisesRegex(ValueError, 'schedule budget'):
            schedules_for(rings, [self.camera] * 3, 1, cfg)

    def test_resident_budget_and_vertex_overflow_fail_the_build(self):
        ring = [[0, 0, 0]] * 6
        group = dict(rings=[ring], schedules=[(0, 1)], indices=[0], bands=1)
        with self.assertRaisesRegex(ValueError, 'resident bytes'):
            emit([group], [('test', 0)], dict(max_resident_bytes=1))
        group['rings'] = [[[0, 0, 0]] + [[32768, 0, 0]] * 5]
        with self.assertRaisesRegex(ValueError, 'overflow'):
            emit([group], [('test', 0)], dict(max_resident_bytes=1000))

    def test_ram_guard_counts_bss_and_protects_reserved_memory(self):
        def elf(address, size):
            data = bytearray(84)
            data[:7] = b'\x7fELF\x01\x01\x01'
            struct.pack_into('<I', data, 28, 52)
            struct.pack_into('<HH', data, 42, 32, 1)
            # Zero bytes stored on disk; a large memory size still costs RAM.
            struct.pack_into('<8I', data, 52, 1, 0, address, address, 0, size, 6, 16)
            return data
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'budget.elf'
            path.write_bytes(elf(0x80010000, 0x1b0000))
            check(path)
            path.write_bytes(elf(0x80010000, 0x1b0001))
            with self.assertRaisesRegex(ValueError, 'exceeds retail RAM budget'):
                check(path)
            path.write_bytes(elf(0x80000000, 0x10000))
            with self.assertRaisesRegex(ValueError, 'reserved low RAM'):
                check(path)


if __name__ == '__main__':
    unittest.main()
