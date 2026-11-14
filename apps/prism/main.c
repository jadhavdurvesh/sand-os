#include "sand/libsand.h"
/* PRISM: a resizable live light-field. It owns its pixel buffer (small, 4x upscaled by the
 * window service via sand_win_blit) and re-renders at the new size on SAND_EV_RESIZE. */
#define SC 4
#define BW (SAND_WIN_MAX_W / SC)
#define BH (SAND_WIN_MAX_H / SC)
static uint32_t buf[BW * BH];
static int win, cw = 420, ch = 300;

static int32_t wave(uint32_t t) {                  /* sine from a quarter-wave table, +-255 */
    static const uint8_t q[17] = { 0, 25, 50, 74, 98, 121, 142, 162, 181, 198, 213, 226, 237, 245, 251, 254, 255 };
    t &= 63u; uint32_t quad = t >> 4, i = t & 15u;
    uint32_t v = (quad & 1u) ? q[16 - i] : q[i];
    return (quad & 2u) ? -(int32_t)v : (int32_t)v;
}
static uint32_t mixc(uint32_t a, uint32_t b, uint32_t t) {
    uint32_t r = (((a >> 16) & 255u) * (255u - t) + ((b >> 16) & 255u) * t) / 255u;
    uint32_t g = (((a >> 8) & 255u) * (255u - t) + ((b >> 8) & 255u) * t) / 255u;
    uint32_t bl = ((a & 255u) * (255u - t) + (b & 255u) * t) / 255u;
    return (r << 16) | (g << 8) | bl;
}
static void render(uint32_t t) {
    int bw = cw / SC, bh = ch / SC;
    for (int y = 0; y < bh; ++y)
        for (int x = 0; x < bw; ++x) {
            int32_t v = wave((uint32_t)x * 2u + t) + wave((uint32_t)y * 3u + t * 2u) + wave((uint32_t)(x + y) + t * 3u);   /* -765..765 */
            uint32_t k = (uint32_t)((v + 765) * 255 / 1530);
            uint32_t col = k < 128u ? mixc(0x15122Fu, 0xA8465Eu, k * 2u) : mixc(0xA8465Eu, 0xF2C98Au, (k - 128u) * 2u);
            buf[y * bw + x] = col;
        }
    struct sand_blit b = { buf, 0, 0, bw, bh, SC };
    sand_win_blit(win, &b);
}
void _start(void) {
    win = sand_win_create("PRISM", cw, ch);
    if (win < 0) { sand_puts("prism: no window service available\n"); sand_exit(1); }
    sand_win_resizable(win, 240, 160);
    uint32_t last = 0;
    for (;;) {
        struct sand_event ev; int32_t got = sand_win_event(win, &ev);
        if (got < 0) break;
        if (got > 0) {
            if (ev.type == SAND_EV_KEY && ev.key == 'q') break;
            if (ev.type == SAND_EV_RESIZE) { cw = ev.x; ch = ev.y; render(sand_ticks() / 4u); last = sand_ticks(); }
            continue;
        }
        if (sand_ticks() - last >= 4u) { last = sand_ticks(); render(last / 4u); }
        sand_sleep_ms(10);
    }
    sand_win_close(win);
    sand_exit(0);
}
