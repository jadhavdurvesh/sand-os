#ifndef SAND_ARCH_X86_IRQ_H
#define SAND_ARCH_X86_IRQ_H
#include <stdint.h>
/* Hardware interrupt lines of the two cascaded 8259 PICs (remapped to
 * vectors 32-47). Line 0 (PIT) is owned by the scheduler tick. */
typedef void (*sand_irq_handler)(void);
int irq_install(uint8_t line, sand_irq_handler handler);  /* installs + unmasks */
void irq_mask(uint8_t line);
void irq_unmask(uint8_t line);
void irq_dispatch(uint32_t line);
#endif
