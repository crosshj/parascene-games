#!/usr/bin/env python3
"""Build Lumenwild's sky texture: a fixed dark-to-horizon gradient, 1-pixel
vertical spires, and single-pixel stars.

Spires are one texel wide (the thinnest a texture can store). Each spire is
itself a vertical gradient of palette indices: near-sky-dark at the tip,
fully saturated at the horizon. Only the hue of those slots is cycled at
runtime; brightness stays with the texel so the dark-to-bright falloff holds.

Palette split:
  1 .. GRAD_LAST              fixed sky gradient
  CYCLE_FIRST .. STAR_FIRST-1 spire ramp (brightness x hue), CLUT-cycled
  STAR_FIRST .. CYCLE_LAST    stars (twinkled randomly)
"""

import colorsys
import random
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pack_sprites as ps

# Match the screen width so one texel is one horizontal pixel — no X stretch.
# An 8-bit texture page is only 256 texels wide, so the right 64 columns live
# in the neighbouring page (VRAM_X + 128) and are drawn with a second quad.
W, H = 320, 64
PAGE_W = 256
# X spans 768..928 (two 8-bit pages). Y starts at 64 so we stay clear of
# the runner sprite CLUTs at (896, 0) / (896, 1) — overwriting those made
# characters render as a solid green block.
VRAM_X, VRAM_Y = 768, 64
VRAM_X2 = VRAM_X + 128  # next 8-bit page
# Under the framebuffers, clear of the 4-bit sprite pages at x 384 and 640.
CLUT_X, CLUT_Y = 0, 481

GRAD_COUNT = 32
# Brightness from sky-dark → full chroma, times a smooth hue ring.
BRIGHT_STEPS = 8
HUE_STEPS = 24
BEAM_RING = BRIGHT_STEPS * HUE_STEPS  # 192
# Many independent star slots so only a few need be lit at once.
STAR_COUNT = 28
CYCLE_COUNT = BEAM_RING + STAR_COUNT

# Near-sky dark used at spire tips (matches the gradient language).
SKY_DARK = (8, 4, 22)
SKY_MID = (48, 18, 72)


def lerp(a, b, t):
    return a + (b - a) * t


def lerp3(a, b, t):
    return tuple(int(lerp(a[i], b[i], t) + 0.5) for i in range(3))


def clamp(v, lo=0, hi=255):
    return lo if v < lo else hi if v > hi else v


def wrap_dx(x, cx, width):
    d = abs(x - cx) % width
    return min(d, width - d)


def build_spires(rng):
    """Exactly one texel wide."""
    spires = []
    x = rng.randint(0, 3)
    while x < W:
        height = rng.uniform(0.28, 0.98)
        hue = rng.random()
        spires.append((x, height, hue))
        if x < 2:
            spires.append((x + W, height, hue))
        if x > W - 3:
            spires.append((x - W, height, hue))
        x += rng.randint(2, 6)
    return spires


def paint_layers():
    top = (4, 2, 18)
    horizon = (72, 28, 96)
    rng = random.Random(0xA71C0DE)
    spires = build_spires(rng)

    base = []
    # Per pixel: 0 = empty, else (bright_01, hue_01) for the spire texel.
    spire = [[None] * W for _ in range(H)]
    star = [[0] * W for _ in range(H)]

    for y in range(H):
        ty = y / (H - 1)
        base.append([lerp3(top, horizon, ty * ty) for _ in range(W)])

    for cx, height, hue in spires:
        top_ty = 1.0 - height
        for y in range(H):
            ty = y / (H - 1)
            if ty < top_ty:
                continue
            # 0 at tip, 1 at horizon — drives the spire's own gradient.
            vert = (ty - top_ty) / max(height, 1e-6)
            x = cx % W
            spire[y][x] = (vert, hue)

    # Dense field of single-pixel stars; CLUT keeps most dim so only a few
    # sparkle at a time.
    placed = 0
    for _ in range(800):
        if placed >= 240:
            break
        sx = rng.randint(0, W - 1)
        sy = rng.randint(0, int(H * 0.78))
        if spire[sy][sx] is not None or star[sy][sx]:
            continue
        star[sy][sx] = 1 + (placed % STAR_COUNT)
        placed += 1

    return base, spire, star


def make_gradient_palette():
    top = (4, 2, 18)
    horizon = (72, 28, 96)
    return [lerp3(top, horizon, i / (GRAD_COUNT - 1)) for i in range(GRAD_COUNT)]


def sat_color(hue, sat, val):
    r, g, b = colorsys.hsv_to_rgb(hue, sat, val)
    return (clamp(int(r * 255)), clamp(int(g * 255)), clamp(int(b * 255)))


def make_beam_palette():
    """BRIGHT_STEPS from sky-dark to full saturation, each a HUE_STEPS ring."""
    out = []
    for b in range(BRIGHT_STEPS):
        t = b / (BRIGHT_STEPS - 1)
        # Early steps stay near the background; only the top steps bloom.
        sat = lerp(0.05, 0.95, t * t)
        val = lerp(0.10, 0.95, t)
        dark = lerp3(SKY_DARK, SKY_MID, t)
        for h in range(HUE_STEPS):
            hue = h / HUE_STEPS
            vivid = sat_color(hue, sat, val)
            # Mix toward sky-dark at low brightness so tips read as background.
            out.append(lerp3(dark, vivid, t))
    assert len(out) == BEAM_RING
    return out


def make_star_palette():
    rng = random.Random(0x57A25)
    out = []
    for i in range(STAR_COUNT):
        # Cool whites with a little colour bias per slot.
        tint = rng.choice([
            (255, 255, 255),
            (220, 230, 255),
            (255, 240, 220),
            (210, 220, 255),
            (255, 230, 245),
            (200, 230, 255),
        ])
        out.append(tint)
    return out


def nearest(rgb, palette):
    return 1 + ps.nearest(rgb, palette)


def encode(base, spire, star, grad_pal, beam_pal, star_pal):
    out = bytearray(W * H)
    preview = []
    cycle_pal = beam_pal + star_pal
    for y in range(H):
        prow = []
        for x in range(W):
            if star[y][x]:
                si = min(star[y][x] - 1, STAR_COUNT - 1)
                idx = GRAD_COUNT + 1 + BEAM_RING + si
                rgb = star_pal[si]
            elif spire[y][x] is not None:
                vert, hue = spire[y][x]
                bright = int(vert * (BRIGHT_STEPS - 1) + 0.5)
                if bright > BRIGHT_STEPS - 1:
                    bright = BRIGHT_STEPS - 1
                hue_i = int(hue * HUE_STEPS) % HUE_STEPS
                idx = GRAD_COUNT + 1 + bright * HUE_STEPS + hue_i
                rgb = beam_pal[bright * HUE_STEPS + hue_i]
            else:
                idx = nearest(base[y][x], grad_pal)
                rgb = grad_pal[idx - 1]
            out[y * W + x] = idx
            prow.append(rgb)
        preview.append(prow)
    clut = [0] * 256
    for i, rgb in enumerate(grad_pal):
        clut[i + 1] = ps.rgb15(*rgb) | 0x8000
    for i, rgb in enumerate(cycle_pal):
        clut[GRAD_COUNT + 1 + i] = ps.rgb15(*rgb) | 0x8000
    return out, clut, preview


def write_png(path, pixels):
    raw = bytearray()
    for row in pixels:
        raw.append(0)
        for r, g, b in row:
            raw.extend((r, g, b))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(
            ">I", zlib.crc32(tag + data) & 0xFFFFFFFF
        )

    ihdr = struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )


def write_header(path, pix, clut):
    cycle_first = GRAD_COUNT + 1
    star_first = GRAD_COUNT + 1 + BEAM_RING
    cycle_last = GRAD_COUNT + CYCLE_COUNT
    lines = [
        "#ifndef LUMEN_SKY_TEX_H",
        "#define LUMEN_SKY_TEX_H",
        "/* Soft Lumenwild sky. Generated by tools/make_lumen_sky.py. */",
        f"#define LUMEN_SKY_W {W}",
        f"#define LUMEN_SKY_H {H}",
        f"#define LUMEN_SKY_PAGE_W {PAGE_W}",
        f"#define LUMEN_SKY_VRAM_X {VRAM_X}",
        f"#define LUMEN_SKY_VRAM_X2 {VRAM_X2}",
        f"#define LUMEN_SKY_VRAM_Y {VRAM_Y}",
        # 8-bit tpages are 256 texels tall; UV V is relative to the page
        # base (0 or 256), not the LoadImage Y. Our bitmap starts at this V.
        f"#define LUMEN_SKY_V0 {VRAM_Y % 256}",
        f"#define LUMEN_SKY_CLUT_X {CLUT_X}",
        f"#define LUMEN_SKY_CLUT_Y {CLUT_Y}",
        f"#define LUMEN_SKY_GRAD_LAST {GRAD_COUNT}",
        f"#define LUMEN_SKY_CYCLE_FIRST {cycle_first}",
        f"#define LUMEN_SKY_CYCLE_LAST {cycle_last}",
        f"#define LUMEN_SKY_BEAM_RING {HUE_STEPS}",
        f"#define LUMEN_SKY_BEAM_BANDS {BRIGHT_STEPS}",
        f"#define LUMEN_SKY_STAR_FIRST {star_first}",
        f"#define LUMEN_SKY_STAR_COUNT {STAR_COUNT}",
        f"#define LUMEN_SKY_STAR_LIT 2",
        "static const uint16_t lumen_sky_clut[256] __attribute__((aligned(4))) = {",
    ]
    for i in range(0, 256, 16):
        lines.append("    " + ", ".join(f"0x{v:04x}" for v in clut[i : i + 16]) + ",")
    lines.append("};")
    lines.append(
        f"static const uint8_t lumen_sky_pix[{W * H}] __attribute__((aligned(4))) = {{"
    )
    lines.append(ps.c_bytes(pix))
    lines.append("};")
    lines.append("#endif")
    path.write_text("\n".join(lines) + "\n")


def main():
    if len(sys.argv) < 2:
        raise SystemExit("usage: make_lumen_sky.py OUTPUT_HEADER [PREVIEW_PNG]")
    base, spire, star = paint_layers()
    grad_pal = make_gradient_palette()
    beam_pal = make_beam_palette()
    star_pal = make_star_palette()
    pix, clut, preview = encode(base, spire, star, grad_pal, beam_pal, star_pal)
    write_header(Path(sys.argv[1]), pix, clut)
    print(
        f"lumen sky: {W}x{H}, grad 1..{GRAD_COUNT}, "
        f"spires {GRAD_COUNT + 1}..{GRAD_COUNT + BEAM_RING} "
        f"({BRIGHT_STEPS} bright x {HUE_STEPS} hue), "
        f"stars {GRAD_COUNT + 1 + BEAM_RING}..{GRAD_COUNT + CYCLE_COUNT}"
    )
    if len(sys.argv) >= 3:
        write_png(Path(sys.argv[2]), preview)
        print(f"wrote {sys.argv[2]}")


if __name__ == "__main__":
    main()
