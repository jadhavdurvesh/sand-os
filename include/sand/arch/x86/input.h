#ifndef SAND_ARCH_X86_INPUT_H
#define SAND_ARCH_X86_INPUT_H
#include <stdint.h>

typedef enum { SAND_INPUT_NONE=0, SAND_INPUT_KEY=1, SAND_INPUT_POINTER=2 } sand_input_type_t;

/* Non-ASCII key codes delivered in sand_input_event_t.ascii (>= 0x80). */
#define SAND_KEY_UP     0x80u
#define SAND_KEY_DOWN   0x81u
#define SAND_KEY_LEFT   0x82u
#define SAND_KEY_RIGHT  0x83u
#define SAND_KEY_HOME   0x84u
#define SAND_KEY_END    0x85u
#define SAND_KEY_DELETE 0x86u
#define SAND_KEY_F1     0x90u   /* F1..F10 = 0x90..0x99 */

#define SAND_MOD_SHIFT 1u
#define SAND_MOD_CTRL  2u
#define SAND_MOD_ALT   4u
#define SAND_MOD_CAPS  8u

typedef struct {
    sand_input_type_t type;
    uint16_t key;        /* raw set-1 scancode (0x100 flag = E0 prefix)   */
    uint8_t ascii;       /* decoded character or SAND_KEY_* (key events)  */
    uint8_t pressed;     /* 1 = make, 0 = break                           */
    uint8_t modifiers;   /* SAND_MOD_* at the time of the event           */
    int16_t mouse_dx,mouse_dy;
    uint8_t buttons;     /* bit0 left, bit1 right, bit2 middle            */
} sand_input_event_t;

/* Interrupt-driven PS/2 controller, keyboard (IRQ1) and mouse (IRQ12). */
int ps2_input_init(void);
/* Non-blocking: 1 when an event was dequeued into *event, 0 when none. */
int ps2_input_poll(sand_input_event_t *event);
uint32_t ps2_input_dropped(void);
uint8_t ps2_input_modifiers(void);
int ps2_mouse_present(void);
/* 1 (default): ASCII key presses go straight to the console input queue.
 * 0: the desktop owns routing and delivers keys to the focused window. */
void ps2_input_set_console_forward(int enable);
/* Pure decoder, exposed for host-side unit tests: set-1 scancode to ASCII. */
uint8_t ps2_decode_scancode(uint8_t scancode, uint8_t modifiers);
#endif
