#include "sand/libsand.h"
/* KEYSTONE: system appearance settings. Unlike the browser preview, these change SAND OS itself:
 * the accent colour of the desktop, reduced motion and the menu-bar clock format. The kernel
 * validates every value and persists it to system.cfg. */
#define SW 400
#define SH 340
static int win;
static struct sand_draw cmds[SAND_WIN_MAX_DRAW];
static uint32_t ncmd;
static const uint32_t accents[SAND_ACCENT_COUNT] = { 0xF2C98Au, 0x83B7FFu, 0xA99BFFu, 0x74D6AAu, 0xE8836Bu };
static const char *const names[SAND_ACCENT_COUNT] = { "Champagne", "Glacier", "Violet", "Jade", "Ember" };

static void flush(void) { if (ncmd) { sand_win_draw(win, cmds, ncmd); ncmd = 0; } }
static struct sand_draw *next(uint8_t op) {
    if (ncmd == SAND_WIN_MAX_DRAW) flush();
    struct sand_draw *c = &cmds[ncmd++]; memset(c, 0, sizeof(*c)); c->op = op; return c;
}
static void rrect(int x, int y, int w, int h, int r, uint32_t c1, uint32_t c2, int alpha) {
    struct sand_draw *c = next(SAND_DRAW_RRECT); c->x = (int16_t)x; c->y = (int16_t)y; c->w = (int16_t)w; c->h = (int16_t)h; c->r = (int16_t)r;
    c->color = c1; c->color2 = c2; c->aux = c2 ? 1 : 0; c->alpha = (uint8_t)alpha;
}
static void circle(int x, int y, int r, uint32_t col, int alpha) {
    struct sand_draw *c = next(SAND_DRAW_CIRCLE); c->x = (int16_t)x; c->y = (int16_t)y; c->r = (int16_t)r; c->color = col; c->alpha = (uint8_t)alpha;
}
static void ring(int x, int y, int w, int h, int r, int thick, uint32_t col, int alpha) {
    struct sand_draw *c = next(SAND_DRAW_RING); c->x = (int16_t)x; c->y = (int16_t)y; c->w = (int16_t)w; c->h = (int16_t)h; c->r = (int16_t)r;
    c->aux = (int16_t)thick; c->color = col; c->alpha = (uint8_t)alpha;
}
static void text(int face, int align, int x, int y, uint32_t col, const char *s) {
    struct sand_draw *c = next(SAND_DRAW_TEXT); c->face = (uint8_t)face; c->align = (uint8_t)align; c->x = (int16_t)x; c->y = (int16_t)y; c->color = col;
    for (int i = 0; s[i] && i < 39; ++i) c->text[i] = s[i];
}
static int get(uint32_t key) { int v = sand_setting_get(key); return v < 0 ? 0 : v; }
static void set(uint32_t key, uint32_t value) { (void)sand_setting_set(key, value); }

/* layout shared by drawing and hit-testing */
#define SW_X(i) (52 + (i) * 74)
#define SW_Y 150
#define MOTION_Y 232
#define CLOCK_Y 282
static void toggle_switch(int x, int y, int on) {
    rrect(x, y, 46, 26, 13, on ? 0x74D6AAu : 0x3A3578u, on ? 0x3E8F78u : 0x25214Fu, 255);
    circle(on ? x + 33 : x + 13, y + 13, 9, 0xFFFFFFu, 255);
}
static void draw(void) {
    int accent = get(SAND_SET_ACCENT), motion = get(SAND_SET_REDUCED_MOTION), c24 = get(SAND_SET_CLOCK_24H);
    struct sand_draw *c = next(SAND_DRAW_CLEAR); c->color = 0x15122Fu;
    circle(SW - 40, 46, 22, accents[accent], 255);                        /* the sun that is the current accent */
    circle(SW - 34, 40, 14, 0xFFE9C8u, 150);
    text(SAND_FACE_LARGE, SAND_ALIGN_LEFT, 24, 52, 0xFFFFFFu, "KEYSTONE");
    text(SAND_FACE_SMALL, SAND_ALIGN_LEFT, 24, 74, 0x9A94B8u, "SYSTEM APPEARANCE");
    text(SAND_FACE_SMALL, SAND_ALIGN_LEFT, 24, 116, 0x9A94B8u, "ACCENT");
    for (int i = 0; i < (int)SAND_ACCENT_COUNT; ++i) {
        circle(SW_X(i), SW_Y, 17, accents[i], 255);
        circle(SW_X(i) - 5, SW_Y - 6, 7, 0xFFFFFFu, 60);
        if (i == accent) ring(SW_X(i) - 24, SW_Y - 24, 48, 48, 24, 2, 0xFFFFFFu, 230);
        text(SAND_FACE_SMALL, SAND_ALIGN_CENTER, SW_X(i), SW_Y + 40, i == accent ? 0xEDE9F7u : 0x9A94B8u, names[i]);
    }
    text(SAND_FACE_MEDIUM, SAND_ALIGN_LEFT, 24, MOTION_Y + 12, 0xEDE9F7u, "Reduced motion");
    text(SAND_FACE_SMALL, SAND_ALIGN_LEFT, 24, MOTION_Y + 28, 0x9A94B8u, "Skip window open and close animation");
    toggle_switch(SW - 70, MOTION_Y, motion);
    text(SAND_FACE_MEDIUM, SAND_ALIGN_LEFT, 24, CLOCK_Y + 12, 0xEDE9F7u, "24-hour clock");
    text(SAND_FACE_SMALL, SAND_ALIGN_LEFT, 24, CLOCK_Y + 28, 0x9A94B8u, "Menu-bar time format");
    toggle_switch(SW - 70, CLOCK_Y, c24);
    text(SAND_FACE_SMALL, SAND_ALIGN_CENTER, SW / 2, SH - 12, 0x6E6990u, "Changes apply to SAND OS now and are saved to system.cfg");
    flush();
}
static void click(int x, int y) {
    for (int i = 0; i < (int)SAND_ACCENT_COUNT; ++i) {
        int dx = x - SW_X(i), dy = y - SW_Y;
        if (dx * dx + dy * dy <= 26 * 26) { set(SAND_SET_ACCENT, (uint32_t)i); return; }
    }
    if (x >= SW - 76 && x < SW - 18 && y >= MOTION_Y - 6 && y < MOTION_Y + 32) set(SAND_SET_REDUCED_MOTION, (uint32_t)!get(SAND_SET_REDUCED_MOTION));
    else if (x >= SW - 76 && x < SW - 18 && y >= CLOCK_Y - 6 && y < CLOCK_Y + 32) set(SAND_SET_CLOCK_24H, (uint32_t)!get(SAND_SET_CLOCK_24H));
}
void _start(void) {
    win = sand_win_create("KEYSTONE", SW, SH);
    if (win < 0) { sand_puts("keystone: no window service available\n"); sand_exit(1); }
    draw();
    for (;;) {
        struct sand_event ev; int32_t got = sand_win_event(win, &ev);
        if (got < 0) break;
        if (got == 0) { sand_sleep_ms(15); continue; }
        if (ev.type == SAND_EV_KEY) {
            if (ev.key == 'q') break;
            if (ev.key >= '1' && ev.key < '1' + (int)SAND_ACCENT_COUNT) set(SAND_SET_ACCENT, ev.key - '1');
            else if (ev.key == 'm') set(SAND_SET_REDUCED_MOTION, (uint32_t)!get(SAND_SET_REDUCED_MOTION));
            else if (ev.key == 'c') set(SAND_SET_CLOCK_24H, (uint32_t)!get(SAND_SET_CLOCK_24H));
            else continue;
        } else if (ev.type == SAND_EV_MOUSE_DOWN) click(ev.x, ev.y);
        else continue;
        draw();
    }
    sand_win_close(win);
    sand_exit(0);
}
