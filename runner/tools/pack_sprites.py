#!/usr/bin/env python3
"""Pack character sheets into one PlayStation sprite header.

A base sheet and its map share a name:

    sprites/runner.png
    sprites/runner.txt

A character names itself and the base it uses:

    sprites/blazer.runner.png

The base sheet is also a character. runner.png appears in the menu as
RUNNER and uses runner.txt. #FF00FF (and nearby magenta) is empty.
Each character is reduced to 15 colors plus that empty index.
"""

import re
import struct
import sys
import zlib
from pathlib import Path

PAGE_W = 256
PAGE_H = 256
# 4-bit pages that sit clear of the framebuffer and the font at (960, 0).
PAGE_SLOTS = ((640, 0), (640, 256), (384, 0), (384, 256))
CLUT_X = 896
REQUIRED = ("RUN", "JUMP", "LEFT", "RIGHT")


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_png(path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path} is not a PNG")
    pos = 8
    width = height = depth = color = interlace = None
    palette = []
    trns = b""
    idat = b""
    while pos + 8 <= len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        tag = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if tag == b"IHDR":
            width, height, depth, color, _comp, _filt, interlace = struct.unpack(">IIBBBBB", chunk)
        elif tag == b"PLTE":
            palette = [tuple(chunk[i:i + 3]) for i in range(0, len(chunk), 3)]
        elif tag == b"tRNS":
            trns = chunk
        elif tag == b"IDAT":
            idat += chunk
        elif tag == b"IEND":
            break
    if interlace:
        raise SystemExit(f"{path} is interlaced; export a non-interlaced PNG")
    if depth != 8 or color not in (2, 3, 6):
        raise SystemExit(f"{path} must be 8-bit indexed, RGB, or RGBA")
    bpp = {2: 3, 3: 1, 6: 4}[color]
    raw = zlib.decompress(idat)
    stride = width * bpp
    rows = []
    prev = bytearray(stride)
    i = 0
    for _y in range(height):
        filt = raw[i]
        i += 1
        row = bytearray(raw[i:i + stride])
        i += stride
        for x in range(stride):
            left = row[x - bpp] if x >= bpp else 0
            up = prev[x]
            ul = prev[x - bpp] if x >= bpp else 0
            if filt == 0:
                pred = 0
            elif filt == 1:
                pred = left
            elif filt == 2:
                pred = up
            elif filt == 3:
                pred = (left + up) // 2
            elif filt == 4:
                pred = paeth(left, up, ul)
            else:
                raise SystemExit(f"{path} has an unknown PNG filter")
            row[x] = (row[x] + pred) & 255
        rows.append(row)
        prev = row
    if color == 3:
        alphas = [255] * len(palette)
        for n, a in enumerate(trns):
            if n < len(alphas):
                alphas[n] = a
        colors = [(r, g, b, a) for (r, g, b), a in zip(palette, alphas)]
        pixels = []
        for row in rows:
            for index in row:
                if index >= len(colors):
                    raise SystemExit(f"{path.name} uses a palette index the file does not have")
                pixels.append(colors[index])
        return width, height, pixels
    pixels = []
    for row in rows:
        for x in range(0, stride, bpp):
            r, g, b = row[x], row[x + 1], row[x + 2]
            a = row[x + 3] if bpp == 4 else 255
            pixels.append((r, g, b, a))
    return width, height, pixels


def is_magenta(r, g, b, a):
    if a < 128:
        return True
    return r >= 220 and b >= 220 and g <= 48 and abs(int(r) - int(b)) <= 48


def parse_map(path):
    frame_w = frame_h = None
    pack_w = pack_h = None
    anims = []
    for lineno, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.replace(",", " ").split()
        if parts[0] == "frame":
            if len(parts) != 3:
                raise SystemExit(f"{path.name}:{lineno}: expected 'frame WIDTH HEIGHT'")
            frame_w, frame_h = int(parts[1]), int(parts[2])
            continue
        if parts[0] == "pack":
            if len(parts) != 3:
                raise SystemExit(f"{path.name}:{lineno}: expected 'pack WIDTH HEIGHT'")
            pack_w, pack_h = int(parts[1]), int(parts[2])
            continue
        if frame_w is None:
            raise SystemExit(f"{path.name}:{lineno}: set the cell size with 'frame WIDTH HEIGHT' first")
        indices = []
        for token in parts[1:]:
            if "-" in token:
                a, b = token.split("-", 1)
                indices.extend(range(int(a), int(b) + 1))
            else:
                indices.append(int(token))
        if not indices:
            raise SystemExit(f"{path.name}:{lineno}: {parts[0]} needs at least one frame")
        anims.append((parts[0], indices))
    if frame_w is None or not anims:
        raise SystemExit(f"{path.name} needs a frame size and at least one animation")
    if pack_w is None:
        pack_w, pack_h = frame_w, frame_h
    if pack_w > PAGE_W or pack_h > PAGE_H or pack_w % 4 or pack_h < 1:
        raise SystemExit(f"{path.name}: packed width must be a multiple of 4 and at most {PAGE_W}")
    return frame_w, frame_h, pack_w, pack_h, anims


def check_frames(path, sheet_w, sheet_h, frame_w, frame_h, anims):
    if sheet_w % frame_w or frame_h > sheet_h:
        raise SystemExit(f"{path.name}: {frame_w}x{frame_h} cells do not fit a {sheet_w}x{sheet_h} sheet")
    cols = sheet_w // frame_w
    rows = sheet_h // frame_h
    limit = cols * rows
    for name, indices in anims:
        for index in indices:
            if index < 0 or index >= limit:
                raise SystemExit(f"{path.name}: {name} frame {index} is outside the {cols}x{rows} grid")
    return cols


def cut_cell(pixels, sheet_w, frame_w, frame_h, cols, index):
    col = index % cols
    row = index // cols
    x0 = col * frame_w
    y0 = row * frame_h
    cell = []
    for y in range(frame_h):
        src = (y0 + y) * sheet_w + x0
        cell.extend(pixels[src:src + frame_w])
    return cell


def downsample(cell, frame_w, frame_h, pack_w, pack_h):
    out = []
    for y in range(pack_h):
        sy0 = y * frame_h / pack_h
        sy1 = (y + 1) * frame_h / pack_h
        y_start = int(sy0)
        y_end = min(frame_h, max(y_start + 1, int(sy1) if sy1 == int(sy1) else int(sy1) + 1))
        for x in range(pack_w):
            sx0 = x * frame_w / pack_w
            sx1 = (x + 1) * frame_w / pack_w
            x_start = int(sx0)
            x_end = min(frame_w, max(x_start + 1, int(sx1) if sx1 == int(sx1) else int(sx1) + 1))
            rs = gs = bs = n = 0
            for yy in range(y_start, y_end):
                row = (yy * frame_w)
                for xx in range(x_start, x_end):
                    pixel = cell[row + xx]
                    if pixel is None or is_magenta(*pixel):
                        continue
                    rs += pixel[0]
                    gs += pixel[1]
                    bs += pixel[2]
                    n += 1
            if n == 0:
                out.append(None)
            else:
                out.append((rs // n, gs // n, bs // n))
    return out


def quantize(samples, limit=15):
    def span(box):
        if len(box) <= 1:
            return 0, 0
        ranges = [max(p[c] for p in box) - min(p[c] for p in box) for c in range(3)]
        channel = max(range(3), key=lambda i: ranges[i])
        return channel, ranges[channel]

    def cut(box, channel):
        # Split on a color boundary so a flat color stays in one box.
        box.sort(key=lambda p: p[channel])
        target = len(box) // 2
        value = box[target][channel]
        left = target
        while left > 0 and box[left - 1][channel] == value:
            left -= 1
        right = target
        while right < len(box) and box[right][channel] == value:
            right += 1
        if left == 0 and right == len(box):
            return len(box) // 2
        if left == 0:
            return right
        if right == len(box):
            return left
        if abs(left - target) <= abs(right - target):
            return left
        return right

    boxes = [list(samples)]
    while len(boxes) < limit:
        boxes.sort(key=lambda box: span(box)[1], reverse=True)
        channel, width = span(boxes[0])
        if width <= 0:
            break
        box = boxes.pop(0)
        mid = cut(box, channel)
        if mid <= 0 or mid >= len(box):
            boxes.append(box)
            break
        boxes.append(box[:mid])
        boxes.append(box[mid:])
    colors = []
    seen = set()
    for box in boxes:
        if not box:
            continue
        n = len(box)
        color = tuple(sum(p[c] for p in box) // n for c in range(3))
        key = rgb15(*color)
        if key in seen:
            continue
        seen.add(key)
        colors.append(color)
    return colors


def nearest(color, palette):
    r, g, b = color
    best, bestd = 0, 1 << 30
    for i, (pr, pg, pb) in enumerate(palette):
        d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
        if d < bestd:
            best, bestd = i, d
    return best


def to_index(frames, palette):
    indexed = []
    for frame in frames:
        indices = []
        for pixel in frame:
            if pixel is None:
                indices.append(0)
            else:
                indices.append(1 + nearest(pixel, palette))
        indexed.append(indices)
    return indexed


def rgb15(r, g, b):
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)


def display_name(stem):
    name = re.sub(r"[^A-Za-z0-9]+", " ", stem).strip().upper()
    return (name or "CHARACTER")[:15]


class Page:
    def __init__(self, x, y):
        self.x = x
        self.y = y
        self.h = 0
        self.pix = [0] * (PAGE_W * PAGE_H)


def encode_page(page):
    raw = bytearray(PAGE_W * page.h // 2)
    for i, color in enumerate(page.pix[:PAGE_W * page.h]):
        byte = i >> 1
        if i & 1:
            raw[byte] |= (color & 15) << 4
        else:
            raw[byte] |= color & 15
    return raw


def c_bytes(data, per=16):
    lines = []
    for i in range(0, len(data), per):
        chunk = data[i:i + per]
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    return "\n".join(lines)


def anim_ident(text, name):
    ident = re.sub(r"[^A-Z0-9]", "_", name.upper())
    if ident not in REQUIRED:
        raise SystemExit(f"{text.name}: unknown animation '{name}'")
    return ident


def layout_for(text):
    frame_w, frame_h, pack_w, pack_h, anims = parse_map(text)
    by_name = {}
    for name, order in anims:
        ident = anim_ident(text, name)
        if ident in by_name:
            raise SystemExit(f"{text.name}: {name} is listed twice")
        by_name[ident] = order
    missing = [name for name in REQUIRED if name not in by_name]
    if missing:
        raise SystemExit(f"{text.name} is missing {', '.join(missing)}")
    packed_ids = []
    play = []
    begins = []
    counts = []
    for ident in REQUIRED:
        begins.append(len(play))
        order = by_name[ident]
        counts.append(len(order))
        for index in order:
            if index not in packed_ids:
                packed_ids.append(index)
            play.append(packed_ids.index(index))
    cols_per_page = PAGE_W // pack_w
    block_rows = (len(packed_ids) + cols_per_page - 1) // cols_per_page
    block_h = block_rows * pack_h
    if block_h > PAGE_H:
        raise SystemExit(f"{text.name}: one character's frames do not fit in a texture page")
    uvs = [((i % cols_per_page) * pack_w, (i // cols_per_page) * pack_h) for i in range(len(packed_ids))]
    return {
        "frame_w": frame_w,
        "frame_h": frame_h,
        "pack_w": pack_w,
        "pack_h": pack_h,
        "anims": anims,
        "packed_ids": packed_ids,
        "play": play,
        "begins": begins,
        "counts": counts,
        "block_h": block_h,
        "uvs": uvs,
    }


def characters_in(folder):
    bases = []
    found = []
    for png in sorted(folder.glob("*.png")):
        if "." not in png.stem:
            if png.with_suffix(".txt").exists():
                bases.append((png, png.stem, png.stem))
            continue
        name, base = png.stem.rsplit(".", 1)
        if not name or not base:
            raise SystemExit(f"{png.name} needs a character name and a base, like blazer.runner.png")
        sheet = folder / f"{base}.png"
        text = folder / f"{base}.txt"
        if not sheet.exists() or not text.exists():
            raise SystemExit(f"{png.name} needs {base}.png and {base}.txt beside it")
        found.append((png, name, base))
    if not bases and not found:
        raise SystemExit("no characters; add runner.png with runner.txt, or name.base.png")
    return bases + found


def pack(folder):
    folder = Path(folder)
    grouped = {}
    order = []
    for png, name, base in characters_in(folder):
        if base not in grouped:
            grouped[base] = layout_for(folder / f"{base}.txt")
            grouped[base]["name"] = base
            grouped[base]["files"] = []
            order.append(base)
        grouped[base]["files"].append((png, name))
    bases = [grouped[name] for name in order]

    pages = []
    characters = []
    cluts = []

    def alloc(height):
        for page in pages:
            if page.h + height <= PAGE_H:
                return page
        if len(pages) >= len(PAGE_SLOTS):
            raise SystemExit("these character sheets do not fit in video memory")
        page = Page(*PAGE_SLOTS[len(pages)])
        pages.append(page)
        return page

    for base_index, base in enumerate(bases):
        for png, name in base["files"]:
            sheet_w, sheet_h, pixels = read_png(png)
            cols = check_frames(png, sheet_w, sheet_h, base["frame_w"], base["frame_h"], base["anims"])
            frames = []
            samples = []
            for index in base["packed_ids"]:
                cell = cut_cell(pixels, sheet_w, base["frame_w"], base["frame_h"], cols, index)
                small = downsample(cell, base["frame_w"], base["frame_h"], base["pack_w"], base["pack_h"])
                frames.append(small)
                samples.extend(pixel for pixel in small if pixel is not None)
            if not samples:
                raise SystemExit(f"{png.name} has no visible pixels; #FF00FF is empty")
            palette = quantize(samples)
            indexed = to_index(frames, palette)
            page = alloc(base["block_h"])
            origin = page.h
            for i, frame in enumerate(indexed):
                u, v = base["uvs"][i]
                for y in range(base["pack_h"]):
                    for x in range(base["pack_w"]):
                        page.pix[(origin + v + y) * PAGE_W + u + x] = frame[y * base["pack_w"] + x]
            page.h += base["block_h"]
            label = display_name(name)
            characters.append((label, pages.index(page), origin, base_index))
            clut = [0] + [rgb15(*color) for color in palette]
            clut.extend([0] * (16 - len(clut)))
            cluts.append(clut[:16])
            print(f"{png.name} -> {label} using {base['name']} ({len(palette)} colors)")

    return {
        "bases": bases,
        "pages": pages,
        "raw_pages": [encode_page(page) for page in pages],
        "characters": characters,
        "cluts": cluts,
    }


def emit(header, packed):
    bases = packed["bases"]
    pages = packed["pages"]
    characters = packed["characters"]
    cluts = packed["cluts"]
    anim_defs = "\n".join(f"#define SPR_ANIM_{name} {n}" for n, name in enumerate(REQUIRED))
    begin_rows = ",\n".join("    { " + ", ".join(str(v) for v in base["begins"]) + " }" for base in bases)
    count_rows = ",\n".join("    { " + ", ".join(str(v) for v in base["counts"]) + " }" for base in bases)
    play = []
    uvs = []
    list_off = []
    uv_off = []
    for base in bases:
        list_off.append(len(play))
        uv_off.append(len(uvs))
        play.extend(base["play"])
        uvs.extend(base["uvs"])
    page_x = ", ".join(str(page.x) for page in pages)
    page_y = ", ".join(str(page.y) for page in pages)
    page_h = ", ".join(str(page.h) for page in pages)
    offsets = []
    cursor = 0
    blobs = []
    for raw in packed["raw_pages"]:
        offsets.append(str(cursor))
        blobs.append(c_bytes(raw))
        cursor += len(raw)
    tex_body = "\n".join(blobs)
    names = ",\n".join(f'    "{name}"' for name, _page, _v, _base in characters)
    char_page = ", ".join(str(page) for _name, page, _v, _base in characters)
    char_v = ", ".join(str(v) for _name, _page, v, _base in characters)
    char_base = ", ".join(str(base) for _name, _page, _v, base in characters)
    clut_rows = ",\n".join(
        "    { " + ", ".join(f"0x{c:04x}" for c in row) + " }" for row in cluts
    )
    text = f"""#ifndef SPRITE_SHEET_H
#define SPRITE_SHEET_H
/* Packed from sprites/*.png. Index 0 is empty. */
#define SPR_PAGE_W {PAGE_W}
#define SPR_PAGE_COUNT {len(pages)}
#define SPR_BASE_COUNT {len(bases)}
#define SPR_CHAR_COUNT {len(characters)}
#define SPR_ANIM_COUNT {len(REQUIRED)}
{anim_defs}
static const uint16_t spr_base_fw[SPR_BASE_COUNT] = {{ {", ".join(str(base["pack_w"]) for base in bases)} }};
static const uint16_t spr_base_fh[SPR_BASE_COUNT] = {{ {", ".join(str(base["pack_h"]) for base in bases)} }};
static const uint8_t spr_base_anim_begin[SPR_BASE_COUNT][SPR_ANIM_COUNT] = {{
{begin_rows}
}};
static const uint8_t spr_base_anim_count[SPR_BASE_COUNT][SPR_ANIM_COUNT] = {{
{count_rows}
}};
static const uint16_t spr_base_list_off[SPR_BASE_COUNT] = {{ {", ".join(str(v) for v in list_off)} }};
static const uint16_t spr_base_uv_off[SPR_BASE_COUNT] = {{ {", ".join(str(v) for v in uv_off)} }};
static const uint8_t spr_anim_frames[{len(play)}] = {{ {", ".join(str(i) for i in play)} }};
static const uint8_t spr_frame_u[{len(uvs)}] = {{ {", ".join(str(u) for u, _v in uvs)} }};
static const uint8_t spr_frame_v[{len(uvs)}] = {{ {", ".join(str(v) for _u, v in uvs)} }};
static const uint16_t spr_page_x[SPR_PAGE_COUNT] = {{ {page_x} }};
static const uint16_t spr_page_y[SPR_PAGE_COUNT] = {{ {page_y} }};
static const uint16_t spr_page_h[SPR_PAGE_COUNT] = {{ {page_h} }};
static const uint32_t spr_page_off[SPR_PAGE_COUNT] = {{ {", ".join(offsets)} }};
static const char spr_char_name[SPR_CHAR_COUNT][16] = {{
{names}
}};
static const uint8_t spr_char_base[SPR_CHAR_COUNT] = {{ {char_base} }};
static const uint8_t spr_char_page[SPR_CHAR_COUNT] = {{ {char_page} }};
static const uint8_t spr_char_v[SPR_CHAR_COUNT] = {{ {char_v} }};
static const uint16_t spr_clut_x[SPR_CHAR_COUNT] = {{ {", ".join([str(CLUT_X)] * len(characters))} }};
static const uint16_t spr_clut_y[SPR_CHAR_COUNT] = {{ {", ".join(str(i) for i in range(len(characters)))} }};
static const uint16_t spr_clut[SPR_CHAR_COUNT][16] __attribute__((aligned(4))) = {{
{clut_rows}
}};
static const uint8_t spr_tex[{cursor}] __attribute__((aligned(4))) = {{
{tex_body}
}};
#endif
"""
    Path(header).write_text(text)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: pack_sprites.py sprites-dir output.h")
    packed = pack(sys.argv[1])
    emit(sys.argv[2], packed)
    who = ", ".join(f"{name} ({packed['bases'][base]['name']})" for name, _p, _v, base in packed["characters"])
    print(f"pages {len(packed['pages'])} characters {who}")


if __name__ == "__main__":
    main()
