#!/usr/bin/env python3
"""Render real console HUD packets with the font embedded in the built ELF.

Unlike the scenery preview, this compiles world.c without RUNNER_PREVIEW, so
the actual sprite, tint, rectangle and allocation paths are exercised.
"""
import argparse
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]


def elf_symbol(path, name):
    data = path.read_bytes()
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    assert header[0][:6] == b"\x7fELF\x01\x01", "Expected a little-endian 32-bit ELF"
    sections = [struct.unpack_from("<10I", data, header[6] + i * header[11])
                for i in range(header[12])]
    for table in sections:
        if table[1] != 2:
            continue
        strings = sections[table[6]]
        names = data[strings[4]:strings[4] + strings[5]]
        for offset in range(table[4], table[4] + table[5], table[9]):
            symbol = struct.unpack_from("<IIIBBH", data, offset)
            if names[symbol[0]:].split(b"\0", 1)[0] == name.encode():
                section = sections[symbol[5]]
                start = section[4] + symbol[1] - section[3]
                return data[start:start + symbol[2]]
    raise ValueError(f"Missing ELF symbol {name}")


def font_bitmap(elf):
    pixels = elf_symbol(elf, "credit_font_pixels")
    palette = struct.unpack("<16H", elf_symbol(elf, "credit_font_palette"))
    assert len(pixels) == 32 * 96 * 2
    return pixels, 64, 96, palette


def render(commands, font):
    pixels = [(40, 56, 72)] * (320 * 240)
    texels, row_bytes, height, palette = font
    for kind, values in commands:
        x, y, a, b, r, g, blue = values[:7]
        if kind == "rect":
            for yy in range(max(0, y), min(240, y + b)):
                for xx in range(max(0, x), min(320, x + a)):
                    pixels[yy * 320 + xx] = (r, g, blue)
            continue
        width, glyph_height = values[7:] if len(values) == 9 else (8, 8)
        for dy in range(glyph_height):
            for dx in range(width):
                u, v = a + dx - 128, b + dy
                if not (0 <= x + dx < 320 and 0 <= y + dy < 240 and v < height):
                    continue
                index = (texels[v * row_bytes + u // 2] >> (4 * (u & 1))) & 15
                color = palette[index]
                if color == 0:
                    continue
                rgb = ((color & 31) * 8, ((color >> 5) & 31) * 8, ((color >> 10) & 31) * 8)
                pixels[(y + dy) * 320 + x + dx] = tuple(min(255, c * tint // 128) & ~7
                                                       for c, tint in zip(rgb, (r, g, blue)))
    return pixels


def glyph_pixels(image, y):
    return sum(max(image[yy * 320 + x]) >= 100
               for yy in range(y, y + 9) for x in range(33, 288))


def save_png(path, image, scale=1):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = []
    for y in range(240):
        row = b"\0" + b"".join(bytes(p) * scale for p in image[y * 320:(y + 1) * 320])
        rows.extend([row] * scale)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" +
                     chunk(b"IHDR", struct.pack(">IIBBBBB", 320 * scale, 240 * scale, 8, 2, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(b"".join(rows))) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build")
    parser.add_argument("--out", type=Path, default=ROOT / "build/music-credit-review")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    sdk = Path(os.environ.get("PSN00BSDK", Path.home() / "psn00bsdk"))
    executable = args.out / "check_music_credit"
    command = shlex.split(os.environ.get("CC", "cc")) + [
        "-std=gnu99", "-O2", "-ffunction-sections", "-fdata-sections",
        "-I", str(ROOT / "tools/preview/host"), "-idirafter", str(sdk / "include/libpsn00b"),
        "-I", str(ROOT / "src"), "-I", str(args.build),
        str(ROOT / "tools/check_music_credit.c"), str(ROOT / "src/music_credit.c"),
        "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections",
        "-o", str(executable)]
    subprocess.run(command, check=True)
    output = subprocess.check_output([str(executable)], text=True)
    font = font_bitmap(args.build / "runner.elf")
    track = age = None
    commands = []
    frames = checks = 0
    sample = None
    sample_width = 0
    for line in output.splitlines():
        parts = line.split()
        if parts[0] == "frame":
            track, age = map(int, parts[1:])
            commands = []
        elif parts[0] == "end":
            frames += 1
            image = render(commands, font)
            if age in (12, 60, 360, 378):
                assert glyph_pixels(image, 212) > 10, f"Missing artist: track {track}, age {age}"
                assert glyph_pixels(image, 222) > 10, f"Missing title: track {track}, age {age}"
                checks += 2
            if age in (-1, 396):
                assert not commands, f"Credit did not disappear at age {age}"
            if age == 60:
                card_width = max(p[1][2] for p in commands if p[0] == 'rect')
                assert card_width <= 170, f"Oversized credit: track {track}, {card_width}px"
                glyphs = [p[1] for p in commands if p[0] == 'glyph']
                icon = next(p for p in glyphs if p[2:4] == (248, 80))
                accent = next(p[1] for p in commands if p[0] == 'rect' and p[1][2:4] == (2, 16))
                assert accent[0] + accent[2] < icon[0]
                assert icon[0] + icon[7] < min(p[0] for p in glyphs if p is not icon)
                assert sum(max(image[y * 320 + x]) >= 100 for y in range(216, 225)
                           for x in range(icon[0], icon[0] + 8)) > 10, 'Music icon was hidden'
                assert any(p[3] >= 64 for p in glyphs if p is not icon), 'Lowercase glyphs were lost'
                save_png(args.out / f"track{track:02d}.png", image)
                if card_width > sample_width:
                    sample, sample_width = image, card_width
                    save_png(args.out / "fixed.png", image)
                # Negative control reproduces the previous background-last
                # GPU order: both credit lines must become invisible.
                covered = render([p for p in commands if p[0] != "rect"] +
                                 [p for p in commands if p[0] == "rect"], font)
                assert glyph_pixels(covered, 212) == glyph_pixels(covered, 222) == 0
                save_png(args.out / "broken-order.png", covered)
        else:
            commands.append((parts[0], tuple(map(int, parts[1:]))))
    assert sample is not None
    save_png(args.out / "fixed-3x.png", sample, 3)
    print(f"Music credit: {frames} native packet frames, {checks} artist/title visibility checks passed; "
          "background-occlusion control reproduced the missing text.")
    print(f"Screenshots: {args.out}")


if __name__ == "__main__":
    main()
