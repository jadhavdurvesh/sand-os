/* Host-side DAWN preview and check: renders splash frames to PPM files and verifies them (see `make dawn-preview`). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sand/graphics/dawn.h"
static void *h_alloc(uint32_t n) { return calloc(1, n); }
static uint32_t *frame_at(sand_font_t *word, sand_font_t *small, uint32_t p, const char *label, const char *path) {
    uint32_t *px = calloc(1024u * 768u, 4); gfx_t g; gfx_init(&g, px, 1024, 768);
    dawn_render(&g, word, small, p, 7u, label);
    FILE *f = fopen(path, "wb"); fprintf(f, "P6\n1024 768\n255\n");
    for (int i = 0; i < 1024 * 768; ++i) { fputc((px[i] >> 16) & 255, f); fputc((px[i] >> 8) & 255, f); fputc(px[i] & 255, f); }
    fclose(f); return px;
}
static uint32_t lum(uint32_t c) { return (((c >> 16) & 255u) * 3u + ((c >> 8) & 255u) * 6u + (c & 255u)) / 10u; }
int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    sand_font_t word, small; char path[256];
    if (font_face_init(&word, 46, 54, 0, h_alloc) || font_face_init(&small, 13, 84, 0, h_alloc)) { fprintf(stderr, "font init failed\n"); return 1; }
    static const uint32_t steps[4] = { 0, 85, 170, 256 }; uint32_t *f[4];
    for (int i = 0; i < 4; ++i) { snprintf(path, sizeof path, "%s/dawn%d.ppm", dir, i); f[i] = frame_at(&word, &small, steps[i], i == 3 ? "ready" : "storage", path); }
    int fails = 0;
    /* wordmark: bright pixels appear in its band once progress is high, and not at progress 0 */
    uint32_t bright0 = 0, bright3 = 0;
    for (int y = 190; y < 240; ++y) for (int x = 400; x < 624; ++x) { if (lum(f[0][y * 1024 + x]) > 200u) ++bright0; if (lum(f[3][y * 1024 + x]) > 200u) ++bright3; }
    if (bright0 != 0u || bright3 < 150u) { fprintf(stderr, "wordmark check failed (%u, %u)\n", bright0, bright3); ++fails; }
    /* progress bar fill grows */
    uint32_t fill[4];
    for (int i = 0; i < 4; ++i) { fill[i] = 0; for (int x = 402; x < 622; ++x) if (f[i][(768 * 93 / 100 + 2) * 1024 + x] == 0xF0B27Cu) ++fill[i]; }
    if (!(fill[0] < fill[1] && fill[1] < fill[2] && fill[2] < fill[3] && fill[3] > 200u)) { fprintf(stderr, "progress bar check failed %u %u %u %u\n", fill[0], fill[1], fill[2], fill[3]); ++fails; }
    /* the moon rises: its centre pixel is moon-coloured at the end, hidden behind the dunes at the start */
    int32_t mx, my; dawn_moon(1024, 768, 256u, &mx, &my);
    int32_t sx, sy; dawn_moon(1024, 768, 0u, &sx, &sy);
    if (!(my < sy) || lum(f[3][my * 1024 + mx]) < 200u) { fprintf(stderr, "moon check failed\n"); ++fails; }
    /* frames differ and are not blank */
    if (memcmp(f[0], f[3], 1024u * 768u * 4u) == 0) { fprintf(stderr, "frames identical\n"); ++fails; }
    puts(fails ? "dawn preview: FAILED" : "dawn preview: ok");
    return fails ? 1 : 0;
}
