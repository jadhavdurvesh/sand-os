#ifndef SAND_DRIVERS_SERIAL_H
#define SAND_DRIVERS_SERIAL_H
#include <stdint.h>
/* 16550 UART on COM1 (I/O 0x3F8, IRQ4), 115200 8N1. */
int serial_init(void);              /* detect, self-test in loopback, enable RX IRQ */
int serial_loopback_test(void);     /* 0 when bytes round-trip through the UART */
void serial_write(const char *data, uint32_t length);  /* LF -> CRLF */
int serial_ready(void);
#endif
