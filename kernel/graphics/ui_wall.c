#include "sand/graphics/ui.h"

/* "Twilight Dunes": the SAND default wallpaper, painted procedurally in integer
 * arithmetic (no image assets). Sharp-crested dune ridges are |sin| harmonics. */
static int32_t sin4096(uint32_t t) {           /* period 1024, result -4096..4096 */
    t &= 1023u;
    int32_t neg = t >= 512u; int32_t x = (int32_t)(t & 511u);
    int32_t p = (x * (512 - x)) >> 4;          /* 0..4096 parabola */
    int32_t s = p + (225 * ((p * p) / 4096 - p)) / 1000;
    return neg ? -s : s;
}
static uint32_t hash2(uint32_t x, uint32_t y) {
    uint32_t h = x * 374761393u + y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static uint32_t lerp_rgb(uint32_t a, uint32_t b, int32_t t, int32_t max) {
    if (t <= 0) return a;
    if (t >= max) return b;
    int32_t r = (int32_t)((a >> 16) & 255u) + ((int32_t)((b >> 16) & 255u) - (int32_t)((a >> 16) & 255u)) * t / max;
    int32_t g = (int32_t)((a >> 8) & 255u) + ((int32_t)((b >> 8) & 255u) - (int32_t)((a >> 8) & 255u)) * t / max;
    int32_t bl = (int32_t)(a & 255u) + ((int32_t)(b & 255u) - (int32_t)(a & 255u)) * t / max;
    return GFX_RGB(r, g, bl);
}
struct ridge { int32_t base, a1, a2, f1, f2, p1, p2; uint32_t top, bottom, rim; };

void sand_ui_paint_wallpaper(gfx_t *g) {
    int32_t W = g->w, H = g->h;
    /* sky, scaled from a 768-line design to any height */
    static const uint32_t stop_c[5] = { 0x100E30u, 0x2E2060u, 0x7A4585u, 0xD97F78u, 0xF0B27Cu };
    static const int32_t stop_y[5] = { 0, 260, 400, 500, 560 };
    int32_t sx = W * 1000 / 1024, sy = H * 1000 / 768;
    for (int32_t y = 0; y < H; ++y) {
        int32_t d = y * 1000 / sy; int i = 0;
        while (i < 3 && d >= stop_y[i + 1]) ++i;
        uint32_t c = lerp_rgb(stop_c[i], stop_c[i + 1], d - stop_y[i], stop_y[i + 1] - stop_y[i]);
        for (int32_t x = 0; x < W; ++x) g->px[y * g->stride + x] = c;
    }
    /* stars */
    for (int32_t y = 0; y < H * 45 / 100; ++y) for (int32_t x = 0; x < W; ++x) {
        uint32_t h = hash2((uint32_t)x, (uint32_t)y);
        if ((h % 2600u) == 0u) {
            uint32_t a = 40u + ((h >> 8) % 140u) * (uint32_t)(H * 45 / 100 - y) / (uint32_t)(H * 45 / 100);
            g->px[y * g->stride + x] = gfx_mix(g->px[y * g->stride + x], 0xF4EEFFu, a);
        }
    }
    /* moon with a warm halo */
    int32_t mx = 720 * sx / 1000, my = 300 * sy / 1000, mr = 52 * sy / 1000, halo = 230 * sy / 1000;
    for (int32_t y = my - halo; y <= my + halo; ++y) {
        if (y < 0 || y >= H) continue;
        for (int32_t x = mx - halo; x <= mx + halo; ++x) {
            if (x < 0 || x >= W) continue;
            int32_t dx = x - mx, dy = y - my; uint32_t d2 = (uint32_t)(dx * dx + dy * dy);
            if (d2 >= (uint32_t)(halo * halo)) continue;
            uint32_t d = sand_isqrt(d2); uint32_t t = 256u - d * 256u / (uint32_t)halo;
            uint32_t a = (t * t >> 8) * 80u >> 8;
            g->px[y * g->stride + x] = gfx_mix(g->px[y * g->stride + x], 0xF2A27Au, a);
        }
    }
    gfx_circle(g, mx, my, mr, 0xF8E6C6u, 255u);
    gfx_circle(g, mx - mr / 3, my - mr / 4, mr * 2 / 3, 0xFFF4DEu, 90u);
    /* four dune ridges, far to near */
    struct ridge r[4] = {
        { 455, 46, 10, 8, 29, 120, 300, 0x7A4F92u, 0x4B3475u, 0xF6BE8Eu },
        { 520, 56, 12, 11, 31, 400, 90, 0x523A7Cu, 0x2F2357u, 0xE8A07Eu },
        { 590, 64, 12, 14, 37, 700, 510, 0x34295Fu, 0x1C1643u, 0xC9857Eu },
        { 676, 52, 10, 19, 43, 250, 820, 0x1F1946u, 0x0B0922u, 0x8F6A8Cu },
    };

    for (int k = 0; k < 4; ++k) {
        int32_t base = r[k].base * sy / 1000;
        for (int32_t x = 0; x < W; ++x) {
            uint32_t t1 = ((uint32_t)(x * 1000 / sx) * (uint32_t)r[k].f1 >> 4) + (uint32_t)r[k].p1;
            uint32_t t2 = ((uint32_t)(x * 1000 / sx) * (uint32_t)r[k].f2 >> 4) + (uint32_t)r[k].p2;
            int32_t s1 = sin4096(t1), s2 = sin4096(t2);
            int32_t abs1 = s1 < 0 ? -s1 : s1;
            int32_t y16 = (base + (r[k].a1 * sy / 1000) * abs1 / 4096) * 16 - (r[k].a2 * sy / 1000) * s2 / 256;
            int32_t top = y16 >> 4, frac = y16 & 15;
            for (int32_t y = top < 0 ? 0 : top; y < H; ++y) {
                int32_t depth = y - top;
                uint32_t c = lerp_rgb(r[k].top, r[k].bottom, depth, H - top);
                uint32_t a = 255u;
                if (depth == 0) a = (uint32_t)(16 - frac) * 16u - 1u;
                if (depth < 3) c = gfx_mix(c, r[k].rim, depth == 0 ? 230u : (depth == 1 ? 120u : 50u));
                g->px[y * g->stride + x] = gfx_mix(g->px[y * g->stride + x], c, a);
            }
        }
        /* light mist between layers */
        int32_t mist_y = base - 30 * sy / 1000;
        for (int32_t y = mist_y; y < base + 60 * sy / 1000; ++y) {
            if (y < 0 || y >= H) continue;
            int32_t d = y - mist_y; if (d > 90) continue;
            uint32_t a = 14u - (uint32_t)(d < 45 ? 45 - d : d - 45) * 14u / 45u;
            for (int32_t x = 0; x < W; ++x) g->px[y * g->stride + x] = gfx_mix(g->px[y * g->stride + x], 0xC48FA8u, a);
        }
    }
    /* film grain keeps the large gradients from banding */
    for (int32_t y = 0; y < H; ++y) for (int32_t x = 0; x < W; ++x) {
        uint32_t p = g->px[y * g->stride + x]; int32_t n = (int32_t)(hash2((uint32_t)x + 7u, (uint32_t)y + 13u) & 3u) - 1;
        int32_t rr = (int32_t)((p >> 16) & 255u) + n, gg = (int32_t)((p >> 8) & 255u) + n, bb = (int32_t)(p & 255u) + n;
        rr = rr < 0 ? 0 : rr > 255 ? 255 : rr; gg = gg < 0 ? 0 : gg > 255 ? 255 : gg; bb = bb < 0 ? 0 : bb > 255 ? 255 : bb;
        g->px[y * g->stride + x] = GFX_RGB(rr, gg, bb);
    }
}

int32_t sand_ui_sin(uint32_t t) { return sin4096(t); }
