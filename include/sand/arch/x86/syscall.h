#ifndef SAND_ARCH_X86_SYSCALL_H
#define SAND_ARCH_X86_SYSCALL_H
#include <stdint.h>
void syscall_init(void);
uint32_t syscall_dispatch(uint32_t saved_frame);
#endif
