#include "sand/graphics/ui.h"

void sand_ui_paint_wallpaper(gfx_t *g);

/* ---- SAND Dusk palette (design tokens) ---- */
#define C_BAR      0x0C0A22u
#define C_SURFACE  0x15122Fu
#define C_SURFACE2 0x1F1B42u
#define C_LINE     0xFFFFFFu
#define C_TEXT     0xEDE9F7u
#define C_MUTED    0x9A94B8u
#define C_CHAMP    0xF2C98Au
#define C_CORAL    0xE8836Bu
#define C_CLOSE    0xFF6B6Bu
#define C_WARN     0xF5B84Bu
#define C_OK       0x5CD6A0u

#define MENU_H 30
#define TITLE_H 36
#define DOCK_ICON 54
#define DOCK_GAP 14
#define DOCK_PAD 12
#define CELL_W 8
#define CELL_H 17
#define TERM_PAD 12
#define ANIM_TICKS 16u                       /* open/close fade: 160 ms at 100 Hz */
#define ANIM_SCRATCH_W 640
#define ANIM_SCRATCH_H 560

/* ---- small helpers ---- */
static int32_t imin(int32_t a, int32_t b) { return a < b ? a : b; }
static int32_t imax(int32_t a, int32_t b) { return a > b ? a : b; }
static void rect_union(struct sand_ui_rect *r, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    if (x0 >= x1 || y0 >= y1) return;
    if (r->x0 >= r->x1) { r->x0 = x0; r->y0 = y0; r->x1 = x1; r->y1 = y1; return; }
    r->x0 = imin(r->x0, x0); r->y0 = imin(r->y0, y0); r->x1 = imax(r->x1, x1); r->y1 = imax(r->y1, y1);
}
static void mark(sand_ui_t *ui, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    x0 = imax(x0, 0); y0 = imax(y0, 0); x1 = imin(x1, ui->w); y1 = imin(y1, ui->h);
    rect_union(&ui->dirty, x0, y0, x1, y1);
}
void sand_ui_mark_all(sand_ui_t *ui) { mark(ui, 0, 0, ui->w, ui->h); }
static void mark_window(sand_ui_t *ui, int i) {
    struct sand_ui_window *w = &ui->win[i];
    mark(ui, w->x - 44, w->y - 36, w->x + w->w + 44, w->y + w->h + 60);
}
static char *put_u(char *p, uint32_t v) {
    char t[12]; int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) *p++ = t[--n];
    return p;
}
static char *put_2(char *p, uint32_t v) { *p++ = (char)('0' + (v / 10u) % 10u); *p++ = (char)('0' + v % 10u); return p; }
static char *put_s(char *p, const char *s) { while (*s) *p++ = *s++; return p; }

/* ---- pointer sprite: rasterised once from an arrow polygon, 4x4 supersampled ---- */
static int inside_arrow(int32_t x, int32_t y) {
    static const int32_t px[7] = { 0, 0, 16, 30, 40, 28, 48 }, py[7] = { 0, 68, 54, 80, 76, 50, 50 };
    int in = 0;
    for (int i = 0, j = 6; i < 7; j = i++)
        if ((py[i] > y) != (py[j] > y) && x < (px[j] - px[i]) * (y - py[i]) / (py[j] - py[i]) + px[i]) in = !in;
    return in;
}
static int near_arrow(int32_t x, int32_t y) {
    static const int32_t px[7] = { 0, 0, 16, 30, 40, 28, 48 }, py[7] = { 0, 68, 54, 80, 76, 50, 50 };
    for (int i = 0, j = 6; i < 7; j = i++)
        if (sand_dist_segment(x, y, px[i], py[i], px[j], py[j]) < 5) return 1;
    return 0;
}
static void build_cursor(sand_ui_t *ui) {
    for (int y = 0; y < 24; ++y) for (int x = 0; x < 16; ++x) {
        uint32_t fill = 0, edge = 0;
        for (int sy = 0; sy < 4; ++sy) for (int sx = 0; sx < 4; ++sx) {
            int32_t ux = (x - 1) * 4 + sx, uy = (y - 1) * 4 + sy;
            int in = inside_arrow(ux, uy);
            if (in) ++fill;
            if (in || near_arrow(ux, uy)) ++edge;
        }
        ui->cur_fill[y * 16 + x] = (uint8_t)(fill * 255u / 16u);
        ui->cur_edge[y * 16 + x] = (uint8_t)(edge * 230u / 16u);
    }
}

/* ---- frosted glass: a 1/4-resolution, heavily blurred copy of the wallpaper ---- */
static void build_blur(sand_ui_t *ui) {
    int32_t bw = ui->bw, bh = ui->bh;
    for (int32_t y = 0; y < bh; ++y) for (int32_t x = 0; x < bw; ++x) {
        uint32_t r = 0, g = 0, b = 0;
        for (int dy = 0; dy < 4; ++dy) for (int dx = 0; dx < 4; ++dx) {
            uint32_t p = ui->wall[(y * 4 + dy) * ui->w + x * 4 + dx];
            r += (p >> 16) & 255u; g += (p >> 8) & 255u; b += p & 255u;
        }
        ui->blur[y * bw + x] = GFX_RGB(r / 16u, g / 16u, b / 16u);
    }
    for (int pass = 0; pass < 4; ++pass) {              /* box blur radius 3, horizontal then vertical, twice */
        int horizontal = (pass & 1) == 0;
        int32_t lines = horizontal ? bh : bw, n = horizontal ? bw : bh;
        for (int32_t o = 0; o < lines; ++o) {
            uint32_t line[256];
            for (int32_t i = 0; i < n; ++i) {
                uint32_t r = 0, g = 0, b = 0, c = 0;
                for (int k = -3; k <= 3; ++k) {
                    int32_t j = i + k; if (j < 0 || j >= n) continue;
                    uint32_t p = horizontal ? ui->blur[o * bw + j] : ui->blur[j * bw + o];
                    r += (p >> 16) & 255u; g += (p >> 8) & 255u; b += p & 255u; ++c;
                }
                line[i] = GFX_RGB(r / c, g / c, b / c);
            }
            for (int32_t i = 0; i < n; ++i) { if (horizontal) ui->blur[o * bw + i] = line[i]; else ui->blur[i * bw + o] = line[i]; }
        }
    }
}
static uint32_t blur_px(const sand_ui_t *ui, int32_t x, int32_t y) {
    int32_t u = x - 2, v = y - 2; int32_t fx = u & 3, fy = v & 3; int32_t ix = u >> 2, iy = v >> 2;
    int32_t x0 = imax(0, imin(ui->bw - 1, ix)), x1 = imax(0, imin(ui->bw - 1, ix + 1));
    int32_t y0 = imax(0, imin(ui->bh - 1, iy)), y1 = imax(0, imin(ui->bh - 1, iy + 1));
    uint32_t p00 = ui->blur[y0 * ui->bw + x0], p10 = ui->blur[y0 * ui->bw + x1], p01 = ui->blur[y1 * ui->bw + x0], p11 = ui->blur[y1 * ui->bw + x1];
    uint32_t w00 = (uint32_t)((4 - fx) * (4 - fy)), w10 = (uint32_t)(fx * (4 - fy)), w01 = (uint32_t)((4 - fx) * fy), w11 = (uint32_t)(fx * fy);
    uint32_t r = (((p00 >> 16) & 255u) * w00 + ((p10 >> 16) & 255u) * w10 + ((p01 >> 16) & 255u) * w01 + ((p11 >> 16) & 255u) * w11) >> 4;
    uint32_t g = (((p00 >> 8) & 255u) * w00 + ((p10 >> 8) & 255u) * w10 + ((p01 >> 8) & 255u) * w01 + ((p11 >> 8) & 255u) * w11) >> 4;
    uint32_t b = ((p00 & 255u) * w00 + (p10 & 255u) * w10 + (p01 & 255u) * w01 + (p11 & 255u) * w11) >> 4;
    return GFX_RGB(r, g, b);
}
/* Coverage (0..255) of the rounded rectangle at pixel (xx,yy). */
static uint32_t rr_cov(int32_t xx, int32_t yy, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r) {
    int32_t dx = 0, dy = 0;
    if (xx < x + r) dx = (x + r) * 16 - (xx * 16 + 8); else if (xx >= x + w - r) dx = (xx * 16 + 8) - (x + w - r) * 16;
    if (yy < y + r) dy = (y + r) * 16 - (yy * 16 + 8); else if (yy >= y + h - r) dy = (yy * 16 + 8) - (y + h - r) * 16;
    if (dx <= 0 || dy <= 0) return 255u;
    int32_t d = (int32_t)sand_isqrt((uint32_t)(dx * dx + dy * dy));
    int32_t c = r * 16 - d + 8; if (c <= 0) return 0u; if (c >= 16) return 255u;
    return (uint32_t)c * 255u / 16u;
}
/* Frosted glass in a rounded rectangle, restricted to a clip band (title bar or body). */
static void glass(sand_ui_t *ui, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r,
                  int32_t cy0, int32_t cy1, uint32_t tint, uint32_t tint_alpha) {
    gfx_t *g = &ui->g;
    int32_t x0 = imax(x, g->cx0), x1 = imin(x + w, g->cx1), y0 = imax(imax(y, cy0), g->cy0), y1 = imin(imin(y + h, cy1), g->cy1);
    for (int32_t yy = y0; yy < y1; ++yy) {
        uint32_t *row = g->px + yy * g->stride;
        int edge_row = yy < y + r || yy >= y + h - r;
        for (int32_t xx = x0; xx < x1; ++xx) {
            uint32_t cov = (edge_row || xx < x + r || xx >= x + w - r) ? rr_cov(xx, yy, x, y, w, h, r) : 255u;
            if (!cov) continue;
            uint32_t c = gfx_mix(blur_px(ui, xx, yy), tint, tint_alpha);
            row[xx] = gfx_mix(row[xx], c, cov);
        }
    }
}

/* ---- init ---- */
int sand_ui_init(sand_ui_t *ui, const sand_ui_host_t *host, int32_t w, int32_t h) {
    ui->host = *host; ui->w = w; ui->h = h;
    ui->back = host->alloc((uint32_t)(w * h) * 4u);
    ui->wall = host->alloc((uint32_t)(w * h) * 4u);
    if (!ui->back || !ui->wall) return -1;
    ui->bw = w / 4; ui->bh = h / 4;
    if (ui->bw > 256 || ui->bh > 256) return -3;
    ui->blur = host->alloc((uint32_t)(ui->bw * ui->bh) * 4u);
    if (!ui->blur) return -1;
    if (font_face_init(&ui->sans, 13, 84, 0, host->alloc) || font_face_init(&ui->sans_medium, 13, 104, 0, host->alloc) ||
        font_face_init(&ui->sans_small, 11, 84, 0, host->alloc) || font_face_init(&ui->title_face, 12, 104, 0, host->alloc) ||
        font_face_init(&ui->large, 30, 54, 0, host->alloc) || font_face_init(&ui->mono, 13, 84, 1, host->alloc)) return -2;
    gfx_init(&ui->g, ui->back, w, h);
    gfx_t wg; gfx_init(&wg, ui->wall, w, h);
    sand_ui_paint_wallpaper(&wg);
    build_cursor(ui);
    build_blur(ui);
    for (int i = 0; i < SAND_UI_WINDOWS; ++i) { ui->win[i] = (struct sand_ui_window){ 0 }; ui->order[i] = (uint8_t)i; }
    ui->win[0] = (struct sand_ui_window){ .x = 40, .y = 64, .w = CELL_W * SAND_UI_TERM_COLS + 2 * TERM_PAD, .h = TITLE_H + CELL_H * SAND_UI_TERM_ROWS + 2 * TERM_PAD, .title = "System Console", .kind = 0, .visible = 1, .alive = 1 };
    ui->win[1] = (struct sand_ui_window){ .x = w - 304, .y = 118, .w = 280, .h = 330, .title = "About SAND", .kind = 1, .visible = 1, .alive = 1 };
    if (ui->win[1].x < 80) ui->win[1].x = 80;
    ui->order[0] = 2; ui->order[1] = 3; ui->order[2] = 4; ui->order[3] = 1; ui->order[4] = 0;
    ui->press_win = -1; ui->hover_ctrl = -1; ui->launcher_open = 0; ui->launcher_hover = -1; ui->liveness_seen = 0;
    ui->px = w / 2; ui->py = h / 2; ui->buttons = 0; ui->drag_win = -1; ui->hover_dock = -1; ui->resize_win = -1; ui->scratch = 0;
    ui->dirty = (struct sand_ui_rect){ 0, 0, 0, 0 };
    ui->cursor_box = (struct sand_ui_rect){ 0, 0, 0, 0 };
    ui->term_fg = C_TEXT; ui->term_gen = 1; ui->term_seen = 0;
    for (int r = 0; r < SAND_UI_TERM_ROWS; ++r) for (int c = 0; c < SAND_UI_TERM_COLS; ++c) { ui->cells[r][c] = ' '; ui->cell_fg[r][c] = C_TEXT; }
    ui->blink_seen = ui->clock_min_seen = ui->about_seen = 0xFFFFFFFFu;
    ui->cursor_moved = 1; ui->frames = 0;
    sand_ui_mark_all(ui);
    return 0;
}

/* ---- terminal emulation (console surface) ---- */
static const uint32_t ansi_pal[16] = {
    0x1B1836u, 0xFF7A8Au, 0x7EE0A8u, 0xF2C98Au, 0x8FA8FFu, 0xD99BFFu, 0x7FD8E8u, 0xE8E4F4u,
    0x5A5580u, 0xFF9AA6u, 0xA2F0C4u, 0xFFE0AEu, 0xB4C6FFu, 0xE8BCFFu, 0xA8EAF4u, 0xFFFFFFu };
static void term_scroll(sand_ui_t *ui) {
    for (int r = 1; r < SAND_UI_TERM_ROWS; ++r) for (int c = 0; c < SAND_UI_TERM_COLS; ++c) {
        ui->cells[r - 1][c] = ui->cells[r][c]; ui->cell_fg[r - 1][c] = ui->cell_fg[r][c];
    }
    for (int c = 0; c < SAND_UI_TERM_COLS; ++c) { ui->cells[SAND_UI_TERM_ROWS - 1][c] = ' '; ui->cell_fg[SAND_UI_TERM_ROWS - 1][c] = C_TEXT; }
}
static void term_newline(sand_ui_t *ui) {
    ui->term_col = 0;
    if (++ui->term_row >= SAND_UI_TERM_ROWS) { term_scroll(ui); ui->term_row = SAND_UI_TERM_ROWS - 1; }
}
static void term_sgr(sand_ui_t *ui) {
    int n = ui->esc_n ? ui->esc_n : 1;
    if (ui->esc_n == 0) ui->esc_p[0] = 0;
    for (int i = 0; i < n; ++i) {
        int p = ui->esc_p[i];
        if (p == 0 || p == 39) { ui->term_fg = C_TEXT; ui->term_bold = 0; }
        else if (p == 1) ui->term_bold = 1;
        else if (p == 2) ui->term_fg = C_MUTED;
        else if (p == 22) ui->term_bold = 0;
        else if (p >= 30 && p <= 37) ui->term_fg = ansi_pal[p - 30 + (ui->term_bold ? 8 : 0)];
        else if (p >= 90 && p <= 97) ui->term_fg = ansi_pal[p - 90 + 8];
        else if (p == 38 && ui->esc_p[i + 1] == 2 && i + 4 <= n - 1) {
            ui->term_fg = GFX_RGB(ui->esc_p[i + 2] & 255, ui->esc_p[i + 3] & 255, ui->esc_p[i + 4] & 255); i += 4;
        }
    }
}
static void term_csi(sand_ui_t *ui, char f) {
    int a = ui->esc_n > 0 ? ui->esc_p[0] : 0, b = ui->esc_n > 1 ? ui->esc_p[1] : 0;
    switch (f) {
    case 'm': term_sgr(ui); break;
    case 'J': if (a == 2 || a == 0 || a == 3) {
            for (int r = 0; r < SAND_UI_TERM_ROWS; ++r) for (int c = 0; c < SAND_UI_TERM_COLS; ++c) { ui->cells[r][c] = ' '; ui->cell_fg[r][c] = C_TEXT; }
            if (a == 2) { ui->term_row = 0; ui->term_col = 0; }
        } break;
    case 'H': case 'f':
        ui->term_row = imax(0, imin(SAND_UI_TERM_ROWS - 1, (a ? a : 1) - 1));
        ui->term_col = imax(0, imin(SAND_UI_TERM_COLS - 1, (b ? b : 1) - 1)); break;
    case 'K': for (int c = (a == 1 || a == 2) ? 0 : ui->term_col; c < ((a == 1) ? ui->term_col + 1 : SAND_UI_TERM_COLS); ++c) ui->cells[ui->term_row][c] = ' '; break;
    case 'A': ui->term_row = imax(0, ui->term_row - (a ? a : 1)); break;
    case 'B': ui->term_row = imin(SAND_UI_TERM_ROWS - 1, ui->term_row + (a ? a : 1)); break;
    case 'C': ui->term_col = imin(SAND_UI_TERM_COLS - 1, ui->term_col + (a ? a : 1)); break;
    case 'D': ui->term_col = imax(0, ui->term_col - (a ? a : 1)); break;
    default: break;
    }
}
void sand_ui_term_write(sand_ui_t *ui, const char *data, uint32_t length) {
    for (uint32_t i = 0; i < length; ++i) {
        uint8_t ch = (uint8_t)data[i];
        if (ui->esc_state == 1) { if (ch == '[') { ui->esc_state = 2; ui->esc_n = 0; for (int k = 0; k < 6; ++k) ui->esc_p[k] = 0; } else ui->esc_state = 0; continue; }
        if (ui->esc_state == 2) {
            if (ch >= '0' && ch <= '9') { if (ui->esc_n == 0) ui->esc_n = 1; ui->esc_p[ui->esc_n - 1] = ui->esc_p[ui->esc_n - 1] * 10 + (ch - '0'); }
            else if (ch == ';') { if (ui->esc_n == 0) ui->esc_n = 1; if (ui->esc_n < 6) ++ui->esc_n; }
            else if (ch >= 0x40 && ch <= 0x7E) { term_csi(ui, (char)ch); ui->esc_state = 0; }
            continue;
        }
        if (ch == 0x1B) { ui->esc_state = 1; continue; }
        if (ch == '\n') term_newline(ui);
        else if (ch == '\r') ui->term_col = 0;
        else if (ch == '\b' || ch == 0x7F) { if (ui->term_col > 0) --ui->term_col; }
        else if (ch == '\t') { ui->term_col = (ui->term_col + 8) & ~7; if (ui->term_col >= SAND_UI_TERM_COLS) term_newline(ui); }
        else if (ch >= 0x20 && ch < 0x7F) {
            if (ui->term_col >= SAND_UI_TERM_COLS) term_newline(ui);
            ui->cells[ui->term_row][ui->term_col] = (char)ch; ui->cell_fg[ui->term_row][ui->term_col] = ui->term_fg; ++ui->term_col;
        }
    }
    ++ui->term_gen;
}

/* ---- layout ---- */
static int dock_items(const sand_ui_t *ui, int *items) {
    int n = 0;
    items[n++] = -1;                                      /* launcher */
    for (int i = 0; i < SAND_UI_WINDOWS; ++i) if (i < SAND_UI_SHELL_WINDOWS || ui->win[i].alive) items[n++] = i;
    return n;
}
static void dock_rect(const sand_ui_t *ui, int32_t *x, int32_t *y, int32_t *w, int32_t *h) {
    int items[SAND_UI_WINDOWS + 1]; int n = dock_items(ui, items);
    *w = n * DOCK_ICON + (n - 1) * DOCK_GAP + 2 * DOCK_PAD + 8;
    *h = DOCK_ICON + 2 * DOCK_PAD - 2; *x = (ui->w - *w) / 2; *y = ui->h - *h - 14;
}
static void dock_icon_pos(const sand_ui_t *ui, int i, int32_t *x, int32_t *y) {
    int32_t dx, dy, dw, dh; dock_rect(ui, &dx, &dy, &dw, &dh);
    *x = dx + DOCK_PAD + 4 + i * (DOCK_ICON + DOCK_GAP); *y = dy + DOCK_PAD - 1 - (ui->hover_dock == i ? 6 : 0);
}
static void mark_dock(sand_ui_t *ui) { int32_t x, y, w, h; dock_rect(ui, &x, &y, &w, &h); mark(ui, 0, y - 24, ui->w, ui->h); (void)x; (void)w; (void)h; }
static int top_window(const sand_ui_t *ui) {
    for (int k = SAND_UI_WINDOWS - 1; k >= 0; --k) if (ui->win[ui->order[k]].visible) return ui->order[k];
    return -1;
}

/* ---- drawing ---- */
static void draw_dock(sand_ui_t *ui) {
    gfx_t *g = &ui->g; int32_t dx, dy, dw, dh; dock_rect(ui, &dx, &dy, &dw, &dh);
    if (!gfx_visible(g, dx - 40, dy - 40, dw + 80, dh + 80)) return;
    int items[SAND_UI_WINDOWS + 1]; int n = dock_items(ui, items);
    gfx_shadow(g, dx, dy, dw, dh, 24, 26, 10, 0x000000u, 120u);
    glass(ui, dx, dy, dw, dh, 24, dy, dy + dh, 0x1B1740u, 150u);
    gfx_rrect_ring(g, dx, dy, dw, dh, 24, 1, C_LINE, 44u);
    gfx_rect(g, dx + 24, dy + 1, dw - 48, 1, C_LINE, 40u);
    static const uint32_t tile_a[4] = { 0x6F8CFFu, 0x5CD6A0u, 0xE8836Bu, 0xB48CFFu };
    static const uint32_t tile_b[4] = { 0x3B3D9Eu, 0x1E7A66u, 0x9C3E5Au, 0x5A3AA0u };
    for (int k = 0; k < n; ++k) {
        int i = items[k]; int32_t x, y; dock_icon_pos(ui, k, &x, &y); int32_t c = DOCK_ICON;
        gfx_shadow(g, x, y, c, c, 14, 10, 4, 0x000000u, 90u);
        if (i < 0) {                                    /* launcher: a sun with eight rays */
            gfx_rrect_gradient(g, x, y, c, c, 14, 0x2A2560u, 0x100D27u, 255u);
            gfx_circle(g, x + 27, y + 27, 6, C_CHAMP, 255u);
            static const int8_t ray[8][4] = { {0,-11,0,-16}, {8,-8,11,-11}, {11,0,16,0}, {8,8,11,11}, {0,11,0,16}, {-8,8,-11,11}, {-11,0,-16,0}, {-8,-8,-11,-11} };
            for (int q = 0; q < 8; ++q) gfx_line(g, x + 27 + ray[q][0], y + 27 + ray[q][1], x + 27 + ray[q][2], y + 27 + ray[q][3], 26, 0xFFE2B0u, ui->launcher_open ? 255u : 190u);
            if (ui->launcher_open) gfx_rrect_ring(g, x, y, c, c, 14, 2, C_CHAMP, 200u);
        } else if (i == 0) {                                   /* System Console: dusk tile, chevron + caret */
            gfx_rrect_gradient(g, x, y, c, c, 14, 0x3A2F6Eu, 0x15112Fu, 255u);
            gfx_line(g, x + 15, y + 17, x + 25, y + 27, 40, C_CHAMP, 255u);
            gfx_line(g, x + 25, y + 27, x + 15, y + 37, 40, C_CHAMP, 255u);
            gfx_rrect(g, x + 29, y + 35, 12, 3, 1, C_TEXT, 230u);
        } else if (i == 1) {                            /* About: locus ring with a centre point */
            gfx_rrect_gradient(g, x, y, c, c, 14, 0xF0A47Au, 0xA2486Eu, 255u);
            gfx_rrect_ring(g, x + 12, y + 12, 30, 30, 15, 3, 0xFFF1DCu, 245u);
            gfx_circle(g, x + 27, y + 27, 4, 0xFFF1DCu, 255u);
        } else {                                        /* app window: accent tile with the app initial */
            uint32_t h = (uint32_t)(unsigned char)ui->win[i].title_buf[0] % 4u;
            gfx_rrect_gradient(g, x, y, c, c, 14, tile_a[h], tile_b[h], 255u);
            char l[2] = { ui->win[i].title_buf[0], 0 };
            gfx_text_center(g, &ui->large, x + c / 2, y + 38, l, 0xFFFFFFu, 245u);
        }
        gfx_rect(g, x + 8, y + 1, c - 16, 1, C_LINE, 90u);
        if (i >= 0) gfx_rrect_ring(g, x, y, c, c, 14, 1, C_LINE, 60u);
        if (i >= 0 && ui->win[i].visible) gfx_circle(g, x + c / 2, dy + dh - 8, 2, C_CHAMP, 230u);
    }
}
/* ---- launcher: a glass panel above the dock ---- */
struct launcher_app { const char *label, *app, *desc; uint32_t top, bottom; char glyph; };
static const struct launcher_app launcher_apps[] = {
    { "CAIRN", "cairn", "Terminal", 0xB9A6FFu, 0x4B3C9Au, '>' },
    { "LOCUS", "locus", "Files", 0x5CD6A0u, 0x1E7A66u, 'L' },
    { "QUANTA", "quanta", "Calculator", 0xE8836Bu, 0x9C3E5Au, 'Q' },
    { "CHRONOS", "chronos", "Stopwatch", 0xB48CFFu, 0x5A3AA0u, 'C' },
    { "LUMEN", "lumen", "Light field", 0x6FC3FFu, 0x2A5FA8u, 'L' },
};
#define LAUNCHER_COUNT ((int)(sizeof(launcher_apps) / sizeof(launcher_apps[0])))
#define LAUNCHER_W 280
#define LAUNCHER_ROW 52
static void launcher_rect(const sand_ui_t *ui, int32_t *x, int32_t *y, int32_t *w, int32_t *h) {
    int32_t dx, dy, dw, dh; dock_rect(ui, &dx, &dy, &dw, &dh);
    *w = LAUNCHER_W; *h = 40 + LAUNCHER_COUNT * LAUNCHER_ROW + 10; *x = dx; *y = dy - *h - 14;
    if (*x + *w > ui->w - 10) *x = ui->w - 10 - *w;
}
static int launcher_row(const sand_ui_t *ui, int32_t px, int32_t py) {
    if (!ui->launcher_open) return -1;
    int32_t x, y, w, h; launcher_rect(ui, &x, &y, &w, &h);
    if (px < x + 8 || px >= x + w - 8 || py < y + 36) return -1;
    int row = (py - y - 36) / LAUNCHER_ROW;
    return (row >= 0 && row < LAUNCHER_COUNT) ? row : -1;
}
static void mark_launcher(sand_ui_t *ui) { int32_t x, y, w, h; launcher_rect(ui, &x, &y, &w, &h); mark(ui, x - 50, y - 50, x + w + 50, y + h + 50); mark_dock(ui); }
static void draw_launcher(sand_ui_t *ui) {
    if (!ui->launcher_open) return;
    gfx_t *g = &ui->g; int32_t x, y, w, h; launcher_rect(ui, &x, &y, &w, &h);
    if (!gfx_visible(g, x - 50, y - 50, w + 100, h + 100)) return;
    gfx_shadow(g, x, y, w, h, 20, 34, 14, 0x000000u, 150u);
    glass(ui, x, y, w, h, 20, y, y + h, 0x17133Au, 165u);
    gfx_rrect_ring(g, x, y, w, h, 20, 1, C_LINE, 50u);
    gfx_rect(g, x + 20, y + 1, w - 40, 1, C_LINE, 60u);
    gfx_text(g, &ui->sans_small, x + 20, y + 26, "APPLICATIONS", C_MUTED, 255u, 120);
    for (int i = 0; i < LAUNCHER_COUNT; ++i) {
        const struct launcher_app *a = &launcher_apps[i]; int32_t ry = y + 36 + i * LAUNCHER_ROW;
        if (ui->launcher_hover == i) { gfx_rrect(g, x + 8, ry, w - 16, LAUNCHER_ROW - 6, 12, 0x3A3578u, 200u); gfx_rrect_ring(g, x + 8, ry, w - 16, LAUNCHER_ROW - 6, 12, 1, C_CHAMP, 90u); }
        gfx_rrect_gradient(g, x + 18, ry + 5, 36, 36, 11, a->top, a->bottom, 255u);
        char l[2] = { a->glyph, 0 };
        gfx_text_center(g, &ui->large, x + 36, ry + 32, l, 0xFFFFFFu, 245u);
        gfx_rrect_ring(g, x + 18, ry + 5, 36, 36, 11, 1, C_LINE, 60u);
        gfx_text(g, &ui->sans_medium, x + 66, ry + 20, a->label, C_TEXT, 255u, 60);
        gfx_text(g, &ui->sans_small, x + 66, ry + 36, a->desc, C_MUTED, 255u, 0);
    }
}
static void draw_menubar(sand_ui_t *ui) {
    gfx_t *g = &ui->g;
    if (!gfx_visible(g, 0, 0, ui->w, MENU_H + 1)) return;
    glass(ui, 0, -20, ui->w, MENU_H + 20, 0, 0, MENU_H, C_BAR, 120u);
    gfx_rect(g, 0, MENU_H, ui->w, 1, C_LINE, 22u);
    gfx_circle(g, 20, 15, 6, C_CHAMP, 255u);
    gfx_circle(g, 22, 13, 4, 0xFFE9C8u, 200u);
    int32_t x = gfx_text(g, &ui->sans_medium, 36, 20, "SAND", C_TEXT, 255u, 160);
    int top = top_window(ui);
    if (top >= 0) gfx_text(g, &ui->sans, x + 18, 20, ui->win[top].title, C_MUTED, 255u, 0);
    int hh, mm; char t[8], *p = t;
    if (ui->host.wall_clock && ui->host.wall_clock(&hh, &mm) == 0) { p = put_2(p, (uint32_t)hh); *p++ = ':'; p = put_2(p, (uint32_t)mm); *p = 0; }
    else { uint32_t s = ui->host.ticks() / 100u; p = put_2(p, (s / 60u) % 100u); *p++ = ':'; p = put_2(p, s % 60u); *p = 0; }
    gfx_text_right(g, &ui->sans_medium, ui->w - 16, 20, t, C_TEXT, 255u);
}
/* "Horizon" window controls (SAND's own): minimise = a dune ripple, centre = a rising sun,
 * close = a setting sun. They sit on the right, are bare glyphs at rest, and bloom into a
 * tinted halo on hover. Window identity is a small accent sun on the left of the title bar. */
#define CTRL_MIN 0
#define CTRL_CENTER 1
#define CTRL_CLOSE 2
static int32_t ctrl_cx(const struct sand_ui_window *w, int c) { return w->x + w->w - 78 + c * 28 + (c == CTRL_CLOSE ? 0 : 0); }
static void half_disc(gfx_t *g, int32_t cx, int32_t base_y, int32_t r, uint32_t col, uint32_t a) {
    for (int32_t dy = 0; dy < r; ++dy) {
        int32_t half = (int32_t)sand_isqrt((uint32_t)(r * r - dy * dy));
        gfx_rect(g, cx - half, base_y - dy, half * 2 + 1, 1, col, a);
    }
}
static void draw_controls(sand_ui_t *ui, int i, int focused) {
    gfx_t *g = &ui->g; struct sand_ui_window *w = &ui->win[i];
    static const uint32_t hue[3] = { 0x7FD6B0u, 0xF2C98Au, 0xFF7E73u };
    int32_t cy = w->y + 18;
    uint32_t accent = i == 0 ? C_CHAMP : (i == 1 ? C_CORAL : 0x8FA8FFu);
    gfx_circle(g, w->x + 20, cy, 6, accent, focused ? 255u : 120u);          /* identity sun */
    gfx_circle(g, w->x + 22, cy - 2, 3, 0xFFFFFFu, focused ? 110u : 50u);
    for (int c = 0; c < 3; ++c) {
        int32_t cx = ctrl_cx(w, c);
        int hot = ui->hover_ctrl == i * 4 + c;
        uint32_t col = hot ? hue[c] : (focused ? 0xB9B3D6u : 0x6E6890u), a = hot ? 255u : 230u;
        if (hot) { gfx_circle(g, cx, cy, 12, hue[c], 46u); gfx_rrect_ring(g, cx - 12, cy - 12, 24, 24, 12, 1, hue[c], 120u); }
        if (c == CTRL_MIN) {                                                  /* dune ripple */
            gfx_line(g, cx - 7, cy + 1, cx - 3, cy - 2, 26, col, a); gfx_line(g, cx - 3, cy - 2, cx + 1, cy + 2, 26, col, a);
            gfx_line(g, cx + 1, cy + 2, cx + 5, cy - 1, 26, col, a); gfx_line(g, cx + 5, cy - 1, cx + 8, cy + 1, 26, col, a);
        } else if (c == CTRL_CENTER) {                                        /* rising sun, hollow */
            gfx_line(g, cx - 8, cy + 4, cx + 8, cy + 4, 24, col, a);
            gfx_line(g, cx - 5, cy + 4, cx - 4, cy, 24, col, a); gfx_line(g, cx - 4, cy, cx - 1, cy - 3, 24, col, a);
            gfx_line(g, cx - 1, cy - 3, cx + 2, cy - 3, 24, col, a); gfx_line(g, cx + 2, cy - 3, cx + 4, cy, 24, col, a);
            gfx_line(g, cx + 4, cy, cx + 5, cy + 4, 24, col, a);
        } else {                                                              /* setting sun, filled */
            half_disc(g, cx, cy + 4, 6, col, a);
            gfx_line(g, cx - 9, cy + 5, cx + 9, cy + 5, 24, col, a);
        }
    }
}
/* "Dune glass" window chrome: frosted title bar and body, a dune-ridge title line, a
 * capsule control pod, and a champagne glow on the focused window. */
static void draw_frame(sand_ui_t *ui, int i, int focused) {
    gfx_t *g = &ui->g; struct sand_ui_window *w = &ui->win[i];
    if (!gfx_visible(g, w->x - 60, w->y - 60, w->w + 120, w->h + 120)) return;
    if (focused) gfx_shadow(g, w->x, w->y, w->w, w->h, 12, 46, 0, C_CHAMP, 30u);
    gfx_shadow(g, w->x, w->y, w->w, w->h, 12, focused ? 34 : 22, focused ? 16 : 8, 0x03020Cu, focused ? 170u : 100u);
    uint32_t body_tint = i == 0 ? 0x0F0C26u : (i == 1 ? 0x1B1742u : 0x15122Fu);
    glass(ui, w->x, w->y, w->w, w->h, 12, w->y + TITLE_H, w->y + w->h, body_tint, i == 0 ? 214u : 206u);
    glass(ui, w->x, w->y, w->w, w->h, 12, w->y, w->y + TITLE_H, focused ? 0x2A2362u : 0x1B1840u, focused ? 140u : 175u);
    gfx_rect(g, w->x + 12, w->y + 1, w->w - 24, 1, C_LINE, focused ? 60u : 26u);
    for (int32_t xx = 12; xx < w->w - 12; ++xx) {                 /* dune-ridge line under the title bar */
        int32_t edge = imin(xx - 12, w->w - 12 - xx); if (edge > 40) edge = 40;
        int32_t yy = w->y + TITLE_H - 2 + ((sand_ui_sin((uint32_t)xx * 7u) * 3) >> 13);
        gfx_rect(g, w->x + xx, yy, 1, 1, C_CHAMP, (uint32_t)(focused ? 90 : 34) * (uint32_t)edge / 40u);
    }
    draw_controls(ui, i, focused);
    gfx_text_center(g, &ui->title_face, w->x + w->w / 2, w->y + 23, w->title, focused ? C_TEXT : C_MUTED, 255u);
    gfx_rrect_ring(g, w->x, w->y, w->w, w->h, 12, 1, focused ? C_CHAMP : C_LINE, focused ? 84u : 30u);
}
static void blit_surface(sand_ui_t *ui, int i) {
    gfx_t *g = &ui->g; struct sand_ui_window *w = &ui->win[i];
    if (!w->surf) return;
    int32_t oy = w->y + TITLE_H;
    int32_t x0 = imax(w->x, g->cx0), x1 = imin(w->x + w->w, g->cx1), y0 = imax(oy, g->cy0), y1 = imin(oy + w->sh, g->cy1);
    for (int32_t yy = y0; yy < y1; ++yy) {
        const uint32_t *src = w->surf + (yy - oy) * w->sw; uint32_t *row = g->px + yy * g->stride;
        int edge = yy >= w->y + w->h - 12;
        for (int32_t xx = x0; xx < x1; ++xx) {
            uint32_t cov = (edge && (xx < w->x + 12 || xx >= w->x + w->w - 12)) ? rr_cov(xx, yy, w->x, w->y, w->w, w->h, 12) : 255u;
            row[xx] = cov == 255u ? src[xx - w->x] : gfx_mix(row[xx], src[xx - w->x], cov);
        }
    }
}
static void draw_console(sand_ui_t *ui, int focused) {
    gfx_t *g = &ui->g; struct sand_ui_window *w = &ui->win[0];
    int32_t ox = w->x + TERM_PAD, oy = w->y + TITLE_H + TERM_PAD;
    for (int r = 0; r < SAND_UI_TERM_ROWS; ++r) {
        int32_t y = oy + r * CELL_H;
        if (y + CELL_H < g->cy0 || y > g->cy1) continue;
        for (int c = 0; c < SAND_UI_TERM_COLS; ++c) {
            char ch = ui->cells[r][c];
            if (ch != ' ') gfx_glyph(g, &ui->mono, ox + c * CELL_W, y + 13, ch, ui->cell_fg[r][c], 255u);
        }
    }
    uint32_t phase = (ui->host.ticks() / 50u) & 1u;
    if (focused && phase == 0u) gfx_rrect(g, ox + ui->term_col * CELL_W, oy + ui->term_row * CELL_H + 1, 7, 15, 2, C_CHAMP, 170u);
}
static void draw_meter(sand_ui_t *ui, int32_t x, int32_t y, int32_t w, uint32_t used, uint32_t total) {
    gfx_t *g = &ui->g; gfx_rrect(g, x, y, w, 8, 4, 0x0E0C24u, 255u);
    int32_t f = total ? (int32_t)((used * (uint32_t)w) / total) : 0;
    if (f < 8 && used) f = 8;
    if (f > w) f = w;
    if (f >= 8) gfx_rrect_gradient(g, x, y, f, 8, 4, C_CHAMP, C_CORAL, 255u);
}
static void draw_about(sand_ui_t *ui) {
    gfx_t *g = &ui->g; struct sand_ui_window *w = &ui->win[1];
    struct sand_ui_sysinfo si; ui->host.sysinfo(&si);
    int32_t x = w->x + 26, y = w->y + TITLE_H;
    gfx_circle(g, x + 22, y + 44, 22, C_CHAMP, 255u);
    gfx_circle(g, x + 29, y + 37, 15, 0xFFE9C8u, 150u);
    gfx_text(g, &ui->large, x + 58, y + 54, "SAND", C_TEXT, 255u, 320);
    gfx_text(g, &ui->sans_small, x + 60, y + 74, "Written from the first byte.", C_MUTED, 255u, 0);
    char b[48], *p;
    uint32_t secs = si.ticks / (si.tick_hz ? si.tick_hz : 100u);
    int32_t ry = y + 112;
    gfx_text(g, &ui->sans, x, ry, "Uptime", C_MUTED, 255u, 0);
    p = b; p = put_u(p, secs / 3600u); *p++ = ':'; p = put_2(p, (secs / 60u) % 60u); *p++ = ':'; p = put_2(p, secs % 60u); *p = 0;
    gfx_text_right(g, &ui->sans_medium, w->x + w->w - 26, ry, b, C_TEXT, 255u);
    ry += 36;
    uint32_t used_fr = si.total_frames - si.free_frames;
    gfx_text(g, &ui->sans, x, ry, "Frames", C_MUTED, 255u, 0);
    p = b; p = put_u(p, used_fr / 256u); p = put_s(p, " / "); p = put_u(p, si.total_frames / 256u); p = put_s(p, " MiB"); *p = 0;
    gfx_text_right(g, &ui->sans_medium, w->x + w->w - 26, ry, b, C_TEXT, 255u);
    draw_meter(ui, x, ry + 10, w->w - 52, used_fr, si.total_frames);
    ry += 46;
    gfx_text(g, &ui->sans, x, ry, "Kernel heap", C_MUTED, 255u, 0);
    p = b; p = put_u(p, si.heap_used / 1024u); p = put_s(p, " KiB"); *p = 0;
    gfx_text_right(g, &ui->sans_medium, w->x + w->w - 26, ry, b, C_TEXT, 255u);
    draw_meter(ui, x, ry + 10, w->w - 52, si.heap_used, si.heap_total);
    ry += 46;
    gfx_text(g, &ui->sans, x, ry, "Processes", C_MUTED, 255u, 0);
    p = b; p = put_u(p, si.processes); *p = 0;
    gfx_text_right(g, &ui->sans_medium, w->x + w->w - 26, ry, b, C_TEXT, 255u);
    ry += 30;
    gfx_text(g, &ui->sans, x, ry, "Context switches", C_MUTED, 255u, 0);
    p = b; p = put_u(p, si.switches); *p = 0;
    gfx_text_right(g, &ui->sans_medium, w->x + w->w - 26, ry, b, C_TEXT, 255u);
}


/* ---- window service ---- */
static void push_event(struct sand_ui_window *w, const struct sand_event *e) {
    uint8_t next = (uint8_t)((w->qt + 1u) % SAND_UI_EVENTS);
    if (e->type == SAND_EV_MOUSE_MOVE && w->qh != w->qt) {
        struct sand_event *last = &w->q[(w->qt + SAND_UI_EVENTS - 1u) % SAND_UI_EVENTS];
        if (last->type == SAND_EV_MOUSE_MOVE) { *last = *e; return; }
    }
    if (next == w->qh) w->qh = (uint8_t)((w->qh + 1u) % SAND_UI_EVENTS);   /* drop oldest */
    w->q[w->qt] = *e; w->qt = next;
}
static struct sand_ui_window *app_window(sand_ui_t *ui, int id, uint32_t owner) {
    if (id < 1 || id > SAND_UI_APP_WINDOWS) return 0;
    struct sand_ui_window *w = &ui->win[SAND_UI_SHELL_WINDOWS + id - 1];
    return (w->alive && w->anim != 2 && w->owner == owner) ? w : 0;
}
static void destroy_app_window(sand_ui_t *ui, int index) {
    struct sand_ui_window *w = &ui->win[index];
    if (!w->alive) return;
    mark_window(ui, index); w->visible = 0; w->alive = 0; w->qh = w->qt = 0;
    mark(ui, 0, 0, ui->w, MENU_H + 1); mark_dock(ui);
}
static void begin_close(sand_ui_t *ui, int index) {
    struct sand_ui_window *w = &ui->win[index];
    if (!w->alive || w->anim == 2) return;
    w->anim = 2; w->anim_t0 = ui->host.ticks(); w->anim_a = 255u;
    if (ui->press_win == index) ui->press_win = -1;
    mark_window(ui, index);
}
int sand_ui_win_create(sand_ui_t *ui, uint32_t owner, const char *title, int32_t w, int32_t h) {
    if (w < 200 || w > SAND_WIN_MAX_W || h < 120 || h > SAND_WIN_MAX_H) return -1;
    for (int s = 0; s < SAND_UI_APP_WINDOWS; ++s) {
        int idx = SAND_UI_SHELL_WINDOWS + s; struct sand_ui_window *win = &ui->win[idx];
        if (win->alive) continue;
        if (!win->surf) { win->surf = ui->host.alloc((uint32_t)(SAND_WIN_MAX_W * SAND_WIN_MAX_H) * 4u); if (!win->surf) return -1; }
        win->sw = w; win->sh = h;
        for (int32_t i = 0; i < w * h; ++i) win->surf[i] = 0x15122Fu;
        int n = 0; while (title && title[n] && n < 31) { win->title_buf[n] = title[n]; ++n; }
        win->title_buf[n] = 0; if (n == 0) { win->title_buf[0] = 'A'; win->title_buf[1] = 0; }
        win->title = win->title_buf; win->kind = 2; win->owner = owner; win->gen = win->seen_gen = 0; win->qh = win->qt = 0;
        win->w = w; win->h = h + TITLE_H;
        win->x = imin(130 + 38 * s, ui->w - w - 10); win->y = imin(86 + 32 * s, ui->h - win->h - 100); if (win->y < MENU_H + 6) win->y = MENU_H + 6;
        win->resizable = 0; win->minw = w; win->minh = h; win->anim = 1; win->anim_t0 = ui->host.ticks(); win->anim_a = 0;
        win->visible = 1; win->alive = 1;
        { int pos = 0; while (ui->order[pos] != idx) ++pos;
          for (; pos < SAND_UI_WINDOWS - 1; ++pos) ui->order[pos] = ui->order[pos + 1];
          ui->order[SAND_UI_WINDOWS - 1] = (uint8_t)idx; }
        mark_window(ui, idx); mark(ui, 0, 0, ui->w, MENU_H + 1); mark_dock(ui);
        return s + 1;
    }
    return -1;
}
static void exec_draw(sand_ui_t *ui, gfx_t *sg, const struct sand_draw *c) {
    uint32_t a = c->alpha ? c->alpha : 255u;
    sand_font_t *faces[5] = { &ui->sans, &ui->sans_medium, &ui->sans_small, &ui->large, &ui->mono };
    sand_font_t *f = faces[c->face < 5 ? c->face : 0];
    char text[40]; int n = 0; while (n < 39 && c->text[n]) { text[n] = c->text[n]; ++n; } text[n] = 0;
    switch (c->op) {
    case SAND_DRAW_CLEAR: gfx_rect(sg, 0, 0, sg->w, sg->h, c->color, 255u); break;
    case SAND_DRAW_RECT: gfx_rect(sg, c->x, c->y, c->w, c->h, c->color, a); break;
    case SAND_DRAW_RRECT: if (c->aux == 1) gfx_rrect_gradient(sg, c->x, c->y, c->w, c->h, c->r, c->color, c->color2, a); else gfx_rrect(sg, c->x, c->y, c->w, c->h, c->r, c->color, a); break;
    case SAND_DRAW_RING: gfx_rrect_ring(sg, c->x, c->y, c->w, c->h, c->r, c->aux > 0 ? c->aux : 1, c->color, a); break;
    case SAND_DRAW_CIRCLE: gfx_circle(sg, c->x, c->y, c->r, c->color, a); break;
    case SAND_DRAW_LINE: gfx_line(sg, c->x, c->y, c->w, c->h, c->aux > 0 ? c->aux : 16, c->color, a); break;
    case SAND_DRAW_SHADOW: gfx_shadow(sg, c->x, c->y, c->w, c->h, c->r, c->aux > 0 ? c->aux : 12, 4, c->color, a); break;
    case SAND_DRAW_TEXT:
        if (c->align == SAND_ALIGN_CENTER) gfx_text_center(sg, f, c->x, c->y, text, c->color, a);
        else if (c->align == SAND_ALIGN_RIGHT) gfx_text_right(sg, f, c->x, c->y, text, c->color, a);
        else gfx_text(sg, f, c->x, c->y, text, c->color, a, 0);
        break;
    default: break;
    }
}
int sand_ui_win_draw(sand_ui_t *ui, int id, uint32_t owner, const struct sand_draw *cmds, uint32_t count) {
    struct sand_ui_window *w = app_window(ui, id, owner);
    if (!w) return SAND_E_NOENT;
    if (count == 0u || count > SAND_WIN_MAX_DRAW) return SAND_E_FAIL;
    gfx_t sg; gfx_init(&sg, w->surf, w->sw, w->sh);
    for (uint32_t i = 0; i < count; ++i) exec_draw(ui, &sg, &cmds[i]);
    ++w->gen;
    return 0;
}
int sand_ui_win_event(sand_ui_t *ui, int id, uint32_t owner, struct sand_event *out) {
    struct sand_ui_window *w = app_window(ui, id, owner);
    if (!w) return SAND_E_NOENT;
    if (w->qh == w->qt) return 0;
    *out = w->q[w->qh]; w->qh = (uint8_t)((w->qh + 1u) % SAND_UI_EVENTS);
    return 1;
}
int sand_ui_win_close(sand_ui_t *ui, int id, uint32_t owner) {
    struct sand_ui_window *w = app_window(ui, id, owner);
    if (!w) return SAND_E_NOENT;
    begin_close(ui, SAND_UI_SHELL_WINDOWS + id - 1);
    return 0;
}
int sand_ui_win_blit(sand_ui_t *ui, int id, uint32_t owner, const struct sand_blit *b, const uint32_t *pixels) {
    struct sand_ui_window *w = app_window(ui, id, owner);
    if (!w) return SAND_E_NOENT;
    int32_t sc = b->scale <= 0 ? 1 : b->scale;
    if (!pixels || b->w <= 0 || b->h <= 0 || sc > 4) return SAND_E_FAIL;
    for (int32_t dy = imax(0, b->y); dy < imin(w->sh, b->y + b->h * sc); ++dy) {
        const uint32_t *src = pixels + ((dy - b->y) / sc) * b->w;
        for (int32_t dx = imax(0, b->x); dx < imin(w->sw, b->x + b->w * sc); ++dx)
            w->surf[dy * w->sw + dx] = src[(dx - b->x) / sc] & 0xFFFFFFu;
    }
    ++w->gen;
    return 0;
}
int sand_ui_win_resizable(sand_ui_t *ui, int id, uint32_t owner, int32_t min_w, int32_t min_h) {
    struct sand_ui_window *w = app_window(ui, id, owner);
    if (!w) return SAND_E_NOENT;
    if (min_w < 200 || min_w > SAND_WIN_MAX_W || min_h < 120 || min_h > SAND_WIN_MAX_H) return SAND_E_FAIL;
    w->resizable = 1; w->minw = min_w; w->minh = min_h;
    mark_window(ui, SAND_UI_SHELL_WINDOWS + id - 1);
    return 0;
}
int sand_ui_key(sand_ui_t *ui, uint8_t ascii) {
    int top = top_window(ui);
    if (top < SAND_UI_SHELL_WINDOWS) return 0;
    struct sand_event e = { SAND_EV_KEY, 0, 0, ascii, ui->buttons };
    push_event(&ui->win[top], &e);
    return 1;
}

/* ---- input ---- */
static int hit_window(const sand_ui_t *ui, int32_t x, int32_t y) {
    for (int k = SAND_UI_WINDOWS - 1; k >= 0; --k) {
        const struct sand_ui_window *w = &ui->win[ui->order[k]];
        if (w->visible && w->anim != 2 && x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h) return ui->order[k];
    }
    return -1;
}
static void raise_window(sand_ui_t *ui, int idx) {
    int pos = 0; while (ui->order[pos] != idx) ++pos;
    for (; pos < SAND_UI_WINDOWS - 1; ++pos) ui->order[pos] = ui->order[pos + 1];
    ui->order[SAND_UI_WINDOWS - 1] = (uint8_t)idx;
    mark_window(ui, idx); mark(ui, 0, 0, ui->w, MENU_H + 1);
}
static int dock_hit(const sand_ui_t *ui, int32_t x, int32_t y) {
    int32_t dx, dy, dw, dh; dock_rect(ui, &dx, &dy, &dw, &dh);
    int items[SAND_UI_WINDOWS + 1]; int n = dock_items(ui, items);
    if (x < dx || x >= dx + dw || y < dy - 10 || y >= dy + dh) return -1;
    for (int k = 0; k < n; ++k) {
        int32_t ix, iy; dock_icon_pos(ui, k, &ix, &iy);
        if (x >= ix - DOCK_GAP / 2 && x < ix + DOCK_ICON + DOCK_GAP / 2) return k;
    }
    return -1;
}
void sand_ui_pointer(sand_ui_t *ui, int32_t dx, int32_t dy, uint8_t buttons) {
    int32_t ox = ui->px, oy = ui->py;
    ui->px = imax(0, imin(ui->w - 1, ui->px + dx)); ui->py = imax(0, imin(ui->h - 1, ui->py + dy));
    int moved = ui->px != ox || ui->py != oy;
    if (moved) ui->cursor_moved = 1;
    int hd = dock_hit(ui, ui->px, ui->py);
    if (hd != ui->hover_dock) { ui->hover_dock = hd; mark_dock(ui); }
    { int hc = -1; int th = top_window(ui);
      for (int k = SAND_UI_WINDOWS - 1; k >= 0 && hc < 0; --k) {
          int wi = ui->order[k]; const struct sand_ui_window *hw = &ui->win[wi];
          if (!hw->visible) continue;
          if (ui->px >= hw->x && ui->px < hw->x + hw->w && ui->py >= hw->y && ui->py < hw->y + hw->h) {
              for (int c = 0; c < 3; ++c) { int32_t ex = ui->px - ctrl_cx(hw, c), ey = ui->py - (hw->y + 18); if (ex * ex + ey * ey <= 144 && ui->py < hw->y + TITLE_H) hc = wi * 4 + c; }
              break;
          }
      }
      (void)th;
      if (hc != ui->hover_ctrl) {
          int old = ui->hover_ctrl; ui->hover_ctrl = hc;
          if (old >= 0) mark(ui, ui->win[old / 4].x, ui->win[old / 4].y, ui->win[old / 4].x + ui->win[old / 4].w, ui->win[old / 4].y + TITLE_H);
          if (hc >= 0) mark(ui, ui->win[hc / 4].x, ui->win[hc / 4].y, ui->win[hc / 4].x + ui->win[hc / 4].w, ui->win[hc / 4].y + TITLE_H);
      } }
    int pressed = (buttons & 1u) && !(ui->buttons & 1u), released = !(buttons & 1u) && (ui->buttons & 1u);
    if (ui->drag_win >= 0 && (buttons & 1u)) {
        struct sand_ui_window *w = &ui->win[ui->drag_win];
        mark_window(ui, ui->drag_win);
        w->x = ui->px - ui->drag_dx; w->y = imax(MENU_H + 2, ui->py - ui->drag_dy);
        w->x = imax(-w->w + 80, imin(ui->w - 80, w->x)); w->y = imin(ui->h - 40, w->y);
        mark_window(ui, ui->drag_win);
    }
    if (ui->resize_win >= 0 && (buttons & 1u)) {
        int ri = ui->resize_win; struct sand_ui_window *w = &ui->win[ri];
        if (!w->alive || w->anim == 2) ui->resize_win = -1;
        else {
            int32_t nw = imax(w->minw, imin(SAND_WIN_MAX_W, ui->rs_w0 + (ui->px - ui->rs_px)));
            int32_t nh = imax(w->minh, imin(SAND_WIN_MAX_H, ui->rs_h0 + (ui->py - ui->rs_py)));
            if (nw != w->sw || nh != w->sh) {
                mark_window(ui, ri);
                w->sw = nw; w->sh = nh; w->w = nw; w->h = nh + TITLE_H;
                for (int32_t i = 0; i < nw * nh; ++i) w->surf[i] = 0x15122Fu;
                struct sand_event e = { SAND_EV_RESIZE, nw, nh, 0, buttons }; push_event(w, &e);
                ++w->gen; mark_window(ui, ri);
            }
        }
    }
    if (!(buttons & 1u)) { ui->drag_win = -1; ui->resize_win = -1; }
    { int hr = launcher_row(ui, ui->px, ui->py); if (hr != ui->launcher_hover) { ui->launcher_hover = hr; if (ui->launcher_open) mark_launcher(ui); } }
    int consumed = 0;
    if (pressed && ui->launcher_open) {
        int row = launcher_row(ui, ui->px, ui->py); int dd = dock_hit(ui, ui->px, ui->py);
        int litems[SAND_UI_WINDOWS + 1]; (void)dock_items(ui, litems);
        if (row >= 0 && ui->host.launch) { (void)ui->host.launch(launcher_apps[row].app); consumed = 1; }
        else if (dd >= 0 && litems[dd] < 0) consumed = 1;
        mark_launcher(ui); ui->launcher_open = 0;
    }
    if (pressed && !consumed) {
        int d = dock_hit(ui, ui->px, ui->py);
        if (d >= 0) {
            int items[SAND_UI_WINDOWS + 1]; (void)dock_items(ui, items); int wi = items[d];
            if (wi < 0) { ui->launcher_open = 1; ui->launcher_hover = -1; mark_launcher(ui); }
            else if (ui->win[wi].visible && top_window(ui) == wi) { ui->win[wi].visible = 0; mark_window(ui, wi); }
            else { ui->win[wi].visible = 1; raise_window(ui, wi); }
            mark_dock(ui);
        } else {
            int wi = hit_window(ui, ui->px, ui->py);
            if (wi >= 0) {
                struct sand_ui_window *w = &ui->win[wi];
                int32_t lx = ui->px - w->x, ly = ui->py - w->y;
                if (top_window(ui) != wi) raise_window(ui, wi);
                if (ly < TITLE_H) {
                    int k = -1;
                    for (int c = 0; c < 3; ++c) { int32_t dxc = ui->px - ctrl_cx(w, c), dyc = ui->py - (w->y + 18); if (dxc * dxc + dyc * dyc <= 144) k = c; }
                    if (k == CTRL_CLOSE || k == CTRL_MIN) {
                        if (k == CTRL_CLOSE && w->kind == 2) begin_close(ui, wi);
                        else { w->visible = 0; mark_window(ui, wi); mark(ui, 0, 0, ui->w, MENU_H + 1); }
                        ui->hover_ctrl = -1; mark_dock(ui);
                    } else if (k == CTRL_CENTER) {
                        mark_window(ui, wi);
                        w->x = (ui->w - w->w) / 2; w->y = imax(MENU_H + 8, (ui->h - 90 - w->h) / 2);
                        mark_window(ui, wi);
                    } else { ui->drag_win = wi; ui->drag_dx = lx; ui->drag_dy = ly; }
                } else if (w->kind == 2 && w->resizable && lx >= w->w - 24 && ly >= w->h - 24) {
                    ui->resize_win = wi; ui->rs_px = ui->px; ui->rs_py = ui->py; ui->rs_w0 = w->sw; ui->rs_h0 = w->sh;
                } else if (w->kind == 2) {
                    struct sand_event e = { SAND_EV_MOUSE_DOWN, lx, ly - TITLE_H, 0, buttons };
                    push_event(w, &e); ui->press_win = wi;
                }
            }
        }
    }
    if (ui->press_win >= 0) {
        struct sand_ui_window *w = &ui->win[ui->press_win];
        if (!w->alive) ui->press_win = -1;
        else if (released) { struct sand_event e = { SAND_EV_MOUSE_UP, ui->px - w->x, ui->py - w->y - TITLE_H, 0, buttons }; push_event(w, &e); ui->press_win = -1; }
        else if (moved && (buttons & 1u) && !pressed) { struct sand_event e = { SAND_EV_MOUSE_MOVE, ui->px - w->x, ui->py - w->y - TITLE_H, 0, buttons }; push_event(w, &e); }
    }
    ui->buttons = buttons;
}

/* ---- frame ---- */
int sand_ui_frame(sand_ui_t *ui, struct sand_ui_rect *out) {
    uint32_t t = ui->host.ticks();
    if (ui->term_gen != ui->term_seen) { ui->term_seen = ui->term_gen; if (ui->win[0].visible) mark(ui, ui->win[0].x, ui->win[0].y + TITLE_H, ui->win[0].x + ui->win[0].w, ui->win[0].y + ui->win[0].h); }
    if (ui->win[0].visible && ui->blink_seen != t / 50u) {
        ui->blink_seen = t / 50u;
        mark(ui, ui->win[0].x + TERM_PAD + ui->term_col * CELL_W, ui->win[0].y + TITLE_H + TERM_PAD + ui->term_row * CELL_H,
             ui->win[0].x + TERM_PAD + (ui->term_col + 1) * CELL_W, ui->win[0].y + TITLE_H + TERM_PAD + (ui->term_row + 1) * CELL_H + 2);
    }
    if (ui->win[1].visible && ui->about_seen != t / 50u) { ui->about_seen = t / 50u; mark(ui, ui->win[1].x, ui->win[1].y + TITLE_H, ui->win[1].x + ui->win[1].w, ui->win[1].y + ui->win[1].h); }
    if (ui->clock_min_seen != t / 500u) { ui->clock_min_seen = t / 500u; mark(ui, ui->w - 120, 0, ui->w, MENU_H); }
    if (ui->liveness_seen != t / 25u) {
        ui->liveness_seen = t / 25u;
        for (int i = SAND_UI_SHELL_WINDOWS; i < SAND_UI_WINDOWS; ++i)
            if (ui->win[i].alive && ui->host.pid_alive && !ui->host.pid_alive(ui->win[i].owner)) destroy_app_window(ui, i);
    }
    for (int i = SAND_UI_SHELL_WINDOWS; i < SAND_UI_WINDOWS; ++i) {      /* open / close animation clock */
        struct sand_ui_window *w = &ui->win[i];
        if (!w->alive || !w->anim) continue;
        uint32_t dt = t - w->anim_t0; uint32_t lin = dt >= ANIM_TICKS ? 255u : dt * 255u / ANIM_TICKS;
        uint32_t e = lin * (510u - lin) / 255u;                           /* ease-out */
        if (w->anim == 1) { w->anim_a = e; if (dt >= ANIM_TICKS) { w->anim = 0; w->anim_a = 255u; } }
        else { w->anim_a = 255u - e; if (dt >= ANIM_TICKS) { destroy_app_window(ui, i); continue; } }
        mark_window(ui, i);
    }
    for (int i = SAND_UI_SHELL_WINDOWS; i < SAND_UI_WINDOWS; ++i) {
        struct sand_ui_window *w = &ui->win[i];
        if (w->alive && w->visible && w->gen != w->seen_gen) { w->seen_gen = w->gen; mark(ui, w->x, w->y + TITLE_H, w->x + w->w, w->y + w->h); }
    }
    struct sand_ui_rect present = ui->dirty;
    struct sand_ui_rect nb = { ui->px - 2, ui->py - 2, ui->px + 18, ui->py + 26 };
    if (ui->cursor_moved) { rect_union(&present, ui->cursor_box.x0, ui->cursor_box.y0, ui->cursor_box.x1, ui->cursor_box.y1); rect_union(&present, nb.x0, nb.y0, nb.x1, nb.y1); }
    present.x0 = imax(present.x0, 0); present.y0 = imax(present.y0, 0); present.x1 = imin(present.x1, ui->w); present.y1 = imin(present.y1, ui->h);
    ui->cursor_box = nb; ui->cursor_moved = 0;
    if (present.x0 >= present.x1 || present.y0 >= present.y1) return 0;
    struct sand_ui_rect d = present;
    ui->dirty = (struct sand_ui_rect){ 0, 0, 0, 0 };
    gfx_set_clip(&ui->g, d.x0, d.y0, d.x1 - d.x0, d.y1 - d.y0);
    gfx_t wg; gfx_init(&wg, ui->wall, ui->w, ui->h);
    gfx_copy(&ui->g, &wg, d.x0, d.y0, d.x1 - d.x0, d.y1 - d.y0);
    int top = top_window(ui);
    for (int k = 0; k < SAND_UI_WINDOWS; ++k) {
        int i = ui->order[k]; if (!ui->win[i].visible) continue;
        int focused = (i == top);
        struct sand_ui_window *aw = &ui->win[i];
        int fading = aw->kind == 2 && aw->anim && aw->anim_a < 255u;
        struct sand_ui_rect fr = { 0, 0, 0, 0 }; int32_t off = 0;
        if (fading) {
            off = (int32_t)((255u - aw->anim_a) * 14u / 255u);
            if (!ui->scratch) ui->scratch = ui->host.alloc((uint32_t)(ANIM_SCRATCH_W * ANIM_SCRATCH_H) * 4u);
            fr.x0 = imax(d.x0, aw->x - 56); fr.y0 = imax(d.y0, aw->y - 56); fr.x1 = imin(d.x1, aw->x + aw->w + 56); fr.y1 = imin(d.y1, aw->y + aw->h + 72);
            if (!ui->scratch || fr.x1 <= fr.x0 || fr.y1 <= fr.y0 || (fr.x1 - fr.x0) > ANIM_SCRATCH_W || (fr.y1 - fr.y0) > ANIM_SCRATCH_H) fading = 0;
            else {
                for (int32_t yy = fr.y0; yy < fr.y1; ++yy) for (int32_t xx = fr.x0; xx < fr.x1; ++xx) ui->scratch[(yy - fr.y0) * (fr.x1 - fr.x0) + (xx - fr.x0)] = ui->back[yy * ui->w + xx];
                aw->y += off;
            }
        }
        draw_frame(ui, i, focused);
        gfx_set_clip(&ui->g, imax(d.x0, ui->win[i].x), imax(d.y0, ui->win[i].y + TITLE_H), imin(d.x1, ui->win[i].x + ui->win[i].w) - imax(d.x0, ui->win[i].x),
                     imin(d.y1, ui->win[i].y + ui->win[i].h) - imax(d.y0, ui->win[i].y + TITLE_H));
        if (i == 0) draw_console(ui, focused); else if (i == 1) draw_about(ui); else blit_surface(ui, i);
        if (aw->kind == 2 && aw->resizable) {                                  /* dune-grip resize handle */
            int32_t cx = aw->x + aw->w - 7, cy = aw->y + aw->h - 7;
            for (int k = 0; k < 3; ++k) { int32_t o = 5 + k * 5; gfx_line(&ui->g, cx - o, cy, cx, cy - o, 24, C_CHAMP, focused ? 150u : 70u); }
        }
        gfx_set_clip(&ui->g, d.x0, d.y0, d.x1 - d.x0, d.y1 - d.y0);
        if (fading) {
            aw->y -= off;
            for (int32_t yy = fr.y0; yy < fr.y1; ++yy) for (int32_t xx = fr.x0; xx < fr.x1; ++xx) {
                uint32_t *px = &ui->back[yy * ui->w + xx];
                *px = gfx_mix(ui->scratch[(yy - fr.y0) * (fr.x1 - fr.x0) + (xx - fr.x0)], *px, aw->anim_a);
            }
        }
    }
    draw_menubar(ui);
    draw_dock(ui);
    draw_launcher(ui);
    gfx_set_clip(&ui->g, 0, 0, ui->w, ui->h);
    ++ui->frames;
    *out = present;
    return 1;
}

void sand_ui_present(sand_ui_t *ui, volatile uint8_t *fb, uint32_t pitch, uint32_t bpp, const struct sand_ui_rect *r) {
    for (int32_t y = r->y0; y < r->y1; ++y) {
        const uint32_t *src = ui->back + y * ui->w;
        volatile uint8_t *dst = fb + (uint32_t)y * pitch + (uint32_t)r->x0 * (bpp / 8u);
        for (int32_t x = r->x0; x < r->x1; ++x) {
            uint32_t c = src[x];
            int32_t cx = x - (ui->px - 1), cy = y - (ui->py - 1);
            if (cx >= 0 && cx < 16 && cy >= 0 && cy < 24) {
                c = gfx_mix(c, 0x14102Eu, ui->cur_edge[cy * 16 + cx]);
                c = gfx_mix(c, 0xFFFFFFu, ui->cur_fill[cy * 16 + cx]);
            }
            if (bpp == 32u) { *(volatile uint32_t *)dst = c; dst += 4; }
            else { dst[0] = (uint8_t)c; dst[1] = (uint8_t)(c >> 8); dst[2] = (uint8_t)(c >> 16); dst += 3; }
        }
    }
}
void sand_ui_frame_pixels(const sand_ui_t *ui, const uint32_t **pixels) { *pixels = ui->back; }
