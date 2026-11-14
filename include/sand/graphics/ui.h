#ifndef SAND_GRAPHICS_UI_H
#define SAND_GRAPHICS_UI_H
#include <stdint.h>
#include "sand/graphics/gfx.h"
#include "sand/abi.h"

/* SAND Dusk shell (desktop chrome only; user apps such as Terminal and Files live in
 * the sand-apps repo and will attach through a window service, not through this file): wallpaper, menu bar, floating dock, windows, pointer.
 * Everything here is pure computation over a back buffer; the kernel supplies
 * data through the host hooks, so the same code is also built natively for
 * frame previews (tools/ui_preview.c). */
struct sand_ui_sysinfo {
    uint32_t ticks, tick_hz, free_frames, total_frames, heap_used, heap_total, switches, processes;
};
struct sand_ui_launcher_item { char label[16], app[17], subtitle[24]; uint32_t accent, accent2; char glyph; };
typedef struct {
    void *(*alloc)(uint32_t size);
    uint32_t (*ticks)(void);
    int (*wall_clock)(int *hour, int *minute);                  /* <0 when no RTC */
    void (*sysinfo)(struct sand_ui_sysinfo *out);
    uint32_t (*current_pid)(void);                              /* caller of a window syscall */
    int (*pid_alive)(uint32_t pid);                             /* 0 when the owner has exited */
    int (*launch)(const char *app);                             /* start an app for the desktop; <0 on failure */
    int (*launcher_count)(void);                                /* launcher catalogue (from app manifests) */
    int (*launcher_item)(int index, struct sand_ui_launcher_item *out);
} sand_ui_host_t;

#define SAND_UI_TERM_COLS 80
#define SAND_UI_TERM_ROWS 24
#define SAND_UI_SHELL_WINDOWS 2    /* 0 = system console, 1 = About SAND (shell-owned panels) */
#define SAND_UI_APP_WINDOWS 3      /* app-owned windows (window service) */
#define SAND_UI_WINDOWS (SAND_UI_SHELL_WINDOWS + SAND_UI_APP_WINDOWS)
#define SAND_UI_EVENTS 16

struct sand_ui_rect { int32_t x0, y0, x1, y1; };      /* half-open; empty when x0>=x1 */
struct sand_ui_window {
    int32_t x, y, w, h; const char *title; uint8_t kind, visible;
    /* window service (kind 2) */
    uint8_t alive, qh, qt; uint32_t owner, gen, seen_gen;
    uint32_t *surf; int32_t sw, sh; char title_buf[32];
    uint8_t resizable, anim; int32_t minw, minh; uint32_t anim_t0, anim_a;   /* anim: 0 none, 1 opening, 2 closing; anim_a 0..255 */
    struct sand_event q[SAND_UI_EVENTS];
};
typedef struct sand_ui {
    sand_ui_host_t host;
    int32_t w, h;
    uint32_t *back, *wall;
    gfx_t g;
    sand_font_t sans, sans_medium, sans_small, title_face, large, mono;
    struct sand_ui_window win[SAND_UI_WINDOWS];
    uint8_t order[SAND_UI_WINDOWS];                    /* z-order, back to front */
    int32_t px, py; uint8_t buttons;
    int32_t resize_win, rs_px, rs_py, rs_w0, rs_h0;
    uint32_t *scratch;                                 /* lazily allocated: backdrop copy for window fades */
    int32_t drag_win, drag_dx, drag_dy, press_win, hover_ctrl;   /* hover_ctrl = window*4 + control, -1 none */
    uint32_t *blur; int32_t bw, bh;                   /* 1/4-resolution frosted wallpaper */
    uint32_t liveness_seen;
    int launcher_open, launcher_hover;
    int32_t hover_dock;
    struct sand_ui_rect dirty, cursor_box;
    int cursor_moved;
    /* terminal */
    volatile uint32_t term_gen; uint32_t term_seen;
    char cells[SAND_UI_TERM_ROWS][SAND_UI_TERM_COLS];
    uint32_t cell_fg[SAND_UI_TERM_ROWS][SAND_UI_TERM_COLS];
    int term_row, term_col; uint32_t term_fg; int esc_state; int esc_n; int esc_p[6]; int term_bold;
    uint32_t blink_seen, clock_min_seen, about_seen;
    uint8_t cur_fill[24 * 16], cur_edge[24 * 16];
    uint32_t frames;
} sand_ui_t;

int sand_ui_init(sand_ui_t *ui, const sand_ui_host_t *host, int32_t w, int32_t h);
void sand_ui_term_write(sand_ui_t *ui, const char *data, uint32_t length);
void sand_ui_pointer(sand_ui_t *ui, int32_t dx, int32_t dy, uint8_t buttons);
void sand_ui_mark_all(sand_ui_t *ui);
int32_t sand_ui_sin(uint32_t t);                         /* period 1024, +-4096 */
/* Window service. ids are 1-based; every call is checked against the owner pid. */
int sand_ui_win_create(sand_ui_t *ui, uint32_t owner, const char *title, int32_t w, int32_t h);
int sand_ui_win_draw(sand_ui_t *ui, int id, uint32_t owner, const struct sand_draw *cmds, uint32_t count);
int sand_ui_win_event(sand_ui_t *ui, int id, uint32_t owner, struct sand_event *out);
int sand_ui_win_close(sand_ui_t *ui, int id, uint32_t owner);
int sand_ui_win_blit(sand_ui_t *ui, int id, uint32_t owner, const struct sand_blit *b, const uint32_t *pixels);
int sand_ui_win_resizable(sand_ui_t *ui, int id, uint32_t owner, int32_t min_w, int32_t min_h);
/* Keyboard routing: returns 1 when the focused window is an app window and took the key. */
int sand_ui_key(sand_ui_t *ui, uint8_t ascii);
/* Renders whatever changed into the back buffer; returns 1 and the screen rectangle to present. */
int sand_ui_frame(sand_ui_t *ui, struct sand_ui_rect *out);
/* Converts the rectangle (plus pointer overlay) to the display format (bpp 24 or 32). */
void sand_ui_present(sand_ui_t *ui, volatile uint8_t *fb, uint32_t pitch, uint32_t bpp, const struct sand_ui_rect *r);
void sand_ui_frame_pixels(const sand_ui_t *ui, const uint32_t **pixels);
#endif
