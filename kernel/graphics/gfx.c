#include "sand/graphics/gfx.h"

void gfx_init(gfx_t *g, uint32_t *pixels, int32_t w, int32_t h) {
    g->px = pixels; g->w = w; g->h = h; g->stride = w;
    g->cx0 = 0; g->cy0 = 0; g->cx1 = w; g->cy1 = h;
}
void gfx_set_clip(gfx_t *g, int32_t x, int32_t y, int32_t w, int32_t h) {
    int32_t x1 = x + w, y1 = y + h;
    g->cx0 = x < 0 ? 0 : x; g->cy0 = y < 0 ? 0 : y;
    g->cx1 = x1 > g->w ? g->w : x1; g->cy1 = y1 > g->h ? g->h : y1;
}
int gfx_visible(const gfx_t *g, int32_t x, int32_t y, int32_t w, int32_t h) {
    return x < g->cx1 && y < g->cy1 && x + w > g->cx0 && y + h > g->cy0;
}
uint32_t gfx_mix(uint32_t d, uint32_t s, uint32_t a) {
    if (a >= 255u) return s;
    if (a == 0u) return d;
    uint32_t ia = 255u - a;
    uint32_t r = ((s >> 16) & 255u) * a + ((d >> 16) & 255u) * ia;
    uint32_t gg = ((s >> 8) & 255u) * a + ((d >> 8) & 255u) * ia;
    uint32_t b = (s & 255u) * a + (d & 255u) * ia;
    r = ((r + 128u) * 257u) >> 16; gg = ((gg + 128u) * 257u) >> 16; b = ((b + 128u) * 257u) >> 16;
    return (r << 16) | (gg << 8) | b;
}
static inline void blend_at(gfx_t *g, int32_t x, int32_t y, uint32_t color, uint32_t a) {
    if (x < g->cx0 || x >= g->cx1 || y < g->cy0 || y >= g->cy1 || a == 0u) return;
    uint32_t *p = g->px + y * g->stride + x;
    *p = gfx_mix(*p, color, a);
}

void gfx_rect(gfx_t *g, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color, uint32_t alpha) {
    int32_t x0 = x < g->cx0 ? g->cx0 : x, y0 = y < g->cy0 ? g->cy0 : y;
    int32_t x1 = x + w > g->cx1 ? g->cx1 : x + w, y1 = y + h > g->cy1 ? g->cy1 : y + h;
    if (x0 >= x1 || y0 >= y1 || alpha == 0u) return;
    for (int32_t yy = y0; yy < y1; ++yy) {
        uint32_t *row = g->px + yy * g->stride;
        if (alpha >= 255u) for (int32_t xx = x0; xx < x1; ++xx) row[xx] = color;
        else for (int32_t xx = x0; xx < x1; ++xx) row[xx] = gfx_mix(row[xx], color, alpha);
    }
}
void gfx_vgradient(gfx_t *g, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t top, uint32_t bottom, uint32_t alpha) {
    if (h <= 0) return;
    int32_t y0 = y < g->cy0 ? g->cy0 : y, y1 = y + h > g->cy1 ? g->cy1 : y + h;
    for (int32_t yy = y0; yy < y1; ++yy) {
        uint32_t t = (uint32_t)((yy - y) * 255 / (h > 1 ? h - 1 : 1));
        gfx_rect(g, x, yy, w, 1, gfx_mix(top, bottom, t), alpha);
    }
}

/* Coverage (0..255) of a rounded rectangle at a pixel centre; all inputs in 1/16 px. */
static uint32_t rr_cov(int32_t px, int32_t py, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r) {
    int32_t qx = px - (x + w / 2), qy = py - (y + h / 2);
    if (qx < 0) qx = -qx;
    if (qy < 0) qy = -qy;
    qx -= w / 2 - r; qy -= h / 2 - r;
    int32_t d;
    if (qx > 0 && qy > 0) d = (int32_t)sand_isqrt((uint32_t)(qx * qx + qy * qy));
    else d = qx > qy ? qx : qy;
    d -= r;
    int32_t c = (8 - d) * 255 / 16;
    return c < 0 ? 0u : (c > 255 ? 255u : (uint32_t)c);
}
static int32_t clamp_radius(int32_t r, int32_t w, int32_t h) {
    int32_t m = (w < h ? w : h) / 2;
    return r > m ? m : (r < 0 ? 0 : r);
}

static void rrect_impl(gfx_t *g, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r,
                       uint32_t top, uint32_t bottom, uint32_t alpha) {
    if (w <= 0 || h <= 0 || alpha == 0u) return;
    r = clamp_radius(r, w, h);
    int32_t y0 = y < g->cy0 ? g->cy0 : y, y1 = y + h > g->cy1 ? g->cy1 : y + h;
    for (int32_t yy = y0; yy < y1; ++yy) {
        uint32_t color = top == bottom ? top : gfx_mix(top, bottom, (uint32_t)((yy - y) * 255 / (h > 1 ? h - 1 : 1)));
        int32_t row = yy - y;
        if (r > 0 && (row < r || row >= h - r)) {
            for (int32_t i = 0; i < r; ++i) {
                uint32_t c = rr_cov((i * 16) + 8, row * 16 + 8, 0, 0, w * 16, h * 16, r * 16);
                if (c == 0u) continue;
                uint32_t a = c * alpha / 255u;
                blend_at(g, x + i, yy, color, a);
                blend_at(g, x + w - 1 - i, yy, color, a);
            }
            gfx_rect(g, x + r, yy, w - 2 * r, 1, color, alpha);
        } else {
            gfx_rect(g, x, yy, w, 1, color, alpha);
        }
    }
}
void gfx_rrect(gfx_t *g, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color, uint32_t alpha) {
    rrect_impl(g, x, y, w, h, r, color, color, alpha);
}
void gfx_rrect_gradient(gfx_t *g, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t top, uint32_t bottom, uint32_t alpha) {
    rrect_impl(g, x, y, w, h, r, top, bottom, alpha);
}
void gfx_circle(gfx_t *g, int32_t cx, int32_t cy, int32_t r, uint32_t color, uint32_t alpha) {
    rrect_impl(g, cx - r, cy - r, r * 2, r * 2, r, color, color, alpha);
}

void gfx_rrect_ring(gfx_t *g, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, int32_t t, uint32_t color, uint32_t alpha) {
    if (w <= 0 || h <= 0 || t <= 0 || alpha == 0u) return;
    r = clamp_radius(r, w, h);
    int32_t y0 = y < g->cy0 ? g->cy0 : y, y1 = y + h > g->cy1 ? g->cy1 : y + h;
    int32_t ri = r > t ? r - t : 0;
    for (int32_t yy = y0; yy < y1; ++yy) {
        int32_t row = yy - y;
        int32_t band = (row < r + 1 || row >= h - r - 1 || row < t + 1 || row >= h - t - 1);
        int32_t span = band ? w : (t + 1 < w ? t + 1 : w);
        for (int32_t i = 0; i < span; ++i) {
            for (int32_t side = 0; side < 2; ++side) {
                int32_t xx = side == 0 ? i : w - 1 - i;
                if (side == 1 && band) break;                  /* band rows already sweep full width on side 0 */
                uint32_t o = rr_cov(xx * 16 + 8, row * 16 + 8, 0, 0, w * 16, h * 16, r * 16);
                if (o == 0u) continue;
                uint32_t in = (w > 2 * t && h > 2 * t)
                    ? rr_cov(xx * 16 + 8, row * 16 + 8, t * 16, t * 16, (w - 2 * t) * 16, (h - 2 * t) * 16, ri * 16) : 0u;
                uint32_t c = o > in ? o - in : 0u;
                if (c) blend_at(g, x + xx, yy, color, c * alpha / 255u);
            }
        }
    }
}

void gfx_line(gfx_t *g, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t width16, uint32_t color, uint32_t alpha) {
    int32_t half = width16 / 2;
    int32_t minx = (x0 < x1 ? x0 : x1) - half / 16 - 2, maxx = (x0 > x1 ? x0 : x1) + half / 16 + 2;
    int32_t miny = (y0 < y1 ? y0 : y1) - half / 16 - 2, maxy = (y0 > y1 ? y0 : y1) + half / 16 + 2;
    if (minx < g->cx0) minx = g->cx0;
    if (miny < g->cy0) miny = g->cy0;
    if (maxx > g->cx1 - 1) maxx = g->cx1 - 1;
    if (maxy > g->cy1 - 1) maxy = g->cy1 - 1;
    for (int32_t yy = miny; yy <= maxy; ++yy) for (int32_t xx = minx; xx <= maxx; ++xx) {
        int32_t d = sand_dist_segment(xx * 16 + 8, yy * 16 + 8, x0 * 16 + 8, y0 * 16 + 8, x1 * 16 + 8, y1 * 16 + 8);
        int32_t c = (half - d + 8) * 255 / 16;
        if (c > 0) blend_at(g, xx, yy, color, (uint32_t)(c > 255 ? 255 : c) * alpha / 255u);
    }
}

void gfx_shadow(gfx_t *g, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, int32_t blur, int32_t oy, uint32_t color, uint32_t alpha) {
    r = clamp_radius(r, w, h);
    int32_t sy = y + oy;
    int32_t X0 = x - blur, Y0 = sy - blur, X1 = x + w + blur, Y1 = sy + h + blur;
    if (X0 < g->cx0) X0 = g->cx0;
    if (Y0 < g->cy0) Y0 = g->cy0;
    if (X1 > g->cx1) X1 = g->cx1;
    if (Y1 > g->cy1) Y1 = g->cy1;
    for (int32_t yy = Y0; yy < Y1; ++yy) {
        /* The window itself overdraws the interior: skip the span that is certainly covered. */
        int32_t skip0 = X1, skip1 = X1;
        if (yy >= y + r && yy < y + h - r) { skip0 = x + 2; skip1 = x + w - 2; }
        for (int32_t xx = X0; xx < X1; ++xx) {
            if (xx >= skip0 && xx < skip1) { xx = skip1 - 1; continue; }
            int32_t qx = (xx * 16 + 8) - (x * 16 + w * 8), qy = (yy * 16 + 8) - (sy * 16 + h * 8);
            if (qx < 0) qx = -qx;
            if (qy < 0) qy = -qy;
            qx -= (w / 2 - r) * 16; qy -= (h / 2 - r) * 16;
            int32_t d = (qx > 0 && qy > 0) ? (int32_t)sand_isqrt((uint32_t)(qx * qx + qy * qy)) : (qx > qy ? qx : qy);
            d -= r * 16;
            if (d >= blur * 16) continue;
            int32_t t = d <= 0 ? 0 : d * 256 / (blur * 16);          /* 0..255 */
            uint32_t s = (uint32_t)(256 - t);
            uint32_t a = (alpha * ((s * s * s) >> 8)) >> 16;         /* cubic falloff */
            if (a) blend_at(g, xx, yy, color, a);
        }
    }
}

void gfx_glyph(gfx_t *g, const sand_font_t *f, int32_t x, int32_t baseline, char ch, uint32_t color, uint32_t alpha) {
    unsigned char c = (unsigned char)ch;
    if (c < 32u || c >= 127u) return;
    const struct sand_glyph_bitmap *b = &f->glyph[c - 32u][0];
    if (b->bits == 0) return;
    int32_t bx = x + b->left, by = baseline - b->top;
    if (!gfx_visible(g, bx, by, b->width, b->height)) return;
    for (int32_t j = 0; j < b->height; ++j) {
        int32_t yy = by + j;
        if (yy < g->cy0 || yy >= g->cy1) continue;
        for (int32_t i = 0; i < b->width; ++i) {
            uint32_t cov = b->bits[j * b->width + i];
            if (cov) blend_at(g, bx + i, yy, color, cov * alpha / 255u);
        }
    }
}

int32_t gfx_text(gfx_t *g, const sand_font_t *f, int32_t x, int32_t baseline, const char *text, uint32_t color, uint32_t alpha, int32_t tracking64) {
    int32_t pen = x * 64;
    for (; *text; ++text) {
        unsigned char c = (unsigned char)*text;
        if (c < 32u || c >= 127u) continue;
        uint32_t idx = c - 32u;
        int32_t px = pen >> 6;
        uint32_t phase = ((uint32_t)(pen & 63) + 16u) >> 5;       /* nearest half-pixel */
        if (phase >= SAND_FONT_PHASES) { phase = 0u; ++px; }
        const struct sand_glyph_bitmap *b = &f->glyph[idx][phase];
        if (b->bits != 0) {
            int32_t bx = px + b->left, by = baseline - b->top;
            if (gfx_visible(g, bx, by, b->width, b->height)) {
                for (int32_t j = 0; j < b->height; ++j) {
                    int32_t yy = by + j;
                    if (yy < g->cy0 || yy >= g->cy1) continue;
                    const uint8_t *src = b->bits + j * b->width;
                    for (int32_t i = 0; i < b->width; ++i)
                        if (src[i]) blend_at(g, bx + i, yy, color, (uint32_t)src[i] * alpha / 255u);
                }
            }
        }
        pen += (int32_t)f->advance64[idx] + tracking64;
    }
    return (pen + 32) >> 6;
}
int32_t gfx_text_right(gfx_t *g, const sand_font_t *f, int32_t right, int32_t baseline, const char *text, uint32_t color, uint32_t alpha) {
    return gfx_text(g, f, right - (int32_t)font_text_width(f, text), baseline, text, color, alpha, 0);
}
int32_t gfx_text_center(gfx_t *g, const sand_font_t *f, int32_t cx, int32_t baseline, const char *text, uint32_t color, uint32_t alpha) {
    return gfx_text(g, f, cx - (int32_t)font_text_width(f, text) / 2, baseline, text, color, alpha, 0);
}
void gfx_copy(gfx_t *dst, const gfx_t *src, int32_t x, int32_t y, int32_t w, int32_t h) {
    int32_t x0 = x < dst->cx0 ? dst->cx0 : x, y0 = y < dst->cy0 ? dst->cy0 : y;
    int32_t x1 = x + w > dst->cx1 ? dst->cx1 : x + w, y1 = y + h > dst->cy1 ? dst->cy1 : y + h;
    for (int32_t yy = y0; yy < y1; ++yy)
        for (int32_t xx = x0; xx < x1; ++xx) dst->px[yy * dst->stride + xx] = src->px[yy * src->stride + xx];
}
