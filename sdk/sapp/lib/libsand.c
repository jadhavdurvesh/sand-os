#include "sand/libsand.h"
void *memset(void *dst, int value, uint32_t count)
{
    volatile uint8_t *d = (volatile uint8_t *)dst;
    for (uint32_t i = 0; i < count; ++i) d[i] = (uint8_t)value;
    return dst;
}
void *memcpy(void *dst, const void *src, uint32_t count)
{
    volatile uint8_t *d = (volatile uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < count; ++i) d[i] = s[i];
    return dst;
}
int memcmp(const void *a, const void *b, uint32_t count)
{
    const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;
    for (uint32_t i = 0; i < count; ++i)
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    return 0;
}
uint32_t strlen(const char *text)
{
    uint32_t n = 0;
    while (text[n]) ++n;
    return n;
}
int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}
int strncmp(const char *a, const char *b, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (a[i] != b[i]) return (int)(uint8_t)a[i] - (int)(uint8_t)b[i];
        if (a[i] == '\0') return 0;
    }
    return 0;
}
char *sand_utoa(uint32_t value, char *out)
{
    char tmp[10];
    uint32_t n = 0;
    do { tmp[n++] = (char)('0' + value % 10u); value /= 10u; } while (value);
    while (n) *out++ = tmp[--n];
    *out = '\0';
    return out;
}
void sand_puts(const char *text) { (void)sand_write(text, strlen(text)); }
void sand_putu(uint32_t value)
{
    char buf[12];
    char *end = sand_utoa(value, buf);
    (void)sand_write(buf, (uint32_t)(end - buf));
}
void sand_puti(int32_t value)
{
    if (value < 0) { sand_puts("-"); sand_putu((uint32_t)(-(value + 1)) + 1u); }
    else sand_putu((uint32_t)value);
}
