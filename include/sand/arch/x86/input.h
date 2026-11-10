#ifndef SAND_ARCH_X86_INPUT_H
#define SAND_ARCH_X86_INPUT_H
#include <stdint.h>
typedef enum { SAND_INPUT_NONE=0, SAND_INPUT_KEY=1, SAND_INPUT_POINTER=2 } sand_input_type_t;
typedef struct {
    sand_input_type_t type;
    uint16_t key;
    int16_t mouse_dx,mouse_dy;
    uint8_t buttons;
} sand_input_event_t;
int ps2_input_init(void);
int ps2_input_poll(sand_input_event_t *event);
#endif
