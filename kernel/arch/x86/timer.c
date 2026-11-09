#include "sand/arch/x86/timer.h"
#include "sand/arch/x86/idt.h"
#include "sand/kernel/scheduler.h"
#define PIT_INPUT_HZ 1193182u
#define PIC1_COMMAND 0x20u
#define PIC1_DATA 0x21u
static volatile uint32_t ticks;
extern void x86_irq0(void);
static inline void outb(uint16_t port, uint8_t value) { __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port)); }
static inline uint8_t inb(uint16_t port) { uint8_t value; __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port)); return value; }
uint32_t timer_irq_handler(uint32_t saved_stack) {
    ++ticks;
    uint32_t next_stack = scheduler_tick(saved_stack);
    outb(PIC1_COMMAND, 0x20u);
    return next_stack;
}
void timer_init(uint32_t frequency_hz) {
    if (frequency_hz == 0u || frequency_hz > PIT_INPUT_HZ) return;
    uint32_t divisor = PIT_INPUT_HZ / frequency_hz;
    if (divisor == 0u || divisor > 65535u) return;
    ticks = 0u;
    idt_set_irq0(x86_irq0);
    idt_load();
    outb(0x43u, 0x36u);
    outb(0x40u, (uint8_t)divisor);
    outb(0x40u, (uint8_t)(divisor >> 8));
    uint8_t mask = inb(PIC1_DATA);
    outb(PIC1_DATA, (uint8_t)(mask & (uint8_t)~0x01u));
}
uint32_t timer_ticks(void) { return ticks; }
