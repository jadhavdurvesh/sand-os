#ifndef SAND_GRAPHICS_DAWN_H
#define SAND_GRAPHICS_DAWN_H
#include <stdint.h>
#include "sand/graphics/framebuffer.h"
#include "sand/graphics/gfx.h"
/* DAWN: SAND's boot splash. A night sky over the dunes; the moon rises and the SAND wordmark fades in as boot stages
 * complete, then the desktop is revealed by an iris that opens from the moon. Everything is drawn with the kernel's own
 * 2D renderer (no image files). */
#define DAWN_STAGES 6u
/* Pure scene renderer (also used by the host preview and test). progress: 0..256, frame: animation counter. */
void dawn_render(gfx_t *g, const sand_font_t *word, const sand_font_t *small, uint32_t progress256, uint32_t frame, const char *label);
void dawn_moon(int32_t w, int32_t h, uint32_t progress256, int32_t *cx, int32_t *cy);
#ifndef DAWN_HOST
int dawn_begin(const sand_framebuffer_t *fb);        /* 0 when the splash is showing; any failure just means no splash */
void dawn_stage(uint32_t stage, const char *label);  /* stage 1..DAWN_STAGES: animate the progress to stage/DAWN_STAGES */
void *dawn_take_buffer(uint32_t bytes);              /* hands the splash back-buffer to the desktop (same size) so the heap is not charged twice */
void dawn_reveal(const uint32_t *desktop);           /* iris from the moon over the splash already on screen; ends the splash */
int dawn_active(void);
#endif
#endif
