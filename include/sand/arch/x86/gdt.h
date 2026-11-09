#ifndef SAND_ARCH_X86_GDT_H
#define SAND_ARCH_X86_GDT_H
#include <stdint.h>
void gdt_init(void);
void tss_set_kernel_stack(uint32_t stack_top);
#endif
