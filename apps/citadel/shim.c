#include "string.h"
void *memchr(const void *s, int c, uint32_t n) {
    const unsigned char *p = (const unsigned char *)s;
    for (uint32_t i = 0; i < n; ++i) if (p[i] == (unsigned char)c) return (void *)(p + i);
    return 0;
}
char *strcpy(char *dst, const char *src) { char *d = dst; while ((*d++ = *src++)) { } return dst; }
