#ifndef SAND_APP_H
#define SAND_APP_H
#include <stdint.h>

/* SAND userspace ABI v0.1. Calls are intentionally centralized so the ABI
 * can evolve without applications embedding interrupt instructions. */
#define SAND_ABI_VERSION 1u

enum sand_syscall_number {
    SAND_SYS_EXIT = 1,
    SAND_SYS_WRITE = 2,
    SAND_SYS_GET_TICKS = 3
};

#if defined(__i386__)
static inline int32_t sand_syscall1(uint32_t number, uint32_t a)
{
    int32_t result;
    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(number), "b"(a) : "memory");
    return result;
}
static inline int32_t sand_syscall3(uint32_t number, uint32_t a,
                                    uint32_t b, uint32_t c)
{
    int32_t result;
    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(number),
                      "b"(a), "c"(b), "d"(c) : "memory");
    return result;
}
#else
#error "SAND applications currently target 32-bit x86"
#endif

static inline void sand_exit(int32_t status)
{
    (void)sand_syscall1(SAND_SYS_EXIT, (uint32_t)status);
    for (;;) { __asm__ volatile ("hlt"); }
}
static inline int32_t sand_write(const char *text, uint32_t length)
{
    return sand_syscall3(SAND_SYS_WRITE, (uint32_t)text, length, 0u);
}
#endif
