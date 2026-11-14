/* CAIRN: the SAND terminal as a real window. It runs the same command interpreter as
 * sandsh (apps/sandsh/shell_core.h) but renders into its own window through the window
 * service: a 60x20 character grid, truecolor SGR, block cursor and a one-line history. */
#include "sand/libsand.h"

#define COLS 60
#define ROWS 20
#define CW 8
#define CH 17
#define PAD_X 20
#define PAD_Y 20
#define SURF_W 520
#define SURF_H 380
#define BG 0x0E0C22u
#define FG 0xEDE9F7u

static int win;
static char cells[ROWS][COLS];
static uint32_t fgc[ROWS][COLS];
static int crow, ccol, esc_state, esc_n, esc_p[6];
static uint32_t cur_fg = FG, dirty_rows;       /* bit per row */
static struct sand_draw cmds[SAND_WIN_MAX_DRAW];
static uint32_t ncmd;

static void mark_row(int r) { dirty_rows |= 1u << r; }
static void scroll(void) {
    for (int r = 1; r < ROWS; ++r) for (int c = 0; c < COLS; ++c) { cells[r - 1][c] = cells[r][c]; fgc[r - 1][c] = fgc[r][c]; }
    for (int c = 0; c < COLS; ++c) { cells[ROWS - 1][c] = ' '; fgc[ROWS - 1][c] = FG; }
    dirty_rows = (1u << ROWS) - 1u;
}
static void newline(void) { mark_row(crow); ccol = 0; if (++crow >= ROWS) { crow = ROWS - 1; scroll(); } mark_row(crow); }
static void clear_all(void) {
    for (int r = 0; r < ROWS; ++r) for (int c = 0; c < COLS; ++c) { cells[r][c] = ' '; fgc[r][c] = FG; }
    crow = ccol = 0; dirty_rows = (1u << ROWS) - 1u;
}
static void sgr(void) {
    for (int i = 0; i < esc_n; ++i) {
        int p = esc_p[i];
        if (p == 0) cur_fg = FG;
        else if (p == 38 && i + 4 < esc_n && esc_p[i + 1] == 2) { cur_fg = ((uint32_t)esc_p[i + 2] << 16) | ((uint32_t)esc_p[i + 3] << 8) | (uint32_t)esc_p[i + 4]; i += 4; }
        else if (p >= 30 && p <= 37) { static const uint32_t pal[8] = { 0x1A1733u, 0xD98991u, 0x82C8ADu, 0xD7BD8Du, 0x8FA8FFu, 0xB99BFFu, 0x8FD6E0u, 0xEDE9F7u }; cur_fg = pal[p - 30]; }
    }
}
static void putc_term(char ch) {
    if (esc_state == 1) { esc_state = (ch == '[') ? 2 : 0; esc_n = 0; for (int i = 0; i < 6; ++i) esc_p[i] = 0; return; }
    if (esc_state == 2) {
        if (ch >= '0' && ch <= '9') { if (esc_n == 0) esc_n = 1; esc_p[esc_n - 1] = esc_p[esc_n - 1] * 10 + (ch - '0'); return; }
        if (ch == ';') { if (esc_n == 0) esc_n = 1; if (esc_n < 6) ++esc_n; return; }
        if (ch == 'm') { if (esc_n == 0) { esc_n = 1; esc_p[0] = 0; } sgr(); }
        else if (ch == 'J') clear_all();
        else if (ch == 'H') { mark_row(crow); crow = ccol = 0; mark_row(0); }
        esc_state = 0; return;
    }
    if (ch == 0x1b) { esc_state = 1; return; }
    if (ch == '\n') { newline(); return; }
    if (ch == '\r') { mark_row(crow); ccol = 0; return; }
    if (ch == '\b') { if (ccol > 0) { --ccol; mark_row(crow); } return; }
    if (ch < 0x20 || ch >= 0x7F) return;
    if (ccol >= COLS) newline();
    cells[crow][ccol] = ch; fgc[crow][ccol] = cur_fg; ++ccol; mark_row(crow);
}

static void flush(void) { if (ncmd) { sand_win_draw(win, cmds, ncmd); ncmd = 0; } }
static struct sand_draw *next(uint8_t op) {
    if (ncmd == SAND_WIN_MAX_DRAW) flush();
    struct sand_draw *c = &cmds[ncmd++]; memset(c, 0, sizeof(*c)); c->op = op; return c;
}
static void render(void) {
    for (int r = 0; r < ROWS; ++r) {
        if (!(dirty_rows & (1u << r))) continue;
        int y = PAD_Y + r * CH - 13;                           /* text y is the baseline */
        struct sand_draw *c = next(SAND_DRAW_RECT); c->x = 0; c->y = (int16_t)(y - 2); c->w = SURF_W; c->h = CH; c->color = BG;
        if (r == crow) {
            c = next(SAND_DRAW_RRECT); c->x = (int16_t)(PAD_X + ccol * CW); c->y = (int16_t)(y); c->w = CW; c->h = (int16_t)(CH - 3); c->r = 2; c->color = 0xF2C98Au; c->alpha = 200;
        }
        int col = 0;
        while (col < COLS) {
            while (col < COLS && cells[r][col] == ' ') ++col;
            if (col >= COLS) break;
            int start = col; uint32_t color = fgc[r][col]; int last = col;
            while (col < COLS && col - start < 38 && fgc[r][col] == color) { if (cells[r][col] != ' ') last = col; ++col; }
            c = next(SAND_DRAW_TEXT); c->face = SAND_FACE_MONO; c->x = (int16_t)(PAD_X + start * CW); c->y = (int16_t)(PAD_Y + r * CH); c->color = color;
            for (int i = start; i <= last; ++i) c->text[i - start] = cells[r][i];
        }
    }
    flush(); dirty_rows = 0;
}

static void sh_write(const char *s, uint32_t n) { for (uint32_t i = 0; i < n; ++i) putc_term(s[i]); }
static void sh_puts(const char *s) { while (*s) putc_term(*s++); }
static void sh_putu(uint32_t v) { char b[12]; sand_utoa(v, b); sh_puts(b); }
#include "../sandsh/shell_core.h"

void _start(void) {
    char line[LINE_MAX + 1], last[LINE_MAX + 1]; uint32_t len = 0, last_len = 0; last[0] = 0;
    win = sand_win_create("CAIRN", SURF_W, SURF_H);
    if (win < 0) { sand_puts("cairn: no window service available\n"); sand_exit(1); }
    struct sand_draw *c = next(SAND_DRAW_CLEAR); c->color = BG;
    clear_all();
    out("\n" C_ACCENT "CAIRN" C_RESET C_MUTED "  SAND terminal  -  type 'help'" C_RESET "\n");
    prompt(); render();
    for (;;) {
        struct sand_event ev; int32_t got = sand_win_event(win, &ev);
        if (got < 0) break;
        if (got == 0) { sand_sleep_ms(10); continue; }
        if (ev.type != SAND_EV_KEY) continue;
        uint32_t ch = ev.key;
        if (ch == '\n' || ch == '\r') {
            out("\n"); line[len] = '\0';
            if (len) { memcpy(last, line, len + 1); last_len = len; }
            int quit = execute(line); len = 0;
            if (quit) break;
            prompt();
        } else if (ch == 0x08 || ch == 0x7F) { if (len) { --len; out("\b \b"); } }
        else if (ch == 0x15) { while (len) { --len; out("\b \b"); } }
        else if (ch == SAND_KEY_UP && last_len) {               /* recall the previous command */
            while (len) { --len; out("\b \b"); }
            memcpy(line, last, last_len + 1); len = last_len; sh_write(line, len);
        } else if (ch >= 0x20 && ch < 0x7F && len < LINE_MAX) { line[len++] = (char)ch; char s[2] = { (char)ch, 0 }; out(s); }
        render();
    }
    sand_win_close(win);
    sand_exit(0);
}
