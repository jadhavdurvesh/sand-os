#ifndef SAND_GRAPHICS_FONT_H
#define SAND_GRAPHICS_FONT_H
#include <stdint.h>

/* SAND Sans: geometric stroke typeface (see tools/genfont.py). Outlines live in
 * generated read-only tables; faces are rasterised once at start-up into small
 * 8-bit coverage bitmaps (two sub-pixel phases) for any size and pen weight. */
struct sand_font_seg { int16_t x0, y0, x1, y1; };
struct sand_font_glyph { uint16_t first; uint16_t count; uint16_t advance; };
extern const struct sand_font_seg sand_font_segments[];
extern const uint32_t sand_font_segment_count;
extern const struct sand_font_glyph sand_font_prop[95];
extern const struct sand_font_glyph sand_font_mono[95];

#define SAND_FONT_PHASES 2u
struct sand_glyph_bitmap { uint8_t *bits; int8_t left; int8_t top; uint8_t width; uint8_t height; };
typedef struct {
    uint8_t px;                 /* em size in pixels */
    uint16_t weight;            /* pen width in 1/1000 em (light 54, regular 84, medium 104) */
    uint8_t mono;
    int8_t ascent, descent;     /* pixels above / below the baseline */
    uint8_t line_height;
    uint16_t advance64[95];     /* pen advance in 1/64 px */
    struct sand_glyph_bitmap glyph[95][SAND_FONT_PHASES];
} sand_font_t;

typedef void *(*sand_alloc_fn)(uint32_t size);
int font_face_init(sand_font_t *face, uint32_t px, uint32_t weight, int mono, sand_alloc_fn alloc);
uint32_t font_text_width64(const sand_font_t *face, const char *text, int32_t tracking64);  /* 1/64 px */
uint32_t font_text_width(const sand_font_t *face, const char *text);                        /* px */
/* Shared helper (also used by the shape code): distance from point to segment, any
 * consistent integer unit; safe while coordinates stay below ~20000 units. */
int32_t sand_dist_segment(int32_t px, int32_t py, int32_t ax, int32_t ay, int32_t bx, int32_t by);
uint32_t sand_isqrt(uint32_t value);
#endif
