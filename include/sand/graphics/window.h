#ifndef SAND_GRAPHICS_WINDOW_H
#define SAND_GRAPHICS_WINDOW_H
#include <stdint.h>
#include "sand/graphics/framebuffer.h"
#define SAND_WINDOW_MAX 8u
typedef uint32_t sand_window_id_t;
typedef struct {
    uint8_t used,visible,focused;
    int32_t x,y;
    uint32_t width,height;
    char title[32];
} sand_window_t;
void window_manager_init(void);
sand_window_id_t window_create(const char *title,int32_t x,int32_t y,uint32_t width,uint32_t height);
int window_close(sand_window_id_t id);
void window_manager_pointer(int32_t x,int32_t y,uint8_t buttons);
void window_manager_render(sand_framebuffer_t *fb);
int window_get(sand_window_id_t id,sand_window_t *out);
#endif
