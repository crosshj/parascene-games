#!/usr/bin/env python3
"""Pack the logo screens into one PlayStation header.

screens/parascene.png is the studio logo shown at boot.
screens/title.png is the game logo on the title screen.

Each image is at most 256 pixels wide and becomes an 8-bit texture with its
own 256-color palette. Pixels with alpha below 128 use index 0, which draws
as empty. Size the copies before the build; this tool does not resize.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pack_sprites as ps

ART = (
    ("PARASCENE", "parascene.png", (512, 256), (0, 480)),
    ("TITLE", "title.png", (512, 0), (256, 480)),
)
MAX_W = 256
MAX_H = 256


def pack_one(path):
    w, h, pixels = ps.read_png(path)
    if w > MAX_W or h > MAX_H:
        raise SystemExit(f"{path.name} is {w}x{h}; resize it to fit {MAX_W}x{MAX_H}")
    stride = w + (w & 1)
    solid = [(p[0], p[1], p[2]) for p in pixels if p[3] >= 128]
    if not solid:
        raise SystemExit(f"{path.name} has no visible pixels")
    palette = ps.quantize(solid, 255)
    cache = {}
    out = bytearray(stride * h)
    for y in range(h):
        for x in range(w):
            r, g, b, a = pixels[y * w + x]
            if a < 128:
                continue
            key = (r, g, b)
            index = cache.get(key)
            if index is None:
                index = 1 + ps.nearest(key, palette)
                cache[key] = index
            out[y * stride + x] = index
    clut = [0] * 256
    for i, (r, g, b) in enumerate(palette):
        # The STP bit keeps true black opaque; only index 0 draws as empty.
        clut[i + 1] = ps.rgb15(r, g, b) | 0x8000
    return w, h, stride, out, clut, len(palette)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: pack_screens.py SCREENS_DIR OUTPUT_HEADER")
    folder = Path(sys.argv[1])
    header = Path(sys.argv[2])
    lines = [
        "#ifndef SCREEN_ART_H",
        "#define SCREEN_ART_H",
        "/* Packed from screens/*.png. 8-bit textures, index 0 is empty. */",
        f"#define ART_COUNT {len(ART)}",
    ]
    rows = []
    blob = bytearray()
    for n, (name, file, (vx, vy), (cx, cy)) in enumerate(ART):
        w, h, stride, pix, clut, used = pack_one(folder / file)
        print(f"{file}: {w}x{h}, {used} colors")
        lines.append(f"#define ART_{name} {n}")
        rows.append((w, h, stride, vx, vy, cx, cy, len(blob), clut))
        blob += pix
        while len(blob) % 4:
            blob.append(0)

    def table(ctype, ident, values):
        lines.append(f"static const {ctype} {ident}[ART_COUNT] = {{ " + ", ".join(str(v) for v in values) + " };")

    table("uint16_t", "art_w", [r[0] for r in rows])
    table("uint16_t", "art_h", [r[1] for r in rows])
    table("uint16_t", "art_stride", [r[2] for r in rows])
    table("uint16_t", "art_vram_x", [r[3] for r in rows])
    table("uint16_t", "art_vram_y", [r[4] for r in rows])
    table("uint16_t", "art_clut_x", [r[5] for r in rows])
    table("uint16_t", "art_clut_y", [r[6] for r in rows])
    table("uint32_t", "art_off", [r[7] for r in rows])
    lines.append("static const uint16_t art_clut[ART_COUNT][256] __attribute__((aligned(4))) = {")
    for r in rows:
        clut = r[8]
        lines.append("    {")
        for i in range(0, 256, 16):
            lines.append("        " + ", ".join(f"0x{v:04x}" for v in clut[i:i + 16]) + ",")
        lines.append("    },")
    lines.append("};")
    lines.append(f"static const uint8_t art_pix[{len(blob)}] __attribute__((aligned(4))) = {{")
    lines.append(ps.c_bytes(blob))
    lines.append("};")
    lines.append("#endif")
    header.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
