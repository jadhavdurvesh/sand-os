#include "sand/libsand.h"
/* QUANTA: calculator on the SAND window service. Fixed-point (2 decimals) integer
 * arithmetic only: no floating point and no libgcc 64-bit division. */
#define SW 296
#define SH 380
#define LIMIT 9999999   /* 99999.99 */
static int win;
static struct sand_draw cmds[SAND_WIN_MAX_DRAW];
static uint32_t ncmd;

static int32_t acc, cur; static int pending_op, fresh = 1, decimals = -1, error;   /* decimals: -1 none, else digits after dot */

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
/* a*b/100 without 64-bit division; returns 0 and sets error on overflow */
static int32_t mulfix(int32_t a, int32_t b) {
    int neg = (a < 0) ^ (b < 0); uint32_t ua = (uint32_t)(a < 0 ? -a : a), ub = (uint32_t)(b < 0 ? -b : b), lo, hi;
    __asm__("mull %3" : "=a"(lo), "=d"(hi) : "0"(ua), "r"(ub) : "cc");
    if (hi >= 100u) { error = 1; return 0; }
    uint32_t q, rem; __asm__("divl %4" : "=a"(q), "=d"(rem) : "0"(lo), "1"(hi), "r"(100u) : "cc");
    if (rem >= 50u) ++q;
    if (q > LIMIT) { error = 1; return 0; }
    return neg ? -(int32_t)q : (int32_t)q;
}
static int32_t divfix(int32_t a, int32_t b) {
    if (b == 0) { error = 1; return 0; }
    int neg = (a < 0) ^ (b < 0); uint32_t ua = (uint32_t)(a < 0 ? -a : a) * 100u, ub = (uint32_t)(b < 0 ? -b : b);
    uint32_t q = (ua + ub / 2u) / ub;
    if (q > LIMIT) { error = 1; return 0; }
    return neg ? -(int32_t)q : (int32_t)q;
}
static void apply(void) {
    if (pending_op == '+') acc += cur; else if (pending_op == '-') acc -= cur;
    else if (pending_op == '*') acc = mulfix(acc, cur); else if (pending_op == '/') acc = divfix(acc, cur); else acc = cur;
    if (acc > LIMIT || acc < -LIMIT) { error = 1; acc = 0; }
}
static void press(int key) {
    if (error && key != 'C') return;
    if (key >= '0' && key <= '9') {
        if (fresh) { cur = 0; decimals = -1; fresh = 0; }
        int sign = cur < 0 ? -1 : 1; int32_t m = cur < 0 ? -cur : cur, d = key - '0';
        if (decimals < 0) { if (m / 100 < 99999) m = (m / 100 * 10 + d) * 100; }
        else if (decimals == 0) { m = m / 100 * 100 + d * 10; decimals = 1; }
        else if (decimals == 1) { m += d; decimals = 2; }
        cur = sign * m;
    } else if (key == '.') { if (fresh) { cur = 0; fresh = 0; } if (decimals < 0) decimals = 0; }
    else if (key == 'C') { acc = cur = 0; pending_op = 0; fresh = 1; decimals = -1; error = 0; }
    else if (key == 'N') cur = -cur;
    else if (key == 'B') {
        if (!fresh) {
            int sign = cur < 0 ? -1 : 1; int32_t m = cur < 0 ? -cur : cur;
            if (decimals == 2) { m = m / 10 * 10; decimals = 1; } else if (decimals == 1) { m = m / 100 * 100; decimals = 0; }
            else if (decimals == 0) decimals = -1; else m = m / 1000 * 100;
            cur = sign * m;
        }
    } else if (key == '+' || key == '-' || key == '*' || key == '/' || key == '=') {
        if (!fresh || pending_op == 0) { apply(); cur = acc; }
        pending_op = key == '=' ? 0 : key; fresh = 1; decimals = -1;
    }
}
static char *fmt(char *p, int32_t v, int entering_decimals) {
    if (v < 0) { *p++ = '-'; v = -v; }
    p = sand_utoa((uint32_t)(v / 100), p);
    int32_t f = v % 100;
    if (entering_decimals >= 0) {
        *p++ = '.';
        if (entering_decimals >= 1) *p++ = (char)('0' + f / 10);
        if (entering_decimals >= 2) *p++ = (char)('0' + f % 10);
    } else if (f) { *p++ = '.'; *p++ = (char)('0' + f / 10); if (f % 10) *p++ = (char)('0' + f % 10); }
    *p = 0; return p;
}
struct button { const char *label; int key, col, row, span; int kind; };   /* kind: 0 digit, 1 function, 2 operator, 3 equals */
static const struct button buttons[] = {
    {"C", 'C', 0, 0, 1, 1}, {"+/-", 'N', 1, 0, 1, 1}, {"DEL", 'B', 2, 0, 1, 1}, {"/", '/', 3, 0, 1, 2},
    {"7", '7', 0, 1, 1, 0}, {"8", '8', 1, 1, 1, 0}, {"9", '9', 2, 1, 1, 0}, {"x", '*', 3, 1, 1, 2},
    {"4", '4', 0, 2, 1, 0}, {"5", '5', 1, 2, 1, 0}, {"6", '6', 2, 2, 1, 0}, {"-", '-', 3, 2, 1, 2},
    {"1", '1', 0, 3, 1, 0}, {"2", '2', 1, 3, 1, 0}, {"3", '3', 2, 3, 1, 0}, {"+", '+', 3, 3, 1, 2},
    {"0", '0', 0, 4, 2, 0}, {".", '.', 2, 4, 1, 0}, {"=", '=', 3, 4, 1, 3},
};
#define NB ((int)(sizeof(buttons) / sizeof(buttons[0])))
#define BX(c) (12 + (c) * 68)
#define BY(r) (108 + (r) * 54)
static int active_key;
static void draw(void) {
    char b[32], *p;
    struct sand_draw *c = next(SAND_DRAW_CLEAR); c->color = 0x15122Fu;
    rrect(12, 12, 272, 84, 14, 0x0D0B21u, 0);
    if (pending_op && !error) { p = fmt(b, acc, -1); *p++ = ' '; *p++ = pending_op == '*' ? 'x' : (char)pending_op; *p = 0; text(SAND_FACE_SMALL, SAND_ALIGN_RIGHT, 270, 36, 0x9A94B8u, b); }
    if (error) text(SAND_FACE_LARGE, SAND_ALIGN_RIGHT, 270, 78, 0xFF7E73u, "Error");
    else { fmt(b, cur, fresh ? -1 : decimals); text(SAND_FACE_LARGE, SAND_ALIGN_RIGHT, 270, 78, 0xFFFFFFu, b); }
    for (int i = 0; i < NB; ++i) {
        const struct button *k = &buttons[i]; int x = BX(k->col), y = BY(k->row), w = 60 + (k->span - 1) * 68, on = active_key == k->key;
        uint32_t top, bot, fg = 0xEDE9F7u;
        if (k->kind == 2) { top = 0xF09C84u; bot = 0xA8465Eu; fg = 0x2B0A14u; }
        else if (k->kind == 3) { top = 0xFFE0AEu; bot = 0xD79A55u; fg = 0x35210Bu; }
        else if (k->kind == 1) { top = 0x3A3578u; bot = 0x25214Fu; }
        else { top = 0x2B2658u; bot = 0x1B1742u; }
        if (on) { top = 0xFFFFFFu; bot = 0xCFC8F0u; fg = 0x15122Fu; }
        rrect(x, y, w, 46, 12, top, bot);
        text(SAND_FACE_MEDIUM, SAND_ALIGN_CENTER, x + w / 2, y + 29, fg, k->label);
    }
    flush();
}
static int key_for_event(uint32_t k) {
    if (k >= '0' && k <= '9') return (int)k;
    switch (k) { case '.': case '+': case '-': case '*': case '/': return (int)k; case 'x': return '*'; case '=': case '\r': case '\n': return '='; case 'c': return 'C'; case '\b': case 0x7F: return 'B'; case 'n': return 'N'; default: return 0; }
}
void _start(void) {
    win = sand_win_create("QUANTA", SW, SH);
    if (win < 0) { sand_puts("quanta: no window service available\n"); sand_exit(1); }
    draw();
    for (;;) {
        struct sand_event ev; int32_t got = sand_win_event(win, &ev);
        if (got < 0) break;
        if (got == 0) { if (active_key) { static uint32_t until; if (!until) until = sand_ticks() + 12u; if ((int32_t)(sand_ticks() - until) >= 0) { active_key = 0; until = 0; draw(); } } sand_sleep_ms(15); continue; }
        if (ev.type == SAND_EV_KEY) {
            if (ev.key == 'q') break;
            int k = key_for_event(ev.key); if (!k) continue;
            press(k); active_key = k; draw();
        } else if (ev.type == SAND_EV_MOUSE_DOWN) {
            for (int i = 0; i < NB; ++i) {
                const struct button *b = &buttons[i]; int x = BX(b->col), y = BY(b->row), w = 60 + (b->span - 1) * 68;
                if (ev.x >= x && ev.x < x + w && ev.y >= y && ev.y < y + 46) { press(b->key); active_key = b->key; draw(); break; }
            }
        }
    }
    sand_win_close(win);
    sand_exit(0);
}
