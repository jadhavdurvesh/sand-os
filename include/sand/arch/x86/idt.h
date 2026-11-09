#ifndef SAND_ARCH_X86_IDT_H
#define SAND_ARCH_X86_IDT_H
#include <stdint.h>
void idt_init(void);
void idt_load(void);
void idt_set_irq0(void (*handler)(void));
void idt_set_reschedule(void (*handler)(void));
void idt_set_syscall(void (*handler)(void));
void x86_exception_dispatch(uint32_t vector, uint32_t error_code);
#endif
