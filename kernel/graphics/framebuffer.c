#include "sand/graphics/framebuffer.h"

int graphics_framebuffer_valid(const sand_framebuffer_t *fb) {
    if (fb == 0 || fb->pixels == 0) return 0;
    if (fb->width == 0u || fb->height == 0u) return 0;
    if (fb->bits_per_pixel != 24u && fb->bits_per_pixel != 32u) return 0;
    uint32_t bytes_per_pixel = fb->bits_per_pixel / 8u;
    if (fb->pitch_bytes < fb->width * bytes_per_pixel) return 0;
    return 1;
}

int graphics_put_pixel(sand_framebuffer_t *fb, int32_t x, int32_t y, uint32_t rgb) {
    if (!graphics_framebuffer_valid(fb)) return -1;
    if (x < 0 || y < 0 || (uint32_t)x >= fb->width || (uint32_t)y >= fb->height) return -1;
    uint32_t bytes_per_pixel = fb->bits_per_pixel / 8u;
    volatile uint8_t *pixel = fb->pixels + (uint32_t)y * fb->pitch_bytes + (uint32_t)x * bytes_per_pixel;
    pixel[0] = (uint8_t)rgb;
    pixel[1] = (uint8_t)(rgb >> 8);
    pixel[2] = (uint8_t)(rgb >> 16);
    if (bytes_per_pixel == 4u) pixel[3] = 0u;
    return 0;
}

int graphics_fill(sand_framebuffer_t *fb, uint32_t rgb) {
    if (!graphics_framebuffer_valid(fb)) return -1;
    for (uint32_t y = 0; y < fb->height; ++y)
        for (uint32_t x = 0; x < fb->width; ++x)
            (void)graphics_put_pixel(fb, (int32_t)x, (int32_t)y, rgb);
    return 0;
}

int graphics_fill_rect(sand_framebuffer_t *fb, int32_t x, int32_t y,
                       uint32_t width, uint32_t height, uint32_t rgb) {
    if (!graphics_framebuffer_valid(fb)) return -1;
    if (width == 0u || height == 0u || x < 0 || y < 0) return -1;
    if ((uint32_t)x >= fb->width || (uint32_t)y >= fb->height) return -1;
    uint32_t x_end = (uint32_t)x + width;
    uint32_t y_end = (uint32_t)y + height;
    if (x_end < (uint32_t)x || y_end < (uint32_t)y) return -1;
    if (x_end > fb->width) x_end = fb->width;
    if (y_end > fb->height) y_end = fb->height;
    for (uint32_t row = (uint32_t)y; row < y_end; ++row)
        for (uint32_t col = (uint32_t)x; col < x_end; ++col)
            (void)graphics_put_pixel(fb, (int32_t)col, (int32_t)row, rgb);
    return 0;
}

int graphics_draw_line(sand_framebuffer_t *fb, int32_t x0, int32_t y0,
                       int32_t x1, int32_t y1, uint32_t rgb) {
    if (!graphics_framebuffer_valid(fb)) return -1;
    int32_t dx = x1 >= x0 ? x1 - x0 : x0 - x1;
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t dy_abs = y1 >= y0 ? y1 - y0 : y0 - y1;
    int32_t dy = -dy_abs;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t error = dx + dy;
    for (;;) {
        if (x0 >= 0 && y0 >= 0 && (uint32_t)x0 < fb->width && (uint32_t)y0 < fb->height)
            (void)graphics_put_pixel(fb, x0, y0, rgb);
        if (x0 == x1 && y0 == y1) break;
        int32_t twice = 2 * error;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
    return 0;
}
