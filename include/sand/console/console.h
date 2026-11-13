#ifndef SAND_CONSOLE_CONSOLE_H
#define SAND_CONSOLE_CONSOLE_H
#include <stdint.h>

/* SAND console: one output stream fanned out to registered sinks (serial
 * port, graphical terminal window, test capture) and one input queue fed by
 * the keyboard and serial drivers. */
typedef void (*sand_console_sink)(const char *data, uint32_t length);
#define SAND_CONSOLE_SINK_MAX 4u

int console_add_sink(sand_console_sink sink);
void console_write(const char *data, uint32_t length);
void console_puts(const char *text);

/* Input: producers may call from interrupt context. */
void console_input_push(uint8_t byte);
uint32_t console_input_read(uint8_t *destination, uint32_t capacity);
uint32_t console_input_pending(void);
const void *console_input_wait_object(void);
void console_input_flush(void);

/* Test support: record everything written while capture is active. */
void console_capture_start(char *buffer, uint32_t capacity);
void console_capture_stop(void);
uint32_t console_capture_length(void);
int console_capture_contains(const char *needle);
#endif
