#include "sand/graphics/font.h"

uint32_t sand_isqrt(uint32_t v) {
    uint32_t r = 0u, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit != 0u) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return r;
}

int32_t sand_dist_segment(int32_t px, int32_t py, int32_t ax, int32_t ay, int32_t bx, int32_t by) {
    int32_t abx = bx - ax, aby = by - ay, apx = px - ax, apy = py - ay;
    int32_t den = abx * abx + aby * aby;
    if (den == 0) return (int32_t)sand_isqrt((uint32_t)(apx * apx + apy * apy));
    int32_t num = apx * abx + apy * aby;
    if (num <= 0) return (int32_t)sand_isqrt((uint32_t)(apx * apx + apy * apy));
    if (num >= den) { int32_t dx = px - bx, dy = py - by; return (int32_t)sand_isqrt((uint32_t)(dx * dx + dy * dy)); }
    int32_t cross = apx * aby - apy * abx;
    if (cross < 0) cross = -cross;
    return cross / (int32_t)sand_isqrt((uint32_t)den);
}

#define CELL_MAX_W 72
#define CELL_MAX_H 96
static uint8_t cell[CELL_MAX_W * CELL_MAX_H];

/* Rasterise one glyph at one sub-pixel phase into the scratch cell, then copy
 * its tight bounding box into permanent storage. Units below are 1/64 pixel. */
static int raster_glyph(sand_font_t *f, uint32_t index, uint32_t phase, uint32_t cw, uint32_t ch,
                        int32_t margin, int32_t baseline, sand_alloc_fn alloc) {
    const struct sand_font_glyph *g = (f->mono ? sand_font_mono : sand_font_prop) + index;
    int32_t scale = (int32_t)f->px * 64;                      /* 1/64 px per 1000 units */
    int32_t half = (int32_t)(((uint32_t)f->weight * (uint32_t)f->px * 64u) / 2000u);
    int32_t ox = margin * 64 + (int32_t)phase * 32;
    int32_t oy = baseline * 64;
    for (uint32_t i = 0; i < cw * ch; ++i) cell[i] = 0u;
    for (uint32_t s = 0; s < g->count; ++s) {
        const struct sand_font_seg *seg = &sand_font_segments[g->first + s];
        int32_t ax = ox + (seg->x0 * scale) / 1000, ay = oy - (seg->y0 * scale) / 1000;
        int32_t bx = ox + (seg->x1 * scale) / 1000, by = oy - (seg->y1 * scale) / 1000;
        int32_t minx = (ax < bx ? ax : bx) - half - 64, maxx = (ax > bx ? ax : bx) + half + 64;
        int32_t miny = (ay < by ? ay : by) - half - 64, maxy = (ay > by ? ay : by) + half + 64;
        int32_t x0 = minx / 64, x1 = maxx / 64, y0 = miny / 64, y1 = maxy / 64;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 >= (int32_t)cw) x1 = (int32_t)cw - 1;
        if (y1 >= (int32_t)ch) y1 = (int32_t)ch - 1;
        for (int32_t y = y0; y <= y1; ++y) for (int32_t x = x0; x <= x1; ++x) {
            int32_t d = sand_dist_segment(x * 64 + 32, y * 64 + 32, ax, ay, bx, by);
            int32_t c = (half - d + 32) * 255 / 64;
            if (c <= 0) continue;
            if (c > 255) c = 255;
            if ((uint8_t)c > cell[y * (int32_t)cw + x]) cell[y * (int32_t)cw + x] = (uint8_t)c;
        }
    }
    int32_t bx0 = (int32_t)cw, bx1 = -1, by0 = (int32_t)ch, by1 = -1;
    for (int32_t y = 0; y < (int32_t)ch; ++y) for (int32_t x = 0; x < (int32_t)cw; ++x)
        if (cell[y * (int32_t)cw + x]) {
            if (x < bx0) bx0 = x;
            if (x > bx1) bx1 = x;
            if (y < by0) by0 = y;
            if (y > by1) by1 = y;
        }
    struct sand_glyph_bitmap *out = &f->glyph[index][phase];
    out->bits = 0; out->width = 0; out->height = 0; out->left = 0; out->top = 0;
    if (bx1 < 0) return 0;                                    /* blank glyph (space) */
    uint32_t w = (uint32_t)(bx1 - bx0 + 1), h = (uint32_t)(by1 - by0 + 1);
    uint8_t *bits = (uint8_t *)alloc(w * h);
    if (bits == 0) return -1;
    for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x) {
        uint32_t c = cell[(by0 + (int32_t)y) * (int32_t)cw + bx0 + (int32_t)x];
        c += (c * (255u - c)) >> 9;                           /* slight gamma lift: light-on-dark text stays crisp */
        bits[y * w + x] = (uint8_t)(c > 255u ? 255u : c);
    }
    out->bits = bits; out->width = (uint8_t)w; out->height = (uint8_t)h;
    out->left = (int8_t)(bx0 - margin); out->top = (int8_t)(baseline - by0);
    return 0;
}

int font_face_init(sand_font_t *f, uint32_t px, uint32_t weight, int mono, sand_alloc_fn alloc) {
    if (f == 0 || alloc == 0 || px < 6u || px > 48u) return -1;
    f->px = (uint8_t)px; f->weight = (uint16_t)weight; f->mono = mono ? 1u : 0u;
    int32_t margin = (int32_t)((weight * px) / 2000u) + 2;
    f->ascent = (int8_t)((px * 760u + 999u) / 1000u + 1u);
    f->descent = (int8_t)((px * 200u + 999u) / 1000u + 1u);
    f->line_height = (uint8_t)(px + px / 3u + 2u);
    const struct sand_font_glyph *table = mono ? sand_font_mono : sand_font_prop;
    uint32_t maxadv = 0;
    for (uint32_t i = 0; i < 95u; ++i) {
        f->advance64[i] = (uint16_t)((table[i].advance * px * 64u) / 1000u);
        if (table[i].advance > maxadv) maxadv = table[i].advance;
    }
    if (mono) {   /* fixed pitch is a whole number of pixels so terminal cells tile exactly */
        uint16_t cellw = (uint16_t)(((600u * px + 500u) / 1000u) * 64u);
        for (uint32_t i = 0; i < 95u; ++i) f->advance64[i] = cellw;
    }
    uint32_t cw = (maxadv * px + 999u) / 1000u + (uint32_t)margin * 2u + 2u;
    uint32_t ch = (uint32_t)(f->ascent + f->descent) + (uint32_t)margin + 2u;
    if (cw > CELL_MAX_W || ch > CELL_MAX_H) return -1;
    for (uint32_t i = 0; i < 95u; ++i)
        for (uint32_t p = 0; p < SAND_FONT_PHASES; ++p)
            if (raster_glyph(f, i, p, cw, ch, margin, (int32_t)f->ascent + margin / 2, alloc) != 0) return -1;
    return 0;
}

uint32_t font_text_width64(const sand_font_t *f, const char *text, int32_t tracking64) {
    int32_t w = 0;
    for (; *text; ++text) {
        unsigned char c = (unsigned char)*text;
        if (c >= 32u && c < 127u) w += (int32_t)f->advance64[c - 32u] + tracking64;
    }
    return w > 0 ? (uint32_t)w : 0u;
}
uint32_t font_text_width(const sand_font_t *f, const char *text) { return (font_text_width64(f, text, 0) + 32u) >> 6; }
