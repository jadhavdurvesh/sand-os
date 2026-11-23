#include "sand/graphics/dawn.h"
#include "sand/graphics/ui.h"
#ifndef DAWN_HOST
#include "sand/mm/heap.h"
#include "sand/arch/x86/timer.h"
#endif

static int32_t lerp(int32_t a, int32_t b, uint32_t t256) { return a + (b - a) * (int32_t)t256 / 256; }
static uint32_t smooth(uint32_t t) { if (t > 256u) t = 256u; return (t * t * (768u - 2u * t)) / 65536u; }
void dawn_moon(int32_t w, int32_t h, uint32_t p, int32_t *cx, int32_t *cy) { *cx = w / 2; *cy = lerp(h * 84 / 100, h * 47 / 100, smooth(p)); }

void dawn_render(gfx_t *g, const sand_font_t *word, const sand_font_t *small, uint32_t p, uint32_t frame, const char *label) {
    const int32_t w = g->w, h = g->h;
    gfx_set_clip(g, 0, 0, w, h);
    /* sky: deepens at the top, warms toward the horizon as boot advances */
    uint32_t horizon = gfx_mix(0x3A2766u, 0xD97F78u, smooth(p));
    gfx_vgradient(g, 0, 0, w, h * 55 / 100, 0x0B0A24u, 0x2E2060u, 255u);
    gfx_vgradient(g, 0, h * 55 / 100, w, h - h * 55 / 100, 0x2E2060u, horizon, 255u);
    uint32_t seed = 0x5A4Eu;
    for (uint32_t i = 0; i < 70u; ++i) {                                       /* stars, twinkling */
        seed = seed * 1664525u + 1013904223u; int32_t x = (int32_t)((seed >> 8) % (uint32_t)w);
        seed = seed * 1664525u + 1013904223u; int32_t y = (int32_t)((seed >> 8) % (uint32_t)(h * 55 / 100));
        uint32_t a = 70u + (uint32_t)(sand_ui_sin(frame * 9u + i * 61u) + 4096) * 150u / 8192u;
        gfx_rect(g, x, y, (i % 7u) == 0u ? 2 : 1, (i % 7u) == 0u ? 2 : 1, 0xF4EEFFu, a);
    }
    int32_t mx, my; dawn_moon(w, h, p, &mx, &my);
    const int32_t mr = h / 16;
    for (int32_t k = 6; k >= 1; --k) gfx_circle(g, mx, my, mr + k * 14, 0xF2A27Au, 13u);                   /* glow */
    gfx_circle(g, mx, my, mr, 0xF8E6C6u, 255u);
    gfx_circle(g, mx - mr / 3, my - mr / 4, mr * 2 / 3, 0xFFF4DEu, 90u);
    static const struct { int32_t base_pct, amp_pct, period; uint32_t color, edge; } dune[3] = {
        { 71, 4, 900, 0x523A7Cu, 0xF6BE8Eu }, { 79, 5, 600, 0x2F2357u, 0xE8A07Eu }, { 89, 4, 420, 0x1C1643u, 0xC9857Eu } };
    for (int32_t d = 0; d < 3; ++d) {
        int32_t base = h * dune[d].base_pct / 100, amp = h * dune[d].amp_pct / 100;
        for (int32_t x = 0; x < w; ++x) {
            int32_t y = base + amp * sand_ui_sin((uint32_t)(x * 1024 / dune[d].period + d * 300)) / 4096;
            gfx_rect(g, x, y, 1, h - y, dune[d].color, 255u);
            gfx_rect(g, x, y, 1, 1, dune[d].edge, 170u);
        }
    }
    uint32_t wa = p * 2u > 255u ? 255u : p * 2u;                               /* wordmark fades in over the first half */
    gfx_text_center(g, word, w / 2, h * 30 / 100, "SAND", 0xF4EEFFu, wa);
    uint32_t ta = p > 128u ? (p - 128u) * 2u : 0u; if (ta > 255u) ta = 255u;
    gfx_text_center(g, small, w / 2, h * 30 / 100 + 34, "Written from the first byte.", 0xB8A9D6u, ta);
    int32_t bw = 220, bx = w / 2 - bw / 2, by = h * 93 / 100;                  /* progress bar */
    gfx_rrect(g, bx, by, bw, 4, 2, 0x2A2250u, 255u);
    if (p > 0u) gfx_rrect(g, bx, by, bw * (int32_t)(p > 256u ? 256u : p) / 256 < 4 ? 4 : bw * (int32_t)(p > 256u ? 256u : p) / 256, 4, 2, 0xF0B27Cu, 255u);
    if (label) gfx_text_center(g, small, w / 2, by + 24, label, 0x8F84B0u, 255u);
}

#ifndef DAWN_HOST
static sand_framebuffer_t fb;
static uint32_t *buf; static gfx_t g; static sand_font_t word_face, small_face;
static int active, taken; static uint32_t progress, frames;
static void *plain_alloc(uint32_t n) { return kmalloc(n); }
static void put_span(int32_t y, int32_t x0, int32_t x1, const uint32_t *src) {
    volatile uint8_t *dst = fb.pixels + (uint32_t)y * fb.pitch_bytes + (uint32_t)x0 * (fb.bits_per_pixel / 8u);
    for (int32_t x = x0; x < x1; ++x) {
        uint32_t c = src[x];
        if (fb.bits_per_pixel == 32u) { *(volatile uint32_t *)dst = c; dst += 4; }
        else { dst[0] = (uint8_t)c; dst[1] = (uint8_t)(c >> 8); dst[2] = (uint8_t)(c >> 16); dst += 3; }
    }
}
static void present_all(void) { for (int32_t y = 0; y < (int32_t)fb.height; ++y) put_span(y, 0, (int32_t)fb.width, buf + y * (int32_t)fb.width); }
static void wait_ticks(uint32_t n) {                                          /* bounded: if the timer is not running there is simply no delay */
    uint32_t flags; __asm__ volatile("pushf; pop %0" : "=r"(flags));
    if ((flags & 0x200u) == 0u) return;                                        /* interrupts off: the tick counter would not move */
    uint32_t t0 = timer_ticks();
    for (uint32_t spin = 0; spin < 3000000u && timer_ticks() - t0 < n; ++spin) __asm__ volatile("pause");
}
static void frame(const char *label) { dawn_render(&g, &word_face, &small_face, progress, frames++, label); present_all(); }
int dawn_begin(const sand_framebuffer_t *f) {
    if (f == 0 || f->width < 640u || f->height < 480u || (f->bits_per_pixel != 24u && f->bits_per_pixel != 32u)) return -1;
    fb = *f;
    buf = (uint32_t *)kmalloc(fb.width * fb.height * 4u);
    if (buf == 0 || font_face_init(&word_face, 46, 54, 0, plain_alloc) != 0 || font_face_init(&small_face, 13, 84, 0, plain_alloc) != 0) return -2;
    gfx_init(&g, buf, (int32_t)fb.width, (int32_t)fb.height);
    progress = 0u; frames = 0u; taken = 0; active = 1;
    frame("starting");
    return 0;
}
void dawn_stage(uint32_t stage, const char *label) {
    if (!active) return;
    uint32_t target = stage >= DAWN_STAGES ? 256u : stage * 256u / DAWN_STAGES;
    while (progress < target) { progress += 256u / (DAWN_STAGES * 8u) + 1u; if (progress > target) progress = target; frame(label); wait_ticks(1u); }
}
void *dawn_take_buffer(uint32_t bytes) {
    if (!active || taken || bytes != fb.width * fb.height * 4u) return 0;
    taken = 1; return buf;
}
static uint32_t isqrt(uint32_t v) { uint32_t r = 0, b = 1u << 30; while (b > v) b >>= 2; while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return r; }
void dawn_reveal(const uint32_t *desktop) {
    if (!active) return;
    int32_t w = (int32_t)fb.width, h = (int32_t)fb.height, mx, my;
    dawn_moon(w, h, 256u, &mx, &my);
    int32_t fx = mx > w / 2 ? mx : w - mx, fy = my > h / 2 ? my : h - my;
    uint32_t rmax = isqrt((uint32_t)(fx * fx + fy * fy)) + 2u, prev = 0u;
    for (uint32_t k = 1; k <= 18u; ++k) {
        uint32_t r = rmax * smooth(k * 256u / 18u) / 256u; if (k == 18u) r = rmax;
        for (int32_t y = my - (int32_t)r; y <= my + (int32_t)r; ++y) {
            if (y < 0 || y >= h) continue;
            int32_t dy = y - my; int64_t o2 = (int64_t)r * r - (int64_t)dy * dy; if (o2 < 0) continue;
            int32_t ox = (int32_t)isqrt((uint32_t)o2), ix = -1;
            int64_t i2 = (int64_t)prev * prev - (int64_t)dy * dy; if (prev > 0u && i2 >= 0) ix = (int32_t)isqrt((uint32_t)i2);
            const uint32_t *row = desktop + y * w;
            int32_t l0 = mx - ox < 0 ? 0 : mx - ox, r1 = mx + ox + 1 > w ? w : mx + ox + 1;
            if (ix < 0) { if (l0 < r1) put_span(y, l0, r1, row); }
            else {
                int32_t l1 = mx - ix < 0 ? 0 : mx - ix, r0 = mx + ix + 1 > w ? w : mx + ix + 1;
                if (l0 < l1) put_span(y, l0, l1, row);
                if (r0 < r1) put_span(y, r0, r1, row);
            }
        }
        prev = r; wait_ticks(1u);
    }
    active = 0;
}
int dawn_active(void) { return active; }
#endif
