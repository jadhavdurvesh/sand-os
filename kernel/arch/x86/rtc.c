#include <stdint.h>
#include "sand/arch/x86/rtc.h"

static inline void outb(uint16_t p, uint8_t v) { __asm__ volatile("outb %0,%1" : : "a"(v), "Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static uint8_t cmos(uint8_t reg) { outb(0x70, reg); return inb(0x71); }
static int updating(void) { return (cmos(0x0A) & 0x80u) != 0u; }

int rtc_read_time(int *hour, int *minute, int *second) {
    uint8_t h, m, s, h2, m2, s2;
    for (uint32_t guard = 0; guard < 100000u && updating(); ++guard) { }
    do {                                   /* re-read until two samples agree */
        h = cmos(4); m = cmos(2); s = cmos(0);
        for (uint32_t guard = 0; guard < 100000u && updating(); ++guard) { }
        h2 = cmos(4); m2 = cmos(2); s2 = cmos(0);
    } while (h != h2 || m != m2 || s != s2);
    uint8_t b = cmos(0x0B);
    int pm = 0;
    if (!(b & 2u) && (h & 0x80u)) { pm = 1; h &= 0x7Fu; }
    if (!(b & 4u)) { h = (uint8_t)((h & 15u) + (h >> 4) * 10u); m = (uint8_t)((m & 15u) + (m >> 4) * 10u); s = (uint8_t)((s & 15u) + (s >> 4) * 10u); }
    if (!(b & 2u)) { if (pm) h = (uint8_t)((h % 12u) + 12u); else if (h == 12u) h = 0u; }
    if (h > 23u || m > 59u || s > 59u) return -1;
    *hour = h; *minute = m; if (second) *second = s;
    return 0;
}

static uint8_t bcd_fix(uint8_t v, uint8_t b) { return (b & 4u) ? v : (uint8_t)((v & 15u) + (v >> 4) * 10u); }
int rtc_read_iso(char out[20]) {
    int hh, mm, ss;
    if (rtc_read_time(&hh, &mm, &ss) != 0) return -1;
    for (uint32_t guard = 0; guard < 100000u && updating(); ++guard) { }
    uint8_t b = cmos(0x0B);
    uint32_t day = bcd_fix(cmos(7), b), mon = bcd_fix(cmos(8), b), yr = bcd_fix(cmos(9), b), cen = bcd_fix(cmos(0x32), b);
    uint32_t year = (cen >= 19u && cen <= 30u ? cen : 20u) * 100u + yr;
    if (mon < 1u || mon > 12u || day < 1u || day > 31u) return -1;
    uint32_t v[6] = { year, mon, day, (uint32_t)hh, (uint32_t)mm, (uint32_t)ss };
    static const uint8_t width[6] = { 4, 2, 2, 2, 2, 2 }; static const char sep[6] = { '-', '-', 'T', ':', ':', 0 };
    int o = 0;
    for (int i = 0; i < 6; ++i) { for (int d = width[i] - 1; d >= 0; --d) { uint32_t p = 1; for (int k = 0; k < d; ++k) p *= 10u; out[o++] = (char)('0' + (v[i] / p) % 10u); } if (sep[i]) out[o++] = sep[i]; }
    out[o] = 0;
    return 0;
}
