#ifndef SAND_ARCH_X86_TIMER_H
#define SAND_ARCH_X86_TIMER_H
#include <stdint.h>
#define SAND_TICK_HZ 100u
void timer_init(uint32_t frequency_hz);
uint32_t timer_ticks(void);
uint32_t timer_irq_handler(uint32_t saved_stack);
#endif
