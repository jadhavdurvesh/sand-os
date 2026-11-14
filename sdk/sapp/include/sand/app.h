#ifndef SAND_APP_H
#define SAND_APP_H
#include <stdint.h>
#include "sand/abi.h"

#if defined(__i386__)
static inline int32_t sand_syscall0(uint32_t number)
{
    int32_t result;
    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(number) : "memory");
    return result;
}
static inline int32_t sand_syscall1(uint32_t number, uint32_t a)
{
    int32_t result;
    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(number), "b"(a) : "memory");
    return result;
}
static inline int32_t sand_syscall2(uint32_t number, uint32_t a, uint32_t b)
{
    int32_t result;
    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(number), "b"(a), "c"(b) : "memory");
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
static inline uint32_t sand_ticks(void) { return (uint32_t)sand_syscall0(SAND_SYS_GET_TICKS); }
static inline int32_t sand_getpid(void) { return sand_syscall0(SAND_SYS_GETPID); }
static inline void sand_yield(void) { (void)sand_syscall0(SAND_SYS_YIELD); }
static inline void sand_sleep_ms(uint32_t ms) { (void)sand_syscall1(SAND_SYS_SLEEP_MS, ms); }
static inline int32_t sand_read(void *buf, uint32_t cap) { return sand_syscall2(SAND_SYS_READ, (uint32_t)buf, cap); }
static inline int32_t sand_fs_read(const char *name, void *buf, uint32_t cap) { return sand_syscall3(SAND_SYS_FS_READ, (uint32_t)name, (uint32_t)buf, cap); }
static inline int32_t sand_fs_write(const char *name, const void *buf, uint32_t len) { return sand_syscall3(SAND_SYS_FS_WRITE, (uint32_t)name, (uint32_t)buf, len); }
static inline int32_t sand_fs_list(uint32_t index, struct sand_dirent *out) { return sand_syscall2(SAND_SYS_FS_LIST, index, (uint32_t)out); }
static inline int32_t sand_fs_remove(const char *name) { return sand_syscall1(SAND_SYS_FS_REMOVE, (uint32_t)name); }
static inline int32_t sand_spawn(const char *app) { return sand_syscall1(SAND_SYS_SPAWN, (uint32_t)app); }
static inline int32_t sand_wait(int32_t pid, int32_t *status) { return sand_syscall2(SAND_SYS_WAIT, (uint32_t)pid, (uint32_t)status); }
static inline int32_t sand_sysinfo(struct sand_sysinfo *out) { return sand_syscall1(SAND_SYS_SYSINFO, (uint32_t)out); }
static inline int32_t sand_proc_list(uint32_t index, struct sand_procinfo *out) { return sand_syscall2(SAND_SYS_PROC_LIST, index, (uint32_t)out); }
static inline int32_t sand_app_list(uint32_t index, struct sand_dirent *out) { return sand_syscall2(SAND_SYS_APP_LIST, index, (uint32_t)out); }
static inline int32_t sand_win_create(const char *title, int32_t w, int32_t h) { return sand_syscall2(SAND_SYS_WIN_CREATE, (uint32_t)title, ((uint32_t)w << 16) | (uint32_t)h); }
static inline int32_t sand_win_draw(int32_t id, const struct sand_draw *cmds, uint32_t count) { return sand_syscall3(SAND_SYS_WIN_DRAW, (uint32_t)id, (uint32_t)cmds, count); }
static inline int32_t sand_win_event(int32_t id, struct sand_event *ev) { return sand_syscall2(SAND_SYS_WIN_EVENT, (uint32_t)id, (uint32_t)ev); }
static inline int32_t sand_win_close(int32_t id) { return sand_syscall1(SAND_SYS_WIN_CLOSE, (uint32_t)id); }
static inline int32_t sand_win_blit(int32_t id, const struct sand_blit *b) { return sand_syscall2(SAND_SYS_WIN_BLIT, (uint32_t)id, (uint32_t)b); }
static inline int32_t sand_setting_get(uint32_t key) { return sand_syscall1(SAND_SYS_SETTING_GET, key); }
static inline int32_t sand_setting_set(uint32_t key, uint32_t value) { return sand_syscall2(SAND_SYS_SETTING_SET, key, value); }
static inline int32_t sand_win_resizable(int32_t id, int32_t min_w, int32_t min_h) { return sand_syscall2(SAND_SYS_WIN_RESIZABLE, (uint32_t)id, ((uint32_t)min_w << 16) | (uint32_t)min_h); }
static inline int32_t sand_device_list(uint32_t index, struct sand_devinfo *out) { return sand_syscall2(SAND_SYS_DEVICE_LIST, index, (uint32_t)out); }
#endif
