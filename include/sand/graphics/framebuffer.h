#ifndef SAND_GRAPHICS_FRAMEBUFFER_H
#define SAND_GRAPHICS_FRAMEBUFFER_H
#include <stdint.h>

/* SAND's first software framebuffer contract.
 * pixels is a kernel-virtual address supplied by a platform display driver.
 * Pixels use 0x00RRGGBB and rows may contain pitch_bytes / 4 pixels.
 */
typedef struct {
    volatile uint8_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch_bytes;
    uint8_t bits_per_pixel;
} sand_framebuffer_t;

int graphics_framebuffer_valid(const sand_framebuffer_t *fb);
int graphics_put_pixel(sand_framebuffer_t *fb, int32_t x, int32_t y, uint32_t rgb);
int graphics_fill(sand_framebuffer_t *fb, uint32_t rgb);
int graphics_fill_rect(sand_framebuffer_t *fb, int32_t x, int32_t y,
                       uint32_t width, uint32_t height, uint32_t rgb);
int graphics_draw_line(sand_framebuffer_t *fb, int32_t x0, int32_t y0,
                       int32_t x1, int32_t y1, uint32_t rgb);
#endif
