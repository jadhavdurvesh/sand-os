#include "sand/libsand.h"
/* LOCUS: windowed file explorer for SANDFS, painted through the SAND window service. */
#define JADE 0x82C8ADu
#define CORAL 0xE8836Bu
#define TEXTC 0xEDE9F7u
#define MUTEC 0x9A94B8u
#define MAXF 32
#define ROW_H 30
#define ROW_Y 52
#define VIEW_ROWS 7
#define SW 520
#define SH 360

static struct sand_dirent files[MAXF];
static int count, sel, top, win;
static struct sand_draw cmds[SAND_WIN_MAX_DRAW];
static uint32_t ncmd;
static char preview[160];

static void flush(void) { if (ncmd) { sand_win_draw(win, cmds, ncmd); ncmd = 0; } }
static void cmd(uint8_t op, uint8_t face, uint8_t align, uint8_t alpha, int x, int y, int w, int h, int r, int aux, uint32_t c1, uint32_t c2, const char *text) {
    if (ncmd == SAND_WIN_MAX_DRAW) flush();
    struct sand_draw *c = &cmds[ncmd++];
    memset(c, 0, sizeof(*c));
    c->op = op; c->face = face; c->align = align; c->alpha = alpha; c->x = (int16_t)x; c->y = (int16_t)y; c->w = (int16_t)w; c->h = (int16_t)h;
    c->r = (int16_t)r; c->aux = (int16_t)aux; c->color = c1; c->color2 = c2;
    if (text) { uint32_t n = 0; while (text[n] && n < 39) { c->text[n] = text[n]; ++n; } }
}
static void rrect(int x, int y, int w, int h, int r, uint32_t col, int alpha) { cmd(SAND_DRAW_RRECT, 0, 0, (uint8_t)alpha, x, y, w, h, r, 0, col, 0, 0); }
static void text(int face, int align, int x, int y, uint32_t col, const char *s) { cmd(SAND_DRAW_TEXT, (uint8_t)face, (uint8_t)align, 0, x, y, 0, 0, 0, 0, col, 0, s); }

static void refresh(void) {
    count = 0;
    while (count < MAXF && sand_fs_list((uint32_t)count, &files[count]) == 0) ++count;
    if (sel >= count) sel = count ? count - 1 : 0;
    if (sel < top) top = sel;
    if (sel >= top + VIEW_ROWS) top = sel - VIEW_ROWS + 1;
    preview[0] = 0;
    if (count) { int32_t n = sand_fs_read(files[sel].name, preview, sizeof(preview) - 1); preview[n > 0 ? n : 0] = 0; }
}
static void draw(void) {
    char b[24], *p;
    cmd(SAND_DRAW_CLEAR, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x15122Fu, 0, 0);
    cmd(SAND_DRAW_RECT, 0, 0, 0, 0, 0, 140, SH, 0, 0, 0x100D27u, 0, 0);
    cmd(SAND_DRAW_RECT, 0, 0, 40, 139, 0, 1, SH, 0, 0, 0xFFFFFFu, 0, 0);
    text(SAND_FACE_MEDIUM, 0, 18, 34, JADE, "LOCUS");
    text(SAND_FACE_SMALL, 0, 18, 70, MUTEC, "VOLUME");
    rrect(10, 80, 120, 30, 9, 0x2A2560u, 255);
    cmd(SAND_DRAW_CIRCLE, 0, 0, 0, 27, 95, 0, 0, 4, 0, JADE, 0, 0);
    text(SAND_FACE_SANS, 0, 40, 100, TEXTC, "SANDFS");
    uint32_t total = 0; for (int i = 0; i < count; ++i) total += files[i].length;
    p = sand_utoa((uint32_t)count, b); p[0] = ' '; p[1] = 'f'; p[2] = 'i'; p[3] = 'l'; p[4] = 'e'; p[5] = count == 1 ? 0 : 's'; p[6] = 0;
    text(SAND_FACE_SMALL, 0, 18, 140, MUTEC, b);
    p = sand_utoa(total, b); p[0] = ' '; p[1] = 'B'; p[2] = 0;
    text(SAND_FACE_SMALL, 0, 18, 158, MUTEC, b);
    cmd(SAND_DRAW_RRECT, 0, 0, 0, 14, 266, 112, 28, 9, 1, 0x7FD6B0u, 0x3E8F78u, 0);
    text(SAND_FACE_MEDIUM, 1, 70, 285, 0x07251Cu, "New file");
    cmd(SAND_DRAW_RRECT, 0, 0, 0, 14, 302, 112, 28, 9, 1, 0xF09C84u, 0xA8465Eu, 0);
    text(SAND_FACE_MEDIUM, 1, 70, 321, 0x2B0A14u, "Delete");
    text(SAND_FACE_SMALL, 0, 166, 34, MUTEC, "NAME");
    text(SAND_FACE_SMALL, 2, 498, 34, MUTEC, "SIZE");
    cmd(SAND_DRAW_RECT, 0, 0, 40, 150, 42, 360, 1, 0, 0, 0xFFFFFFu, 0, 0);
    if (count == 0) text(SAND_FACE_SANS, 1, 330, 130, MUTEC, "No files yet. Choose New file.");
    for (int i = 0; i < VIEW_ROWS && top + i < count; ++i) {
        int idx = top + i, y = ROW_Y + i * ROW_H;
        if (idx == sel) { rrect(150, y, 360, 26, 8, 0x2D2766u, 255); rrect(150, y + 5, 3, 16, 1, JADE, 255); }
        rrect(164, y + 5, 12, 16, 3, JADE, idx == sel ? 255 : 170);
        text(SAND_FACE_SANS, 0, 188, y + 18, idx == sel ? 0xFFFFFFu : TEXTC, files[idx].name);
        p = sand_utoa(files[idx].length, b); p[0] = ' '; p[1] = 'B'; p[2] = 0;
        text(SAND_FACE_SMALL, 2, 498, y + 18, MUTEC, b);
    }
    cmd(SAND_DRAW_RECT, 0, 0, 40, 150, 270, 360, 1, 0, 0, 0xFFFFFFu, 0, 0);
    text(SAND_FACE_SMALL, 0, 166, 290, MUTEC, "PREVIEW");
    int line = 0, col = 0; char row[48];
    for (int i = 0; preview[i] && line < 3; ++i) {
        if (preview[i] == '\n' || col == 39) { row[col] = 0; text(SAND_FACE_MONO, 0, 166, 312 + line * 16, TEXTC, row); ++line; col = 0; if (preview[i] == '\n') continue; }
        if (preview[i] >= 0x20 && preview[i] < 0x7F) row[col++] = preview[i];
    }
    if (line < 3 && col) { row[col] = 0; text(SAND_FACE_MONO, 0, 166, 312 + line * 16, TEXTC, row); }
    flush();
}
static void select_to(int i) {
    if (i < 0 || i >= count) return;
    sel = i; if (sel < top) top = sel; if (sel >= top + VIEW_ROWS) top = sel - VIEW_ROWS + 1;
    refresh();
}
static void new_file(void) {
    char name[SAND_NAME_MAX]; struct sand_dirent probe;
    for (uint32_t n = 1; n < 100; ++n) {
        char *p = name; const char *s = "note-"; while (*s) *p++ = *s++;
        p = sand_utoa(n, p); s = ".txt"; while (*s) *p++ = *s++; *p = 0;
        int taken = 0; for (uint32_t i = 0; sand_fs_list(i, &probe) == 0; ++i) if (!strcmp(probe.name, name)) taken = 1;
        if (!taken) break;
    }
    static const char body[] = "Created in LOCUS.\nSand keeps its shape.";
    sand_fs_write(name, body, sizeof(body) - 1);
    refresh();
    for (int i = 0; i < count; ++i) if (!strcmp(files[i].name, name)) select_to(i);
}
static void delete_selected(void) { if (count) { sand_fs_remove(files[sel].name); refresh(); } }

void _start(void) {
    win = sand_win_create("LOCUS", SW, SH);
    if (win < 0) { sand_puts("locus: no window service available\n"); sand_exit(1); }
    refresh(); draw();
    uint32_t last_poll = sand_ticks();
    for (;;) {
        struct sand_event ev; int32_t got = sand_win_event(win, &ev);
        if (got < 0) break;
        if (got == 0) {
            if (sand_ticks() - last_poll > 100u) { last_poll = sand_ticks(); int before = count; refresh(); if (count != before) draw(); }
            sand_sleep_ms(15); continue;
        }
        int redraw = 1;
        if (ev.type == SAND_EV_KEY) {
            if (ev.key == 'j' || ev.key == SAND_KEY_DOWN) select_to(sel + 1);
            else if (ev.key == 'k' || ev.key == SAND_KEY_UP) select_to(sel - 1);
            else if (ev.key == 'd') delete_selected();
            else if (ev.key == 'n') new_file();
            else if (ev.key == 'q') break;
            else redraw = 0;
        } else if (ev.type == SAND_EV_MOUSE_DOWN) {
            if (ev.x >= 150 && ev.x < 510 && ev.y >= ROW_Y && ev.y < ROW_Y + VIEW_ROWS * ROW_H) select_to(top + (ev.y - ROW_Y) / ROW_H);
            else if (ev.x >= 14 && ev.x < 126 && ev.y >= 266 && ev.y < 294) new_file();
            else if (ev.x >= 14 && ev.x < 126 && ev.y >= 302 && ev.y < 330) delete_selected();
            else redraw = 0;
        } else redraw = 0;
        if (redraw) draw();
    }
    sand_win_close(win);
    sand_exit(0);
}
