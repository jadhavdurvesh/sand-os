#ifndef SAND_GRAPHICS_GFX_H
#define SAND_GRAPHICS_GFX_H
#include <stdint.h>
#include "sand/graphics/font.h"

/* SAND 2D software renderer. Surface pixels are 0x00RRGGBB. Every primitive honours
 * the surface clip rectangle, so the compositor can redraw only damaged regions.
 * Shapes are anti-aliased with analytic coverage (no supersampling). */
typedef struct {
    uint32_t *px;
    int32_t w, h, stride;           /* stride in pixels */
    int32_t cx0, cy0, cx1, cy1;     /* clip: [cx0,cx1) x [cy0,cy1) */
} gfx_t;

#define GFX_RGB(r, g, b) ((((uint32_t)(r)) << 16) | (((uint32_t)(g)) << 8) | (uint32_t)(b))
void gfx_init(gfx_t *g, uint32_t *pixels, int32_t w, int32_t h);
void gfx_set_clip(gfx_t *g, int32_t x, int32_t y, int32_t w, int32_t h);
int gfx_visible(const gfx_t *g, int32_t x, int32_t y, int32_t w, int32_t h);
uint32_t gfx_mix(uint32_t dst, uint32_t src, uint32_t alpha);

void gfx_rect(gfx_t *g, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color, uint32_t alpha);
void gfx_vgradient(gfx_t *g, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t top, uint32_t bottom, uint32_t alpha);
void gfx_rrect(gfx_t *g, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color, uint32_t alpha);
/* Rounded rectangle with a vertical colour gradient. */
void gfx_rrect_gradient(gfx_t *g, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t top, uint32_t bottom, uint32_t alpha);
void gfx_rrect_ring(gfx_t *g, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, int32_t thickness, uint32_t color, uint32_t alpha);
void gfx_circle(gfx_t *g, int32_t cx, int32_t cy, int32_t r, uint32_t color, uint32_t alpha);
void gfx_line(gfx_t *g, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t width16, uint32_t color, uint32_t alpha);
/* Soft drop shadow outside a rounded rectangle (interior is left untouched). */
void gfx_shadow(gfx_t *g, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, int32_t blur, int32_t offset_y, uint32_t color, uint32_t alpha);

/* Text: (x, baseline_y). tracking64 adds letter-spacing in 1/64 px. */
int32_t gfx_text(gfx_t *g, const sand_font_t *face, int32_t x, int32_t baseline, const char *text, uint32_t color, uint32_t alpha, int32_t tracking64);
int32_t gfx_text_right(gfx_t *g, const sand_font_t *face, int32_t right, int32_t baseline, const char *text, uint32_t color, uint32_t alpha);
int32_t gfx_text_center(gfx_t *g, const sand_font_t *face, int32_t cx, int32_t baseline, const char *text, uint32_t color, uint32_t alpha);
void gfx_glyph(gfx_t *g, const sand_font_t *face, int32_t x, int32_t baseline, char ch, uint32_t color, uint32_t alpha);
void gfx_copy(gfx_t *dst, const gfx_t *src, int32_t x, int32_t y, int32_t w, int32_t h);   /* same-coordinate blit */
#endif
