#!/usr/bin/env python3
"""Write the runner pose guide the image editor starts from.

sprites/runner.png is the sheet and sprites/runner.txt is its map.
A painted character is a separate file named name.runner.png, for
example blazer.runner.png. Run this again to rebuild the guide. It
leaves those character files in place.
"""

import struct
import zlib
from pathlib import Path

SHEET = 1024
CELL_W = 128
CELL_H = 160
LOGIC_W = 32
LOGIC_H = 40
SCALE = 4
COLS = SHEET // CELL_W

KEY = (255, 0, 255)
OUTLINE = (24, 16, 32)
SKIN = (232, 188, 152)
SHIRT = (64, 214, 196)
PANTS = (36, 44, 96)
SHOES = (240, 196, 48)
HAIR = (88, 48, 168)


def chunk(tag, data):
    crc = zlib.crc32(tag + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)


def write_png(path, width, height, rgb):
    raw = bytearray()
    stride = width * 3
    for y in range(height):
        raw.append(0)
        raw.extend(rgb[y * stride:(y + 1) * stride])
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", ihdr)
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


def empty():
    return [None] * (LOGIC_W * LOGIC_H)


def blob(grid, x, y, w, h, color):
    for yy in range(y - 1, y + h + 1):
        for xx in range(x - 1, x + w + 1):
            if 0 <= xx < LOGIC_W and 0 <= yy < LOGIC_H:
                grid[yy * LOGIC_W + xx] = OUTLINE
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            if 0 <= xx < LOGIC_W and 0 <= yy < LOGIC_H:
                grid[yy * LOGIC_W + xx] = color


def figure(bob=0, shift=0, head_y=2, torso_y=13, torso_h=10,
           al=(5, 14, 4, 10), ar=(23, 14, 4, 10),
           ll=(10, 22, 5, 14), rl=(17, 22, 5, 14)):
    grid = empty()

    def add(x, y, w, h, color):
        blob(grid, x + shift, y + bob, w, h, color)

    add(12, head_y, 8, 3, HAIR)
    add(10, head_y + 2, 12, 7, HAIR)
    add(12, head_y + 6, 8, 4, SKIN)
    add(10, torso_y, 12, torso_h, SHIRT)
    add(*al, SHIRT)
    add(*ar, SHIRT)
    add(*ll, PANTS)
    add(ll[0] - 1, ll[1] + ll[3] - 1, ll[2] + 2, 4, SHOES)
    add(*rl, PANTS)
    add(rl[0] - 1, rl[1] + rl[3] - 1, rl[2] + 2, 4, SHOES)
    return grid


def mirror(grid):
    out = empty()
    for y in range(LOGIC_H):
        for x in range(LOGIC_W):
            out[y * LOGIC_W + (LOGIC_W - 1 - x)] = grid[y * LOGIC_W + x]
    return out


def poses():
    """24 frames: run, jump, lean left, lean right. Feet sit on the
    bottom row, except the tuck, whose feet pull up."""
    run = [
        figure(al=(3, 11, 4, 8), ar=(23, 16, 4, 12), ll=(8, 22, 5, 14), rl=(18, 20, 5, 8)),
        figure(bob=-1, al=(4, 13, 4, 10), ar=(22, 13, 4, 11), ll=(9, 22, 5, 12), rl=(17, 22, 5, 12)),
        figure(bob=-1, al=(5, 16, 4, 12), ar=(22, 10, 4, 8), ll=(7, 19, 5, 7), rl=(17, 22, 5, 14)),
        figure(al=(4, 14, 4, 11), ar=(23, 12, 4, 10), ll=(9, 21, 5, 10), rl=(17, 22, 5, 14)),
    ]
    run = run + [mirror(frame) for frame in run]
    jump = [
        figure(head_y=7, torso_y=16, torso_h=8, al=(2, 16, 4, 8), ar=(26, 16, 4, 8),
               ll=(5, 23, 5, 13), rl=(21, 23, 5, 13)),
        figure(head_y=9, torso_y=18, torso_h=7, al=(1, 18, 4, 7), ar=(27, 18, 4, 7),
               ll=(4, 24, 6, 12), rl=(21, 24, 6, 12)),
        figure(head_y=3, torso_y=13, al=(3, 6, 4, 10), ar=(25, 6, 4, 10),
               ll=(9, 22, 5, 14), rl=(18, 22, 5, 14)),
        figure(head_y=2, torso_y=12, al=(4, 1, 4, 12), ar=(24, 1, 4, 12),
               ll=(10, 22, 5, 14), rl=(17, 22, 5, 14)),
        figure(al=(2, 0, 4, 14), ar=(26, 0, 4, 14), ll=(10, 22, 5, 14), rl=(17, 22, 5, 14)),
        figure(head_y=4, torso_y=14, torso_h=8, al=(0, 14, 8, 4), ar=(24, 14, 8, 4),
               ll=(8, 20, 5, 6), rl=(18, 20, 5, 6)),
        figure(head_y=2, torso_y=12, al=(4, 2, 4, 12), ar=(24, 2, 4, 12),
               ll=(8, 22, 5, 13), rl=(18, 22, 5, 14)),
        figure(head_y=8, torso_y=17, torso_h=8, al=(0, 16, 8, 4), ar=(24, 16, 8, 4),
               ll=(4, 24, 6, 12), rl=(21, 24, 6, 12)),
    ]
    left = []
    for shift, lead, trail in ((0, 8, 12), (-2, 6, 10), (-4, 5, 8), (-5, 3, 6)):
        left.append(figure(shift=shift, al=(4, 12, 4, 10), ar=(22, 14, 4, 11),
                           ll=(lead, 22, 5, 14), rl=(18, 22, 5, trail)))
    right = [mirror(frame) for frame in left]
    return run + jump + left + right


def stamp(rgb, frame, grid):
    col = frame % COLS
    row = frame // COLS
    ox = col * CELL_W
    oy = row * CELL_H
    for y in range(LOGIC_H):
        for x in range(LOGIC_W):
            color = grid[y * LOGIC_W + x]
            if color is None:
                continue
            for sy in range(SCALE):
                base = ((oy + y * SCALE + sy) * SHEET + ox + x * SCALE) * 3
                for sx in range(SCALE):
                    i = base + sx * 3
                    rgb[i] = color[0]
                    rgb[i + 1] = color[1]
                    rgb[i + 2] = color[2]


SHEET_TXT = """\
# Pose guide for the image editor. The canvas is 1024x1024.
# Empty pixels are #FF00FF, or any pixel with alpha below 128.
# Cells are 128x160, 8 columns, counted left to right, then down.
# 160 does not divide 1024, so the bottom 64 pixels stay empty.
#
#   0  1  2  3  4  5  6  7
#   8  9 10 11 12 13 14 15
#  16 17 18 19 20 21 22 23
#
# Keep each pose inside its cell. Feet sit on the bottom edge of the cell,
# except the tuck, whose feet pull up. Bright magenta inside a character
# is treated as empty.
# This file is the map for runner.png. Save a painted character as
# name.runner.png, for example blazer.runner.png. The part before the
# dot is the name on the menu. The part after it selects this sheet.
#
# run    0-7    contact, passing, knee up, reach, then the other foot
# jump   8-15   squat, crouch, launch, rise, stretch, tuck, fall, land
# left   16-19  lean left, shallow to deep
# right  20-23  lean right, shallow to deep

frame 128 160
pack 52 64

run 0-7
jump 8-15
left 16-19
right 20-23
"""


def main():
    out = Path(__file__).resolve().parents[1] / "sprites"
    out.mkdir(parents=True, exist_ok=True)
    old = out / "sheet.txt"
    if old.exists():
        old.unlink()
    frames = poses()
    rgb = bytearray((255, 0, 255)) * (SHEET * SHEET)
    for index, grid in enumerate(frames):
        stamp(rgb, index, grid)
    write_png(out / "runner.png", SHEET, SHEET, rgb)
    (out / "runner.txt").write_text(SHEET_TXT)
    print(f"runner.png {SHEET}x{SHEET}, {len(frames)} poses, runner.txt")


if __name__ == "__main__":
    main()
