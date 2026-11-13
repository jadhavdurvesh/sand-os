#include "sand/libsand.h"
/* CHRONOS: stopwatch with laps on the SAND window service (kernel tick clock, 100 Hz). */
#define SW 320
#define SH 300
static int win;
static struct sand_draw cmds[SAND_WIN_MAX_DRAW];
static uint32_t ncmd;
static int running; static uint32_t base_ticks, elapsed_ticks, laps[4]; static int nlaps;

static void flush(void) { if (ncmd) { sand_win_draw(win, cmds, ncmd); ncmd = 0; } }
static struct sand_draw *next(uint8_t op) {
    if (ncmd == SAND_WIN_MAX_DRAW) flush();
    struct sand_draw *c = &cmds[ncmd++]; memset(c, 0, sizeof(*c)); c->op = op; return c;
}
static void rrect(int x, int y, int w, int h, int r, uint32_t c1, uint32_t c2) {
    struct sand_draw *c = next(SAND_DRAW_RRECT); c->x = (int16_t)x; c->y = (int16_t)y; c->w = (int16_t)w; c->h = (int16_t)h; c->r = (int16_t)r; c->color = c1; c->color2 = c2; c->aux = c2 ? 1 : 0;
}
static void text(int face, int align, int x, int y, uint32_t col, const char *s) {
    struct sand_draw *c = next(SAND_DRAW_TEXT); c->face = (uint8_t)face; c->align = (uint8_t)align; c->x = (int16_t)x; c->y = (int16_t)y; c->color = col;
    for (int i = 0; s[i] && i < 39; ++i) c->text[i] = s[i];
}
static char *two(char *p, uint32_t v) { *p++ = (char)('0' + (v / 10u) % 10u); *p++ = (char)('0' + v % 10u); return p; }
static void stamp(char *out, uint32_t t) {
    char *p = out; p = two(p, t / 6000u); *p++ = ':'; p = two(p, (t / 100u) % 60u); *p++ = '.'; p = two(p, t % 100u); *p = 0;
}
static uint32_t now_elapsed(void) { return running ? elapsed_ticks + (sand_ticks() - base_ticks) : elapsed_ticks; }
static void draw(void) {
    char b[24]; uint32_t t = now_elapsed();
    struct sand_draw *c = next(SAND_DRAW_CLEAR); c->color = 0x15122Fu;
    stamp(b, t); text(SAND_FACE_LARGE, SAND_ALIGN_CENTER, SW / 2, 78, running ? 0xFFFFFFu : 0xB9B3D6u, b);
    text(SAND_FACE_SMALL, SAND_ALIGN_CENTER, SW / 2, 100, 0x9A94B8u, running ? "RUNNING" : (t ? "PAUSED" : "READY"));
    /* a ring that sweeps once per minute */
    c = next(SAND_DRAW_RING); c->x = 20; c->y = 20; c->w = 280; c->h = 92; c->r = 24; c->aux = 1; c->color = 0xFFFFFFu; c->alpha = 24;
    uint32_t sec100 = t % 6000u; int fill = (int)(sec100 * 248u / 6000u);
    rrect(36, 116, 248, 4, 2, 0x0D0B21u, 0);
    if (fill > 4) rrect(36, 116, fill, 4, 2, 0xF2C98Au, 0xE8836Bu);
    rrect(20, 136, 136, 40, 12, running ? 0xF09C84u : 0x7FD6B0u, running ? 0xA8465Eu : 0x3E8F78u);
    text(SAND_FACE_MEDIUM, SAND_ALIGN_CENTER, 88, 162, running ? 0x2B0A14u : 0x07251Cu, running ? "Stop" : "Start");
    rrect(164, 136, 136, 40, 12, 0x3A3578u, 0x25214Fu);
    text(SAND_FACE_MEDIUM, SAND_ALIGN_CENTER, 232, 162, 0xEDE9F7u, running || !t ? "Lap" : "Reset");
    for (int i = 0; i < nlaps && i < 4; ++i) {
        char row[32], *p = row; *p++ = 'L'; *p++ = (char)('1' + i); *p = 0;
        text(SAND_FACE_SMALL, SAND_ALIGN_LEFT, 28, 208 + i * 20, 0x9A94B8u, row);
        stamp(b, laps[i]); text(SAND_FACE_MONO, SAND_ALIGN_RIGHT, 292, 208 + i * 20, 0xEDE9F7u, b);
    }
    flush();
}
static void toggle(void) {
    if (running) { elapsed_ticks = now_elapsed(); running = 0; } else { base_ticks = sand_ticks(); running = 1; }
}
static void secondary(void) {
    if (running) { if (nlaps < 4) laps[nlaps++] = now_elapsed(); }
    else { elapsed_ticks = 0; nlaps = 0; }
}
void _start(void) {
    win = sand_win_create("CHRONOS", SW, SH);
    if (win < 0) { sand_puts("chronos: no window service available\n"); sand_exit(1); }
    draw();
    uint32_t last = 0;
    for (;;) {
        struct sand_event ev; int32_t got = sand_win_event(win, &ev);
        if (got < 0) break;
        if (got == 0) { if (running && sand_ticks() - last >= 5u) { last = sand_ticks(); draw(); } sand_sleep_ms(10); continue; }
        if (ev.type == SAND_EV_KEY) {
            if (ev.key == 'q') break;
            if (ev.key == ' ' || ev.key == 's') toggle(); else if (ev.key == 'l' || ev.key == 'r') secondary(); else continue;
        } else if (ev.type == SAND_EV_MOUSE_DOWN && ev.y >= 136 && ev.y < 176) {
            if (ev.x >= 20 && ev.x < 156) toggle(); else if (ev.x >= 164 && ev.x < 300) secondary(); else continue;
        } else continue;
        draw();
    }
    sand_win_close(win);
    sand_exit(0);
}
