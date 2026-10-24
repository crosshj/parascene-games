#!/usr/bin/env python3
"""Bake Daniel Linssen's m5x7 TTF at its native 16px size into a PS1 atlas.

FFmpeg/FreeType renders the original font without filtering. TrueType advance
metrics preserve its spacing, and a shared baseline retains the descenders.
"""
from pathlib import Path
import argparse
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'web/brand/m5x7.ttf'


def advances(font):
    data = font.read_bytes()
    u16 = lambda at: struct.unpack_from('>H', data, at)[0]
    tables = {}
    for i in range(u16(4)):
        tag, _, offset, _ = struct.unpack_from('>4sIII', data, 12 + i * 16)
        tables[tag.decode()] = offset
    em = u16(tables['head'] + 18)
    cmap = tables['cmap']
    for i in range(u16(cmap + 2)):
        platform, encoding, offset = struct.unpack_from('>HHI', data, cmap + 4 + i * 8)
        if platform == 3 and encoding == 1:
            cmap += offset
            break
    assert u16(cmap) == 4, 'm5x7 must contain a Unicode format-4 cmap'
    count = u16(cmap + 6) // 2
    ends = cmap + 14
    starts = ends + count * 2 + 2
    deltas = starts + count * 2
    ranges = deltas + count * 2
    result = []
    for code in range(32, 127):
        for i in range(count):
            first, last = u16(starts + i * 2), u16(ends + i * 2)
            if not first <= code <= last:
                continue
            delta = struct.unpack_from('>h', data, deltas + i * 2)[0]
            distance = u16(ranges + i * 2)
            if distance:
                glyph = u16(ranges + i * 2 + distance + (code - first) * 2)
                glyph = (glyph + delta) & 65535 if glyph else 0
            else:
                glyph = (code + delta) & 65535
            advance = u16(tables['hmtx'] + glyph * 4) * 16
            assert glyph and advance % em == 0, f'Non-pixel metric for {chr(code)!r}'
            result.append(advance // em)
            break
        else:
            raise ValueError(f'm5x7 is missing {chr(code)!r}')
    assert len(result) == 95 and all(1 < width <= 8 for width in result)
    return result


def bake(path, font=SOURCE, ffmpeg='ffmpeg'):
    spacing = advances(font)
    with tempfile.TemporaryDirectory(prefix='runner-m5x7-') as folder:
        temp = Path(folder)
        shutil.copyfile(font, temp / 'm5x7.ttf')
        filters = []
        for i, code in enumerate(range(32, 127)):
            (temp / f'g{i}.txt').write_text(chr(code))
            filters.append('drawtext=fontfile=m5x7.ttf:textfile=g%d.txt:expansion=none:'
                           'fontsize=16:fontcolor=white:ft_load_flags=monochrome+no_hinting:'
                           'y_align=baseline:x=%d:y=%d' % (i, (i % 16) * 8, (i // 16) * 16 + 7))
        bitmap = subprocess.check_output([
            ffmpeg, '-v', 'error', '-f', 'lavfi', '-i', 'color=c=black:s=128x96:r=1,format=gray',
            '-vf', ','.join(filters), '-frames:v', '1', '-f', 'rawvideo', '-pix_fmt', 'gray', 'pipe:1'],
            cwd=temp)
    assert len(bitmap) == 128 * 96
    pixels = [0] * (32 * 96)
    for y in range(96):
        for x in range(128):
            if bitmap[y * 128 + x] >= 128:
                pixels[y * 32 + x // 4] |= 1 << ((x & 3) * 4)
    # The unused final atlas cell holds the small music-note icon.
    note = ('..#####.', '..#...#.', '..#...#.', '..#...#.', '..#...#.',
            '..#..##.', '.##.###.', '###.##..', '##......')
    for y, row in enumerate(note):
        for x, value in enumerate(row):
            if value == '#':
                px, py = 120 + x, 80 + y
                pixels[py * 32 + px // 4] |= 1 << ((px & 3) * 4)
    def values(items, count=16):
        return '\n'.join('    ' + ', '.join(str(v) for v in items[i:i + count]) + ','
                         for i in range(0, len(items), count))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('/* m5x7 by Daniel Linssen (CC0), baked at 16px. See runner/README.txt. */\n'
                    '#pragma once\n#include <stdint.h>\n'
                    '#define CREDIT_FONT_ATLAS_H 96\n#define CREDIT_FONT_CELL_H 16\n'
                    '#define CREDIT_FONT_GLYPH_H 9\n'
                    'static const uint8_t credit_font_width[95] = {\n' +
                    values([width - 1 for width in spacing]) + '\n};\n' +
                    'const uint16_t credit_font_pixels[3072] __attribute__((aligned(4))) = {\n' +
                    values(pixels) + '\n};\n' +
                    'const uint16_t credit_font_palette[16] __attribute__((aligned(4))) = {0, 32767};\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('header', type=Path)
    parser.add_argument('--font', type=Path, default=SOURCE)
    parser.add_argument('--ffmpeg', default='ffmpeg')
    args = parser.parse_args()
    bake(args.header, args.font, args.ffmpeg)
