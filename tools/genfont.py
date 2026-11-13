#!/usr/bin/env python3
"""SAND Sans - the SAND OS system typeface, defined as geometric stroke outlines.

Every glyph is a set of centre-line polylines (arcs are flattened here, at build
time) on a 1000-unit em: baseline 0, x-height 500, cap height 700, ascender 740,
descender -190. The kernel rasteriser (kernel/graphics/font.c) strokes each polyline
with a round-capped pen of any weight and anti-aliases it, so one outline set
serves every size and weight (light / regular / medium).

  python3 tools/genfont.py emit  kernel/graphics/font_data.c
  python3 tools/genfont.py preview build/font-specimen.png
"""
import math
import sys

B, XC, CC, AC, DC = 38, 462, 662, 702, -152   # centre-line heights (outer edges 0/500/700/740/-190)
MONO_ADVANCE = 600


def ell(cx, cy, rx, ry, a0, a1, step=10):
    n = max(2, int(abs(a1 - a0) / step) + 1)
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


def full(cx, cy, rx, ry):
    return ell(cx, cy, rx, ry, 0, 360)


def dot(x, y):
    return [(x, y), (x, y + 6)]


G = {}      # char -> (advance, [polylines])


def g(ch, adv, *lines):
    G[ch] = (adv, [list(l) for l in lines])


def flip_x(lines, adv):
    return [[(adv - x, y) for x, y in l] for l in lines]


def rot180(lines, cx, cy):
    return [[(2 * cx - x, 2 * cy - y) for x, y in l] for l in lines]


# ---- capitals -----------------------------------------------------------
g('A', 640, [(80, B), (320, CC), (560, B)], [(170, 235), (470, 235)])
g('B', 610, [(100, B), (100, CC)],
  [(100, CC), (300, CC)] + ell(300, 506, 160, 156, 90, -90) + [(100, 350)],
  [(100, 350), (320, 350)] + ell(320, 194, 170, 156, 90, -90) + [(100, B)])
g('C', 650, ell(340, 350, 235, 312, 38, 322))
g('D', 660, [(100, B), (100, CC)], [(100, CC), (270, CC)] + ell(270, 350, 270, 312, 90, -90) + [(100, B)])
g('E', 540, [(100, B), (100, CC)], [(100, CC), (450, CC)], [(100, 350), (400, 350)], [(100, B), (450, B)])
g('F', 520, [(100, B), (100, CC)], [(100, CC), (450, CC)], [(100, 350), (400, 350)])
g('G', 670, ell(345, 350, 240, 312, 40, 360) + [(345, 350)])
g('H', 660, [(100, B), (100, CC)], [(560, B), (560, CC)], [(100, 350), (560, 350)])
g('I', 260, [(130, B), (130, CC)])
g('J', 430, [(320, CC), (320, 165)] + ell(200, 165, 120, 127, 0, -165))
g('K', 600, [(100, B), (100, CC)], [(510, CC), (110, 255)], [(235, 375), (530, B)])
g('L', 480, [(100, B), (100, CC)], [(100, B), (430, B)])
g('M', 800, [(100, B), (100, CC), (400, 190), (700, CC), (700, B)])
g('N', 670, [(100, B), (100, CC), (570, B), (570, CC)])
g('O', 710, full(355, 350, 255, 312))
g('P', 590, [(100, B), (100, CC)], [(100, CC), (290, CC)] + ell(290, 484, 180, 178, 90, -90) + [(100, 306)])
g('Q', 710, full(355, 350, 255, 312), [(410, 190), (590, 10)])
g('R', 620, [(100, B), (100, CC)], [(100, CC), (290, CC)] + ell(290, 484, 180, 178, 90, -90) + [(100, 306)],
  [(270, 306), (530, B)])
S_UP = ell(285, 506, 175, 156, 28, 270)
S_LO = ell(285, 194, 190, 156, 90, -152)
g('S', 570, S_UP + S_LO)
g('T', 570, [(50, CC), (520, CC)], [(285, CC), (285, B)])
g('U', 660, [(100, CC), (100, 235)] + ell(330, 235, 230, 197, 180, 360) + [(560, CC)])
g('V', 630, [(50, CC), (315, B), (580, CC)])
g('W', 910, [(50, CC), (235, B), (455, 500), (675, B), (860, CC)])
g('X', 610, [(60, CC), (550, B)], [(60, B), (550, CC)])
g('Y', 590, [(50, CC), (295, 350), (540, CC)], [(295, 350), (295, B)])
g('Z', 570, [(60, CC), (510, CC), (60, B), (510, B)])

# ---- lowercase ----------------------------------------------------------
BOWL = full(270, 250, 170, 212)
g('a', 540, full(270, 250, 170, 212), [(440, XC), (440, B)])
g('b', 560, [(100, AC), (100, B)], full(270, 250, 170, 212))
g('c', 490, ell(280, 250, 170, 212, 40, 320))
g('d', 560, [(460, AC), (460, B)], full(290, 250, 170, 212))
g('e', 540, [(100, 250), (440, 250)] + ell(270, 250, 170, 212, 0, 322))
g('f', 330, [(150, B), (150, 560)] + ell(270, 560, 120, 142, 180, 90) + [(320, AC)], [(55, XC), (285, XC)])
g('g', 560, full(280, 250, 170, 212), [(450, XC), (450, -20)] + ell(280, -20, 170, 132, 0, -172))
g('h', 540, [(100, AC), (100, B)], [(100, 300)] + ell(270, 310, 170, 152, 180, 0) + [(440, B)])
g('i', 240, [(120, B), (120, XC)], dot(120, 645))
g('j', 300, [(215, XC), (215, -40)] + ell(95, -40, 120, 112, 0, -115), dot(215, 645))
g('k', 500, [(100, AC), (100, B)], [(420, XC), (110, 205)], [(225, 285), (440, B)])
g('l', 240, [(120, AC), (120, B)])
g('m', 800, [(100, XC), (100, B)], [(100, 310)] + ell(255, 310, 155, 152, 180, 0) + [(410, B)],
  [(410, 310)] + ell(565, 310, 155, 152, 180, 0) + [(720, B)])
g('n', 540, [(100, XC), (100, B)], [(100, 310)] + ell(270, 310, 170, 152, 180, 0) + [(440, B)])
g('o', 550, full(275, 250, 175, 212))
g('p', 560, [(100, XC), (100, -152)], full(270, 250, 170, 212))
g('q', 560, [(460, XC), (460, -152)], full(290, 250, 170, 212))
g('r', 350, [(100, XC), (100, B)], [(100, 330)] + ell(250, 310, 150, 152, 180, 62))
g('s', 460, ell(230, 356, 125, 106, 30, 270) + ell(230, 144, 142, 106, 90, -152))
g('t', 340, [(150, 640), (150, 150)] + ell(260, 150, 110, 112, 180, 270), [(55, XC), (300, XC)])
g('u', 540, [(100, XC), (100, 190)] + ell(270, 190, 170, 152, 180, 360) + [(440, XC)], [(440, XC), (440, B)])
g('v', 490, [(50, XC), (245, B), (440, XC)])
g('w', 760, [(50, XC), (215, B), (380, XC), (545, B), (710, XC)])
g('x', 470, [(50, XC), (420, B)], [(50, B), (420, XC)])
g('y', 490, [(50, XC), (242, B)], [(440, XC), (150, -152)])
g('z', 450, [(60, XC), (390, XC), (60, B), (390, B)])

# ---- digits (tabular 580) ------------------------------------------------
g('0', 580, full(290, 350, 195, 312))
g('1', 580, [(135, 530), (300, CC), (300, B)])
g('2', 580, ell(290, 490, 190, 172, 158, -32) + [(100, B), (500, B)])
g('3', 580, ell(290, 510, 175, 152, 152, -90) + ell(285, 195, 195, 157, 90, -152))
g('4', 580, [(410, B), (410, CC), (70, 230), (520, 230)])
g('5', 580, [(470, CC), (170, CC), (140, 372)] + ell(285, 215, 190, 177, 118, -150))
g('6', 580, full(290, 220, 190, 182), ell(360, 380, 260, 282, 62, 180) + [(100, 220)])
g('7', 580, [(95, CC), (490, CC), (230, B)])
g('8', 580, full(290, 505, 163, 157), full(290, 196, 190, 158))
g('9', 580, rot180([full(290, 220, 190, 182), ell(360, 380, 260, 282, 62, 180) + [(100, 220)]][0:1] +
                  [ell(360, 380, 260, 282, 62, 180) + [(100, 220)]], 290, 350)[0:2])

# ---- punctuation --------------------------------------------------------
g(' ', 280)
g('!', 280, [(140, CC), (140, 230)], dot(140, B + 12))
g('"', 400, [(120, CC), (120, 500)], [(280, CC), (280, 500)])
g('#', 640, [(240, 680), (180, 20)], [(430, 680), (370, 20)], [(80, 470), (550, 470)], [(60, 230), (530, 230)])
g('$', 570, S_UP + S_LO, [(285, 730), (285, -30)])
g('%', 780, full(200, 510, 95, 115), full(580, 190, 95, 115), [(590, CC), (190, B)])
g('&', 660, ell(250, 525, 118, 140, 40, 330) + [(520, 70)],
  ell(262, 215, 192, 177, 125, 360) + [(560, 60)], [(560, 60), (620, 120)])
g("'", 240, [(120, CC), (120, 500)])
g('(', 340, ell(360, 280, 220, 470, 128, 232))
g(')', 340, ell(-20, 280, 220, 470, -52, 52))
g('*', 440, [(220, 640), (220, 360)], [(100, 570), (340, 430)], [(100, 430), (340, 570)])
g('+', 580, [(290, 140), (290, 460)], [(130, 300), (450, 300)])
g(',', 250, dot(125, 40), [(125, 40), (95, -100)])
g('-', 380, [(80, 270), (300, 270)])
g('.', 250, dot(125, B))
g('/', 440, [(50, B), (390, CC)])
g(':', 250, dot(125, B), dot(125, 424))
g(';', 250, dot(125, 424), dot(125, 40), [(125, 40), (95, -100)])
g('<', 580, [(450, 520), (120, 300), (450, 80)])
g('=', 580, [(110, 390), (470, 390)], [(110, 210), (470, 210)])
g('>', 580, [(130, 520), (460, 300), (130, 80)])
g('?', 480, ell(240, 500, 155, 162, 165, -50) + [(240, 250), (240, 200)], dot(240, B + 12))
g('@', 900, full(450, 260, 390, 335), ell(450, 235, 112, 135, 0, 360), [(562, 360), (562, 175), (640, 175)])
g('[', 340, [(250, AC), (130, AC), (130, -100), (250, -100)])
g('\\', 440, [(50, CC), (390, B)])
g(']', 340, [(90, AC), (210, AC), (210, -100), (90, -100)])
g('^', 580, [(110, 380), (290, 640), (470, 380)])
g('_', 580, [(30, -70), (550, -70)])
g('`', 320, [(100, 700), (200, 590)])
g('{', 350, [(260, AC), (200, 690), (180, 640), (180, 420), (130, 350), (180, 280), (180, 70), (200, 10), (260, -2)])
g('|', 250, [(125, -100), (125, AC)])
g('}', 350, flip_x([[(260, AC), (200, 690), (180, 640), (180, 420), (130, 350), (180, 280), (180, 70), (200, 10), (260, -2)]], 350)[0])
g('~', 580, [(80 + i * 20, 340 + 52 * math.sin(math.pi + i / 20 * 2 * math.pi)) for i in range(21)])

# '9' above uses rot180 of the '6' strokes
_six = G['6'][1]
G['9'] = (580, rot180(_six, 290, 350))


def mono_variant(ch):
    """Fixed-pitch version of a glyph (advance 600): centred, wide glyphs squeezed,
    narrow glyphs given slab feet so i/l/1/I/J stay distinguishable in a terminal."""
    adv, lines = G[ch]
    scale = min(1.0, 548.0 / adv) if adv > 560 else 1.0
    out = []
    for l in lines:
        out.append([(x * scale, y) for x, y in l])
    width = adv * scale
    off = (MONO_ADVANCE - width) / 2.0
    out = [[(x + off, y) for x, y in l] for l in out]
    cx = MONO_ADVANCE / 2.0
    extra = {
        'i': [[(cx - 90, XC), (cx, XC)], [(cx - 110, B), (cx + 110, B)]],
        'l': [[(cx - 90, AC), (cx, AC)], [(cx - 110, B), (cx + 110, B)]],
        'j': [[(cx - 40, XC), (cx + 80, XC)]],
        'I': [[(cx - 120, CC), (cx + 120, CC)], [(cx - 120, B), (cx + 120, B)]],
        '1': [[(cx - 130, B), (cx + 130, B)]],
        'J': [[(cx - 20, CC), (cx + 150, CC)]],
        '0': [[(cx, 340), (cx, 350)]],
        'r': [[(cx - 130, B), (cx + 20, B)]],
    }.get(ch, [])
    return MONO_ADVANCE, out + extra


# ------------------------------------------------------------------------
def emit(path):
    chars = [chr(c) for c in range(32, 127)]
    seg_lines = []
    tables = {}
    for name, fn in (("prop", lambda c: G[c]), ("mono", mono_variant)):
        glyphs = []
        for ch in chars:
            adv, lines = fn(ch)
            first = len(seg_lines)
            count = 0
            for l in lines:
                for (x0, y0), (x1, y1) in zip(l, l[1:]):
                    seg_lines.append("{%d,%d,%d,%d}" % (round(x0), round(y0), round(x1), round(y1)))
                    count += 1
            glyphs.append("{%d,%d,%d}" % (first, count, adv))
        tables[name] = glyphs
    with open(path, "w") as f:
        f.write("/* GENERATED by tools/genfont.py - do not edit. SAND Sans outlines. */\n")
        f.write('#include "sand/graphics/font.h"\n')
        f.write("const struct sand_font_seg sand_font_segments[%d] = {\n" % len(seg_lines))
        for i in range(0, len(seg_lines), 8):
            f.write("    " + ",".join(seg_lines[i:i + 8]) + ",\n")
        f.write("};\nconst uint32_t sand_font_segment_count = %d;\n" % len(seg_lines))
        for name in ("prop", "mono"):
            f.write("const struct sand_font_glyph sand_font_%s[95] = {\n" % name)
            for i in range(0, 95, 6):
                f.write("    " + ",".join(tables[name][i:i + 6]) + ",\n")
            f.write("};\n")
    print("wrote %s: %d segments" % (path, len(seg_lines)))


def preview(path):
    import numpy as np
    from PIL import Image
    chars = [chr(c) for c in range(33, 127)]

    def render(text, px, weight, mono=False, fg=(233, 234, 242), bg=(19, 23, 34), width=None):
        scale = px / 1000.0
        adv_total = sum((MONO_ADVANCE if mono else G[c][0]) for c in text) * scale + px
        H = int(px * 1.5) + 4
        W = int(adv_total) + 8
        img = np.zeros((H, W), np.float32)
        yy, xx = np.mgrid[0:H, 0:W]
        pxc = xx + 0.5
        pyc = yy + 0.5
        base = px * 1.05
        x = 4.0
        for ch in text:
            adv, lines = (mono_variant(ch) if mono else G[ch])
            for l in lines:
                for (x0, y0), (x1, y1) in zip(l, l[1:]):
                    ax, ay = x + x0 * scale, base - y0 * scale
                    bx, by = x + x1 * scale, base - y1 * scale
                    dx, dy = bx - ax, by - ay
                    den = dx * dx + dy * dy
                    t = np.clip(((pxc - ax) * dx + (pyc - ay) * dy) / den, 0, 1) if den > 0 else 0
                    d = np.hypot(pxc - (ax + dx * t), pyc - (ay + dy * t))
                    cov = np.clip(weight * scale / 2 - d + 0.5, 0, 1)
                    img = np.maximum(img, cov)
            x += adv * scale
        out = np.zeros((H, W, 3), np.float32)
        for c in range(3):
            out[..., c] = bg[c] + (fg[c] - bg[c]) * img
        return out.astype(np.uint8)

    rows = []
    sample = "".join(chars)
    for text in (sample[:47], sample[47:]):
        rows.append(render(text, 30, 76))
    for px, wt in ((13, 80), (15, 96), (11, 96)):
        rows.append(render("The quick brown fox jumps over the lazy dog 0123456789 SAND OS", px, wt))
        rows.append(render("Memory 14.9 MiB free  |  kernel heap 1.2 MiB  |  ready: serial0 kbd0 mouse0 ata0", px, wt))
    rows.append(render("sand > ls -l /home && run hello  # mono Il1 O0 {}[]()", 14, 80, mono=True))
    rows.append(render("34", 80, 52, fg=(169, 155, 255)))
    W = max(r.shape[1] for r in rows)
    canvas = np.zeros((sum(r.shape[0] for r in rows), W, 3), np.uint8)
    canvas[:] = (19, 23, 34)
    y = 0
    for r in rows:
        canvas[y:y + r.shape[0], :r.shape[1]] = r
        y += r.shape[0]
    Image.fromarray(canvas).resize((canvas.shape[1] * 2, canvas.shape[0] * 2), Image.NEAREST).save(path)
    print("wrote", path)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "emit":
        emit(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "preview":
        preview(sys.argv[2])
    else:
        print(__doc__)
