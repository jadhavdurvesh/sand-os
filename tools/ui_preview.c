/* Host-side preview: renders one SAND Dusk frame to a PPM (see `make ui-preview`). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sand/graphics/ui.h"
static void *h_alloc(uint32_t n) { return calloc(1, n); }
static uint32_t h_ticks(void) { return 123456u; }
static int h_clock(int *h, int *m) { *h = 14; *m = 41; return 0; }
static void h_sys(struct sand_ui_sysinfo *s) { s->ticks = 123456; s->tick_hz = 100; s->free_frames = 3700; s->total_frames = 4096; s->heap_used = 2400000; s->heap_total = 8388608; s->switches = 48213; s->processes = 2; }
static uint32_t h_pid(void) { return 7u; }
static int h_alive(uint32_t pid) { (void)pid; return 1; }
int main(int argc, char **argv) {
    sand_ui_host_t host = { h_alloc, h_ticks, h_clock, h_sys, h_pid, h_alive };
    static sand_ui_t ui;
    if (sand_ui_init(&ui, &host, 1024, 768)) { fprintf(stderr, "init failed\n"); return 1; }
    const char *t = "\x1b[38;2;242;201;138mSAND\x1b[0m shell \x1b[2m- type help\x1b[0m\r\n\x1b[1;36msand\x1b[0m> ls\r\nhello.txt   18\r\nnotes.md    204\r\n\x1b[1;36msand\x1b[0m> ";
    sand_ui_term_write(&ui, t, (uint32_t)strlen(t));
    sand_ui_pointer(&ui, -300, -200, 0);
    sand_ui_pointer(&ui, 0, 0, 0);
    int id = sand_ui_win_create(&ui, 7u, "LOCUS", 520, 360);
    struct sand_draw d[3] = { { .op = SAND_DRAW_CLEAR, .color = 0x15122Fu }, { .op = SAND_DRAW_RRECT, .x = 14, .y = 266, .w = 112, .h = 28, .r = 9, .aux = 1, .color = 0x7FD6B0u, .color2 = 0x3E8F78u },
                              { .op = SAND_DRAW_TEXT, .face = SAND_FACE_MEDIUM, .align = SAND_ALIGN_CENTER, .x = 70, .y = 285, .color = 0x07251Cu, .text = "New file" } };
    sand_ui_win_draw(&ui, id, 7u, d, 3);
    struct sand_ui_rect r; sand_ui_frame(&ui, &r);
    static uint8_t out[1024 * 768 * 4];
    sand_ui_present(&ui, out, 1024 * 4, 32, &(struct sand_ui_rect){ 0, 0, 1024, 768 });
    FILE *f = fopen(argc > 1 ? argv[1] : "ui.ppm", "wb"); fprintf(f, "P6\n1024 768\n255\n");
    for (int i = 0; i < 1024 * 768; ++i) { fputc(out[i * 4 + 2], f); fputc(out[i * 4 + 1], f); fputc(out[i * 4], f); }
    fclose(f); return 0;
}
