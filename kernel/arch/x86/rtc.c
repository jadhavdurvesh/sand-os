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
