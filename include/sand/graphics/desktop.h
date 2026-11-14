#ifndef SAND_GRAPHICS_DESKTOP_H
#define SAND_GRAPHICS_DESKTOP_H
#include "sand/graphics/framebuffer.h"
#include "sand/abi.h"
/* Starts the SAND Dusk desktop: paints the first frame, attaches the system
 * console window to the console sinks and runs the compositor as a kernel thread. */
int sand_desktop_start(const sand_framebuffer_t *framebuffer);
uint32_t sand_desktop_frames(void);
/* Window service entry points used by the syscall layer. Return SAND_E_NOSYS when the
 * desktop is not running (no framebuffer). */
int sand_desktop_win_create(const char *title, int32_t w, int32_t h);
int sand_desktop_win_draw(int32_t id, const struct sand_draw *cmds, uint32_t count);
int sand_desktop_win_event(int32_t id, struct sand_event *out);
int sand_desktop_win_close(int32_t id);
int sand_desktop_win_blit(int32_t id, const struct sand_blit *b, const uint32_t *pixels);
int sand_desktop_win_resizable(int32_t id, int32_t min_w, int32_t min_h);
#endif
