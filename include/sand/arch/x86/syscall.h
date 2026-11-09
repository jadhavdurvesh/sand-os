#ifndef SAND_ARCH_X86_SYSCALL_H
#define SAND_ARCH_X86_SYSCALL_H
#include <stdint.h>
void syscall_init(void);
void syscall_dispatch(uint32_t *registers);
#endif
