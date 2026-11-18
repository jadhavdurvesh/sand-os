#include "sand/libsand.h"
#include "citadel/citadel.h"
#include "citadel/sandos.h"
#include <string.h>
/* CITADEL: the offline command assistant, on the SAND OS window service.
 * The assistant logic (what a request means, what needs confirmation) is the Citadel core + SandOS adapter
 * (third_party/citadel); this file only supplies the window and the platform table that maps those requests to
 * SAND syscalls. Settings changes are asked back ("y/n") before they run. */
#define SW 520
#define SH 360
#define CW 8
#define CH 17
#define COLS 60
#define ROWS 12
#define PAD_X 16
#define TOP_Y 92
#define BG 0x15122Fu
#define C_TEXT 0xEDE9F7u
#define C_MUTED 0x9A94B8u
#define C_USER 0xC9B3FFu
#define C_WARN 0xE8B86Bu
#define INPUT_MAX 120

static int win;
static struct sand_draw cmds[SAND_WIN_MAX_DRAW];
static uint32_t ncmd;
static const uint32_t accents[SAND_ACCENT_COUNT] = { 0xF2C98Au, 0x83B7FFu, 0xA99BFFu, 0x74D6AAu, 0xE8836Bu };

/* ---- transcript ---- */
struct row { char text[COLS + 1]; uint32_t color; };
static struct row rows[ROWS];
static int nrows;
static int dirty = 1;
static void push_row(const char *s, int n, uint32_t color)
{
    if (nrows == ROWS) { memcpy(&rows[0], &rows[1], sizeof(rows[0]) * (ROWS - 1)); --nrows; }
    memset(&rows[nrows], 0, sizeof(rows[0])); memcpy(rows[nrows].text, s, (uint32_t)n); rows[nrows].color = color; ++nrows; dirty = 1;
}
static void say(const char *prefix, const char *s, uint32_t color)      /* word-wraps at COLS; continuation lines are indented */
{
    char line[COLS + 1]; int n = 0, plen = (int)strlen(prefix), first = 1;
    memcpy(line, prefix, (uint32_t)plen); n = plen;
    while (*s) {
        int len = 0; while (s[len] && s[len] != ' ') ++len;
        if (n + len > COLS && n > (first ? plen : 2)) { push_row(line, n, color); first = 0; line[0] = ' '; line[1] = ' '; n = 2; }
        for (int i = 0; i < len && n < COLS; ++i) line[n++] = s[i];
        s += len; while (*s == ' ') { if (n < COLS) line[n++] = ' '; ++s; }
    }
    while (n > 0 && line[n - 1] == ' ') --n;
    if (n > 0) push_row(line, n, color);
}

/* ---- platform table: Citadel requests -> SAND syscalls ---- */
#define APP_MAX 24
static char app_names[APP_MAX][16];
static uint32_t app_total;
static int32_t opened[8];
static const char *const hidden[] = { "sandsh", "abitest", "spin", "crash", "hello", "jail", "citadel", "locusc", "herald" };

static void scan_apps(void)
{
    struct sand_dirent e; app_total = 0;
    for (uint32_t i = 0; sand_app_list(i, &e) == 0 && app_total < APP_MAX; ++i) {
        int skip = 0; for (uint32_t h = 0; h < sizeof(hidden) / sizeof(hidden[0]); ++h) if (!strcmp(e.name, hidden[h])) skip = 1;
        if (skip) continue;
        memcpy(app_names[app_total], e.name, 15); app_names[app_total][15] = 0; ++app_total;
    }
}
static void reap(void)                                  /* collect finished apps we started so their slots are reusable */
{
    for (int i = 0; i < 8; ++i) {
        if (opened[i] <= 0) continue;
        struct sand_procinfo info; int found = 0;
        for (uint32_t n = 0; sand_proc_list(n, &info) == 0; ++n)
            if ((int32_t)info.pid == opened[i]) { found = 1; if (info.state == 3u) { int32_t st; (void)sand_wait(opened[i], &st); opened[i] = 0; } break; }
        if (!found) opened[i] = 0;
    }
}
static uint32_t p_count(void *c) { (void)c; return app_total; }
static int p_name(void *c, uint32_t i, char *out, size_t cap) { (void)c; if (i >= app_total || strlen(app_names[i]) + 1 > cap) return -1; memcpy(out, app_names[i], strlen(app_names[i]) + 1); return 0; }
static int p_launch(void *c, const char *app)
{
    (void)c; int known = 0, slot = -1;
    for (uint32_t i = 0; i < app_total; ++i) if (!strcmp(app, app_names[i])) known = 1;
    if (!known) return -1;                              /* only apps we list: never the shell, test or probe apps */
    reap(); for (int i = 0; i < 8; ++i) if (opened[i] <= 0) { slot = i; break; }
    if (slot < 0) return -1;
    int32_t pid = sand_spawn(app); if (pid < 0) return -1;
    opened[slot] = pid; return 0;
}
static int p_get(void *c, uint32_t k) { (void)c; return sand_setting_get(k); }
static int p_set(void *c, uint32_t k, uint32_t v) { (void)c; return sand_setting_set(k, v) == 0 ? 0 : -1; }
static int p_time(void *c, CitadelSandTime *t)
{
    struct sand_time n; (void)c; if (sand_time_get(&n) != 0) return -1;
    t->year = n.year; t->month = n.month; t->day = n.day; t->hour = n.hour; t->minute = n.minute; t->second = n.second; return 0;
}
static int p_info(void *c, CitadelSandInfo *o)
{
    struct sand_sysinfo s; (void)c; if (sand_sysinfo(&s) != 0 || s.tick_hz == 0) return -1;
    o->uptime_seconds = s.ticks / s.tick_hz; o->free_kib = s.free_frames * 4u; o->total_kib = s.total_frames * 4u; o->process_count = s.process_count; return 0;
}

/* notes: kept in notes.txt (one per line; the FS syscalls move at most 512 bytes) */
#define NOTE_MAX 8
#define NOTES_BYTES 500
static char notes[NOTE_MAX][CITADEL_SAND_NOTE_MAX + 1];
static uint32_t nnotes;
static void notes_load(void)
{
    char buf[513]; int32_t n = sand_fs_read("notes.txt", buf, 512); nnotes = 0;
    if (n <= 0) return;
    buf[n] = 0; char *p = buf;
    while (*p && nnotes < NOTE_MAX) {
        uint32_t len = 0; while (p[len] && p[len] != '\n') ++len;
        if (len > 0 && len <= CITADEL_SAND_NOTE_MAX) { memcpy(notes[nnotes], p, len); notes[nnotes][len] = 0; ++nnotes; }
        p += len; if (*p == '\n') ++p;
    }
}
static int notes_save(void)
{
    char buf[512]; uint32_t n = 0;
    for (uint32_t i = 0; i < nnotes; ++i) { uint32_t len = (uint32_t)strlen(notes[i]); memcpy(buf + n, notes[i], len); n += len; buf[n++] = '\n'; }
    if (n == 0) { (void)sand_fs_remove("notes.txt"); return 0; }
    return sand_fs_write("notes.txt", buf, n) == 0 ? 0 : -1;
}
static int p_note_add(void *c, const char *t)
{
    (void)c; uint32_t used = 0, len = (uint32_t)strlen(t);
    for (uint32_t i = 0; i < nnotes; ++i) used += (uint32_t)strlen(notes[i]) + 1u;
    if (nnotes >= NOTE_MAX || used + len + 1u > NOTES_BYTES) return -1;
    memcpy(notes[nnotes], t, len + 1u); ++nnotes;
    if (notes_save() != 0) { --nnotes; return -1; }
    dirty = 1; return 0;
}
static uint32_t p_note_count(void *c) { (void)c; return nnotes; }
static int p_note_get(void *c, uint32_t i, char *out, size_t cap) { (void)c; if (i >= nnotes || strlen(notes[i]) + 1 > cap) return -1; memcpy(out, notes[i], strlen(notes[i]) + 1); return 0; }
static int p_notes_clear(void *c) { (void)c; nnotes = 0; dirty = 1; return notes_save(); }

/* reminders: in memory only (they end with this window); due time is measured in kernel ticks */
#define REM_MAX 4
static struct { uint32_t due; char text[CITADEL_SAND_NOTE_MAX + 1]; } rem[REM_MAX];
static uint32_t nrem;
static uint32_t tick_hz = 100;
static int alert;
/* HERALD (the background service) keeps the timers alive after this window closes and raises the desktop toast. */
static int32_t ch_req = -1, ch_rep = -1;
static int herald_running(void)
{
    struct sand_procinfo info;
    for (uint32_t n = 0; sand_proc_list(n, &info) == 0; ++n)
        if (!strcmp(info.name, "herald") && info.state != 3u) return 1;
    return 0;
}
static int herald_ensure(void)
{
    if (ch_req < 1) return 0;
    if (herald_running()) return 1;
    if (sand_spawn("herald") < 0) return 0;
    sand_sleep_ms(150); return 1;
}
static void herald_send(const char *msg, uint32_t len) { if (ch_req >= 1) (void)sand_chan_send(ch_req, msg, len); }
static void herald_sync(void)                            /* re-learn reminders that outlived an earlier window */
{
    if (ch_req < 1 || ch_rep < 1 || !herald_running()) return;
    struct sand_chan_msg m; while (sand_chan_recv(ch_rep, &m) == 1) {}
    herald_send("Q", 1);
    for (int tries = 0; tries < 20; ++tries) {
        int done = 0;
        while (sand_chan_recv(ch_rep, &m) == 1) {
            if (m.data[0] == 'E') { done = 1; break; }
            if (m.data[0] != 'L' || nrem >= REM_MAX) continue;
            uint32_t left = 0, i = 1; while (i < m.length && m.data[i] >= '0' && m.data[i] <= '9') left = left * 10u + (uint32_t)(m.data[i++] - '0');
            if (i < m.length && m.data[i] == ' ') ++i;
            uint32_t tl = 0; while (i < m.length && tl < CITADEL_SAND_NOTE_MAX) rem[nrem].text[tl++] = (char)m.data[i++];
            rem[nrem].text[tl] = 0; rem[nrem].due = sand_ticks() + left * tick_hz; ++nrem;
        }
        if (done) break;
        sand_sleep_ms(20);
    }
}
static int p_rem_add(void *c, uint32_t seconds, const char *t)
{
    (void)c; if (nrem >= REM_MAX) return -1;
    if (herald_ensure()) {
        char msg[72]; uint32_t o = 0, k = 0; char dig[10]; uint32_t v = seconds; int d = 0;
        msg[o++] = 'R'; do { dig[d++] = (char)('0' + v % 10u); v /= 10u; } while (v);
        while (d) msg[o++] = dig[--d];
        msg[o++] = ' '; while (t[k] && o < sizeof(msg)) msg[o++] = t[k++];
        herald_send(msg, o);
    }
    rem[nrem].due = sand_ticks() + seconds * tick_hz; strcpy(rem[nrem].text, t); ++nrem; dirty = 1; return 0;
}
static uint32_t p_rem_count(void *c) { (void)c; return nrem; }
static int p_rem_get(void *c, uint32_t i, uint32_t *left, char *out, size_t cap)
{
    (void)c; if (i >= nrem || strlen(rem[i].text) + 1 > cap) return -1;
    int32_t d = (int32_t)(rem[i].due - sand_ticks()); *left = d > 0 ? ((uint32_t)d + tick_hz - 1u) / tick_hz : 0u;
    memcpy(out, rem[i].text, strlen(rem[i].text) + 1); return 0;
}
static int p_rem_clear(void *c) { (void)c; nrem = 0; dirty = 1; herald_send("C", 1); return 0; }
static void rem_fire(void)
{
    uint32_t now = sand_ticks();
    for (uint32_t i = 0; i < nrem; ) {
        if ((int32_t)(now - rem[i].due) >= 0) {
            char line[CITADEL_SAND_NOTE_MAX + 16] = "Reminder: "; strcpy(line + 10, rem[i].text[0] ? rem[i].text : "time is up");
            say("", line, C_WARN); alert = 1; dirty = 1;
            for (uint32_t k = i + 1; k < nrem; ++k) rem[k - 1] = rem[k];
            --nrem;
        } else ++i;
    }
}

/* ---- drawing ---- */
static void flush(void) { if (ncmd) { sand_win_draw(win, cmds, ncmd); ncmd = 0; } }
static struct sand_draw *next(uint8_t op)
{
    if (ncmd == SAND_WIN_MAX_DRAW) flush();
    struct sand_draw *c = &cmds[ncmd++]; memset(c, 0, sizeof(*c)); c->op = op; return c;
}
static void mono(int x, int y, uint32_t col, const char *s, int n)   /* <=38 characters per draw op */
{
    for (int start = 0; start < n; start += 38) {
        int len = n - start < 38 ? n - start : 38;
        struct sand_draw *c = next(SAND_DRAW_TEXT); c->face = SAND_FACE_MONO; c->x = (int16_t)(x + start * CW); c->y = (int16_t)y; c->color = col;
        for (int i = 0; i < len; ++i) c->text[i] = s[start + i];
    }
}
static char input[INPUT_MAX + 1];
static int input_len;
static char pending[INPUT_MAX + 1];                    /* a request waiting for the user's yes/no */
static int has_pending;

static void draw(void)
{
    int accent = sand_setting_get(SAND_SET_ACCENT); if (accent < 0 || accent >= (int)SAND_ACCENT_COUNT) accent = 0;
    uint32_t acc = accents[accent];
    struct sand_draw *c = next(SAND_DRAW_CLEAR); c->color = BG;
    c = next(SAND_DRAW_CIRCLE); c->x = SW - 44; c->y = 44; c->r = 20; c->color = acc; c->alpha = 255;
    c = next(SAND_DRAW_CIRCLE); c->x = SW - 38; c->y = 38; c->r = 12; c->color = 0xFFE9C8u; c->alpha = 140;
    c = next(SAND_DRAW_TEXT); c->face = SAND_FACE_LARGE; c->x = 24; c->y = 48; c->color = 0xFFFFFFu; memcpy(c->text, "CITADEL", 8);
    c = next(SAND_DRAW_TEXT); c->face = SAND_FACE_SMALL; c->x = 24; c->y = 70; c->color = C_MUTED; memcpy(c->text, "OFFLINE ASSISTANT", 18);
    { char cnt[40]; int n = 0; uint32_t v;
      v = nnotes; if (v >= 10) cnt[n++] = (char)('0' + v / 10); cnt[n++] = (char)('0' + v % 10); memcpy(cnt + n, nnotes == 1 ? " note  " : " notes ", 7); n += 7;
      v = nrem; cnt[n++] = (char)('0' + v % 10); memcpy(cnt + n, nrem == 1 ? " reminder" : " reminders", nrem == 1 ? 9 : 10); n += nrem == 1 ? 9 : 10;
      c = next(SAND_DRAW_TEXT); c->face = SAND_FACE_SMALL; c->x = 24; c->y = 84 - 4; c->color = alert ? C_WARN : C_MUTED; c->x = 150; c->y = 70;
      for (int i = 0; i < n; ++i) c->text[i] = cnt[i]; }
    for (int r = 0; r < nrows; ++r) mono(PAD_X, TOP_Y + r * CH, rows[r].color, rows[r].text, (int)strlen(rows[r].text));
    c = next(SAND_DRAW_RRECT); c->x = 12; c->y = SH - 44; c->w = SW - 24; c->h = 32; c->r = 10; c->color = 0x24205Au; c->color2 = 0x1B1842u; c->aux = 1; c->alpha = 255;
    c = next(SAND_DRAW_RING); c->x = 12; c->y = SH - 44; c->w = SW - 24; c->h = 32; c->r = 10; c->aux = 1; c->color = (has_pending || alert) ? C_WARN : acc; c->alpha = alert ? 255 : 120;
    const char *marker = has_pending ? "? " : "> ";
    mono(22, SH - 33, has_pending ? C_WARN : acc, marker, 2);
    int shown = input_len > COLS - 4 ? COLS - 4 : input_len; const char *from = input + (input_len - shown);
    mono(22 + 2 * CW, SH - 33, C_TEXT, from, shown);
    c = next(SAND_DRAW_RRECT); c->x = (int16_t)(22 + (2 + shown) * CW); c->y = SH - 36; c->w = CW; c->h = 16; c->r = 2; c->color = acc; c->alpha = 200;
    flush(); dirty = 0;
}

/* ---- assistant ---- */
static CitadelRegistry registry;
static CitadelSandPlatform platform;
static const CitadelPolicy policy_ask = { CITADEL_SAND_POLICY_PERMISSIONS, 0 };
static const CitadelPolicy policy_yes = { CITADEL_SAND_POLICY_PERMISSIONS, 1 };

static void answer(const char *text, uint32_t color) { say("", text, color); }
static void run_request(const char *text, int confirmed)
{
    CitadelCommand cmd; CitadelResult res;
    if (citadel_parse(text, strlen(text), &cmd) != CITADEL_OK) { answer("I did not understand that. Say 'help'.", C_WARN); return; }
    CitadelStatus st = citadel_dispatch(&registry, &cmd, confirmed ? &policy_yes : &policy_ask, &res);
    if (st == CITADEL_OK) answer(res.text, C_TEXT);
    else if (st == CITADEL_ERR_NOT_FOUND && res.text[0] == 0) answer("I do not know that one yet. Say 'help'.", C_WARN);
    else if (st == CITADEL_ERR_DENIED) answer("That is not allowed.", C_WARN);
    else if (res.text[0]) answer(res.text, C_WARN);
    else answer("I could not do that.", C_WARN);
}
static int submit(void)                                 /* returns 1 to quit */
{
    char text[INPUT_MAX + 1]; int n = 0, i = 0;
    while (i < input_len && input[i] == ' ') ++i;       /* trim, lower-case: matching is exact-phrase */
    for (; i < input_len; ++i) { char ch = input[i]; if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32); text[n++] = ch; }
    while (n > 0 && text[n - 1] == ' ') --n;
    text[n] = 0; input_len = 0; input[0] = 0; dirty = 1;
    if (n == 0) return 0;
    say(has_pending ? "? " : "> ", text, C_USER);
    if (has_pending) {
        has_pending = 0;
        if (!strcmp(text, "y") || !strcmp(text, "yes")) run_request(pending, 1); else answer("Cancelled. Nothing was changed.", C_MUTED);
        return 0;
    }
    if (!strcmp(text, "exit") || !strcmp(text, "quit") || !strcmp(text, "bye")) return 1;
    if (!strcmp(text, "clear")) { nrows = 0; return 0; }
    CitadelCommand cmd;
    if (citadel_parse(text, (size_t)n, &cmd) == CITADEL_OK && citadel_requires_confirmation(&registry, &cmd)) {
        memcpy(pending, text, (uint32_t)n + 1); has_pending = 1;
        char q[COLS + 24] = "Change the system: '"; int q0 = (int)strlen(q);
        memcpy(q + q0, text, (uint32_t)n); memcpy(q + q0 + n, "'? Type y to confirm.", 22);
        answer(q, C_WARN); return 0;
    }
    run_request(text, 0);
    return 0;
}

void _start(void)
{
    win = sand_win_create("CITADEL", SW, SH);
    if (win < 0) sand_exit(1);
    memset(&platform, 0, sizeof(platform));
    platform.app_count = p_count; platform.app_name = p_name; platform.launch = p_launch; platform.setting_get = p_get;
    platform.setting_set = p_set; platform.wall_time = p_time; platform.system_info = p_info;
    platform.note_add = p_note_add; platform.note_count = p_note_count; platform.note_get = p_note_get; platform.notes_clear = p_notes_clear;
    platform.reminder_add = p_rem_add; platform.reminder_count = p_rem_count; platform.reminder_get = p_rem_get; platform.reminders_clear = p_rem_clear;
    { struct sand_sysinfo si; if (sand_sysinfo(&si) == 0 && si.tick_hz) tick_hz = si.tick_hz; }
    notes_load();
    ch_req = sand_chan_open("herald.req"); ch_rep = sand_chan_open("herald.rep");
    herald_sync();
    scan_apps();
    if (citadel_registry_init(&registry) != CITADEL_OK || citadel_sandos_register(&registry, &platform) != CITADEL_OK) { sand_win_close(win); sand_exit(2); }
    say("", "Hello, I am Citadel. I work offline and only do what I can list: say 'help'.", C_TEXT);
    uint32_t idle = 0; int last_accent = -1;
    for (;;) {
        int accent = sand_setting_get(SAND_SET_ACCENT);
        if (accent != last_accent) { last_accent = accent; dirty = 1; }   /* follow accent changes made elsewhere */
        rem_fire();
        if (dirty) draw();
        struct sand_event ev; int32_t got = sand_win_event(win, &ev);
        if (got < 0) break;
        if (got == 0) { sand_sleep_ms(15); if (++idle % 64u == 0u) reap(); continue; }
        if (ev.type == SAND_EV_RESIZE) dirty = 1;
        else if (ev.type == SAND_EV_KEY) {
            if (alert) { alert = 0; dirty = 1; }
            if (ev.key == '\r' || ev.key == '\n') { if (submit()) break; }
            else if (ev.key == 0x08u || ev.key == 0x7Fu) { if (input_len) { input[--input_len] = 0; dirty = 1; } }
            else if (ev.key >= 0x20u && ev.key < 0x7Fu && input_len < INPUT_MAX) { input[input_len++] = (char)ev.key; input[input_len] = 0; dirty = 1; }
        }
    }
    sand_win_close(win);
    sand_exit(0);
}
