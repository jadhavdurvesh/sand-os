#!/usr/bin/env python3
"""SAND icon pack: crops the nine system icons out of assets/ui/app-icons.png and writes build/icons.sip.

icons.sip: "SIPK" u32 version(1) u32 count(9) u32 size(64), then count images of size x size pixels, each pixel a
little-endian u32 0xAARRGGBB (straight alpha). Order: CITADEL CAIRN LOCUS QUANTA CHRONOS KEYSTONE LUMEN, System Console, About SAND.
The artwork is square tiles on a dark poster; each tile is cut out with an anti-aliased rounded-corner mask."""
import struct
import sys

from PIL import Image, ImageDraw

SIZE = 64
CROP = 190
CENTERS = [(188, 357), (424, 357), (659, 357), (885, 357), (1117, 357), (1349, 357), (432, 671), (769, 671), (1102, 671)]


def mask(size, radius_ratio=0.26, inset=1.0, ss=4):
    big = size * ss
    m = Image.new("L", (big, big), 0)
    ImageDraw.Draw(m).rounded_rectangle((inset * ss, inset * ss, big - 1 - inset * ss, big - 1 - inset * ss), radius=int(big * radius_ratio), fill=255)
    return m.resize((size, size), Image.LANCZOS)


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "assets/ui/app-icons.png"
    dst = sys.argv[2] if len(sys.argv) > 2 else "build/icons.sip"
    im = Image.open(src).convert("RGB")
    alpha = mask(SIZE)
    out = bytearray(struct.pack("<4sIII", b"SIPK", 1, len(CENTERS), SIZE))
    for cx, cy in CENTERS:
        tile = im.crop((cx - CROP // 2, cy - CROP // 2, cx + CROP // 2, cy + CROP // 2)).resize((SIZE, SIZE), Image.LANCZOS).convert("RGBA")
        tile.putalpha(alpha)
        px = tile.load()
        for y in range(SIZE):
            for x in range(SIZE):
                r, g, b, a = px[x, y]
                out += struct.pack("<I", (a << 24) | (r << 16) | (g << 8) | b)
    open(dst, "wb").write(out)
    print(f"{dst}: {len(CENTERS)} icons, {len(out)} bytes")


if __name__ == "__main__":
    main()
