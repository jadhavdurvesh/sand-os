#ifndef SAND_ARCH_X86_IDT_H
#define SAND_ARCH_X86_IDT_H
#include <stdint.h>
void idt_init(void);
void idt_load(void);
void idt_set_irq0(void (*handler)(void));
void idt_set_reschedule(void (*handler)(void));
void idt_set_syscall(void (*handler)(void));
/* Returns the stack pointer to resume (changes when a faulting user process
 * is terminated and another thread is scheduled in its place). */
uint32_t x86_exception_dispatch(uint32_t vector, uint32_t error_code, uint32_t eip, uint32_t cs, uint32_t frame);
/* Install a handler for hardware IRQ line 0-15 (remapped to vectors 32-47). */
void idt_set_irq(uint8_t irq, void (*handler)(void));
#endif
