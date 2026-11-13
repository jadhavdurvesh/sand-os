#ifndef SAND_LIBSAND_H
#define SAND_LIBSAND_H
#include <stdint.h>
#include "sand/app.h"
/* Minimal freestanding runtime for SAND applications. */
void *memset(void *dst, int value, uint32_t count);
void *memcpy(void *dst, const void *src, uint32_t count);
int memcmp(const void *a, const void *b, uint32_t count);
uint32_t strlen(const char *text);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, uint32_t count);
char *sand_utoa(uint32_t value, char *out);        /* writes decimal, returns end */
void sand_puts(const char *text);
void sand_putu(uint32_t value);
void sand_puti(int32_t value);
#endif
