#include "sand/libsand.h"
/* LOCUS (native): file explorer for SANDFS. Console/ANSI front end; the windowed
 * LOCUS from the sand-apps preview will reuse this logic once a window service exists. */
#define JADE  "\x1b[38;2;130;200;173m"
#define MUTE  "\x1b[38;2;146;153;173m"
#define TEXT  "\x1b[38;2;233;234;242m"
#define BAD   "\x1b[38;2;217;137;145m"
#define BOLD  "\x1b[1m"
#define RST   "\x1b[0m"
#define MAXF 32
#define VIEW_ROWS 12

static struct sand_dirent files[MAXF];
static int count, sel, top;
static char preview[600];

static void out(const char *s) { sand_puts(s); }
static void at(int row, int col) {
    char b[24], *p = b; *p++ = 0x1b; *p++ = '[';
    p = sand_utoa((uint32_t)row, p); *p++ = ';'; p = sand_utoa((uint32_t)col, p); *p++ = 'H';
    sand_write(b, (uint32_t)(p - b));
}
static void refresh(void) {
    count = 0;
    while (count < MAXF && sand_fs_list((uint32_t)count, &files[count]) == 0) ++count;
    if (sel >= count) sel = count ? count - 1 : 0;
}
static void pad(const char *s, int width) {
    int n = (int)strlen(s); out(s);
    for (; n < width; ++n) out(" ");
}
static void draw(const char *status) {
    out("\x1b[2J\x1b[H");
    at(1, 2); out(JADE BOLD "LOCUS" RST MUTE "  file explorer  " RST);
    char b[16]; sand_utoa((uint32_t)count, b); out(MUTE); out(b); out(count == 1 ? " file" : " files"); out(RST);
    at(3, 2); out(MUTE "NAME                          SIZE" RST);
    if (sel < top) top = sel;
    if (sel >= top + VIEW_ROWS) top = sel - VIEW_ROWS + 1;
    if (count == 0) { at(5, 4); out(MUTE "No files yet. Press n to create one." RST); }
    for (int i = 0; i < VIEW_ROWS && top + i < count; ++i) {
        int idx = top + i; at(4 + i, 2);
        if (idx == sel) out(JADE BOLD "> "); else out("  ");
        out(idx == sel ? "" : TEXT);
        pad(files[idx].name, 28);
        char n[16]; sand_utoa(files[idx].length, n); out(n); out(" B" RST);
    }
    if (count > 0) {
        int32_t got = sand_fs_read(files[sel].name, preview, sizeof(preview) - 1);
        at(17, 2); out(MUTE "preview" RST);
        if (got < 0) got = 0;
        preview[got] = 0;
        int row = 18, col = 4; at(row, col); out(TEXT);
        for (int i = 0; i < got && row < 22; ++i) {
            char c = preview[i];
            if (c == '\n') { ++row; at(row, col); }
            else if (c >= 0x20 && c < 0x7F) sand_write(&c, 1);
        }
        out(RST);
    }
    at(23, 2); out(status ? status : MUTE "j/k move  enter refresh  n new  d delete  q quit" RST);
}
static int readline(char *buf, int cap) {
    int n = 0;
    for (;;) {
        char c; if (sand_read(&c, 1) != 1) continue;
        if (c == '\r' || c == '\n') { buf[n] = 0; return n; }
        if ((c == '\b' || c == 0x7F) && n > 0) { --n; out("\b \b"); }
        else if (c >= 0x20 && c < 0x7F && n < cap - 1) { buf[n++] = c; sand_write(&c, 1); }
    }
}
void _start(void) {
    refresh(); draw(0);
    for (;;) {
        char c; if (sand_read(&c, 1) != 1) continue;
        if (c == 'q') break;
        else if ((c == 'j' || c == 's') && sel + 1 < count) ++sel;
        else if ((c == 'k' || c == 'w') && sel > 0) --sel;
        else if (c == '\r' || c == '\n' || c == 'r') refresh();
        else if (c == 'd' && count > 0) {
            draw(BAD "delete this file? y/n" RST);
            char k; while (sand_read(&k, 1) != 1) { }
            if (k == 'y') { sand_fs_remove(files[sel].name); refresh(); }
        } else if (c == 'n') {
            char name[SAND_NAME_MAX], text[200];
            at(23, 2); out("\x1b[K" JADE "name: " RST); if (readline(name, sizeof(name)) == 0) { draw(0); continue; }
            at(23, 2); out("\x1b[K" JADE "text: " RST); int len = readline(text, sizeof(text));
            sand_fs_write(name, text, (uint32_t)len); refresh();
            for (int i = 0; i < count; ++i) if (!strcmp(files[i].name, name)) sel = i;
        }
        draw(0);
    }
    out("\x1b[2J\x1b[H");
    sand_exit(0);
}
