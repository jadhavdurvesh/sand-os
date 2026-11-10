#include "sand/graphics/desktop.h"

int sand_desktop_render(sand_framebuffer_t *fb) {
    if (!graphics_framebuffer_valid(fb) || fb->width < 320u || fb->height < 200u) return -1;
    (void)graphics_fill(fb, 0x101827u);
    (void)graphics_fill_rect(fb, 0, 0, fb->width, 42u, 0x080E18u);
    (void)graphics_fill_rect(fb, 24, 66, fb->width - 48u, fb->height - 112u, 0x172438u);
    (void)graphics_fill_rect(fb, 40, 84, fb->width - 80u, 2u, 0x2B4058u);
    (void)graphics_fill_rect(fb, 40, 104, 9u, 9u, 0x4DE0C1u);
    (void)graphics_fill_rect(fb, 56, 104, 9u, 9u, 0x4DE0C1u);
    (void)graphics_fill_rect(fb, 72, 104, 9u, 9u, 0x4DE0C1u);
    (void)graphics_fill_rect(fb, 40, 132, fb->width / 3u, 72u, 0x20334Au);
    (void)graphics_fill_rect(fb, 40, 220, fb->width / 3u, 72u, 0x20334Au);
    (void)graphics_fill_rect(fb, fb->width / 3u + 64u, 132, fb->width / 3u, 160u, 0x20334Au);
    (void)graphics_fill_rect(fb, 40, fb->height - 34u, fb->width - 80u, 2u, 0x33465Du);
    (void)graphics_fill_rect(fb, 0, fb->height - 26u, fb->width, 26u, 0x080E18u);
    (void)graphics_fill_rect(fb, 18, fb->height - 20u, 14u, 14u, 0x4DE0C1u);
    (void)graphics_fill_rect(fb, 42, fb->height - 17u, 76u, 7u, 0x34465Bu);
    (void)graphics_fill_rect(fb, fb->width - 112u, fb->height - 17u, 92u, 7u, 0x34465Bu);
    return 0;
}
