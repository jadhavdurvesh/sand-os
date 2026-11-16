#ifndef CITADEL_SHIM_STRING_H
#define CITADEL_SHIM_STRING_H
/* Freestanding stand-in for <string.h>: the SAND runtime (sdk/sapp/lib/libsand.c) plus memchr/strcpy from shim.c. */
#include "sand/libsand.h"
void *memchr(const void *s, int c, uint32_t n);
char *strcpy(char *dst, const char *src);
#endif
